module;

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

export module engine.jobs.job_system;
import std;
import engine.core.contracts;

export namespace engine::jobs
{

struct Job
{
	using Function = void (*)(void *);

	Function function{nullptr};
	void *context{nullptr};
};

struct JobSystemConfig
{
	// Zero selects half of the standard library's logical CPU count, with one
	// worker as the fallback when the host cannot report a usable count.
	std::size_t workerCount{0};
};

class JobSystem
{
public:
	explicit JobSystem(JobSystemConfig config = {});
	~JobSystem() noexcept;

	JobSystem(const JobSystem &) = delete;
	JobSystem &operator=(const JobSystem &) = delete;

	// Execute is an explicit batch barrier. The caller owns the Job array and
	// must keep it alive until this function returns. A thrown job exception is
	// rethrown after all workers have stopped processing the batch.
	void Execute(std::span<Job> jobs);
	// Submit copies the job descriptors and returns as soon as the batch is
	// published. Job contexts must remain valid until TryComplete/Wait reports
	// completion, or until Shutdown returns. Batch submission and completion
	// belong to one coordinating caller; these operations are not reentrant.
	void Submit(std::span<Job> jobs);
	// Polls an asynchronous batch without waiting. It returns true only after
	// all jobs have completed and the batch has been retired.
	[[nodiscard]] bool TryComplete();
	// Completes the currently submitted batch, waiting only when a caller
	// explicitly reaches a dependency boundary.
	void Wait();
	void Shutdown() noexcept;

	[[nodiscard]] std::size_t WorkerCount() const noexcept { return m_workerCount; }

	// A burst (several batches back to back, such as one simulation tick's waves): while one is open, idle workers keep
	// spinning between batches instead of going to sleep, so each batch starts on hot threads. Which thread runs a
	// job never changes a result; only the hand-off latency. Bursts nest.
	void BeginBurst() noexcept { m_bursts.fetch_add(1, std::memory_order_acq_rel); }
	void EndBurst() noexcept { m_bursts.fetch_sub(1, std::memory_order_acq_rel); }

	class Burst
	{
	public:
		explicit Burst(JobSystem &jobs) noexcept : m_jobs(&jobs) { m_jobs->BeginBurst(); }
		~Burst() noexcept { m_jobs->EndBurst(); }
		Burst(const Burst &) = delete;
		Burst &operator=(const Burst &) = delete;

	private:
		JobSystem *m_jobs;
	};

private:
	struct WorkDeque;

	void WorkerLoop(std::size_t workerIndex);
	// Runs a claimed job (skipped once the batch has failed) and counts it done; the last one wakes a blocked waiter.
	void RunClaimed(const Job &job) noexcept;
	// Closes the published batch: late workers no longer enter it.
	void CloseBatch() noexcept;

	std::vector<std::thread> m_workers;
	std::vector<std::unique_ptr<WorkDeque>> m_deques;
	std::size_t m_workerCount{0};
	// The published batch in ONE word: its sequence number above bit 17, how many workers take part in bits 1-16, and
	// whether it is open (bit 0). A worker takes part in an open batch only after announcing itself (m_inside) and
	// seeing it still open, so a batch completes when its jobs are done (m_pending) and never waits for a worker that
	// has not even woken up: that one simply finds the batch closed.
	alignas(64) std::atomic<std::uint64_t> m_batch{0};
	alignas(64) std::atomic<std::size_t> m_pending{0};
	alignas(64) std::atomic<std::size_t> m_inside{0};
	// Blocking hand-off once spinning gives up: workers sleep on m_wake for a new batch, the caller on m_done for the
	// batch's last job. The notifier checks the sleeper counts after publishing (sequentially consistent), so a wake-up
	// cannot slip between a sleeper's check and its wait.
	alignas(64) std::atomic<std::size_t> m_sleepers{0};
	alignas(64) std::atomic<std::size_t> m_doneWaiters{0};
	std::mutex m_wakeMutex;
	std::condition_variable m_wake;
	std::mutex m_doneMutex;
	std::condition_variable m_done;
	std::exception_ptr m_failure{};
	alignas(64) std::atomic<bool> m_stopping{false};
	alignas(64) std::atomic<bool> m_failureReady{false};
	// 0 = idle, 1 = executing, 2 = stopping, 3 = preparing. Preparation must
	// finish before shutdown can drain a batch or release worker storage.
	alignas(64) std::atomic<std::uint32_t> m_lifecycle{0};
	alignas(64) std::atomic<bool> m_batchFailed{false};
	alignas(64) std::atomic<std::uint32_t> m_bursts{0};
};

} // namespace engine::jobs

namespace engine::jobs
{

namespace
{

inline void spinPause(std::size_t &iterations) noexcept
{
	const auto iteration = iterations++;
	// Spin briefly for hand-off latency, then yield. Long waits block
	// instead (see SpinLimit): sleeping would round up to the OS timer
	// tick (~15.6 ms on Windows) on every batch hand-off.
	if (iteration >= 1024)
	{
		std::this_thread::yield();
		return;
	}
#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
	_mm_pause();
#else
	std::this_thread::yield();
#endif
	if ((iteration & 0x3fu) == 0)
		std::this_thread::yield();
}

// Spins before a waiter blocks.
constexpr std::size_t SpinLimit = 1024;

constexpr std::uint64_t OpenBit = 1u;
constexpr unsigned ParticipantShift = 1;
constexpr std::uint64_t ParticipantMask = 0xFFFFu;
constexpr unsigned SequenceShift = 17;

constexpr bool IsOpen(std::uint64_t batch) noexcept { return (batch & OpenBit) != 0; }
constexpr std::size_t ParticipantsOf(std::uint64_t batch) noexcept { return static_cast<std::size_t>((batch >> ParticipantShift) & ParticipantMask); }

} // namespace

// Fixed-size Chase-Lev work-stealing deque. The owner removes work from the
// bottom; any other worker may steal from the top. A batch is fully populated
// before it is published, and only re-populated once no worker is inside, so no
// allocation or producer lock is needed while workers are running.
struct JobSystem::WorkDeque
{
	std::vector<Job> entries;
	alignas(64) std::atomic<std::size_t> top{0};
	alignas(64) std::atomic<std::size_t> bottom{0};

	void prepare(std::size_t expected)
	{
		entries.clear();
		if (entries.capacity() < expected)
			entries.reserve(expected);
		top.store(0, std::memory_order_relaxed);
		bottom.store(0, std::memory_order_relaxed);
	}

	bool popBottom(Job &job) noexcept
	{
		const auto current = bottom.load(std::memory_order_relaxed);
		if (current == 0)
			return false;
		const auto next = current - 1;
		bottom.store(next, std::memory_order_relaxed);
		// The bottom decrement must become ordered before observing top. Without
		// this fence, an owner and thief can both claim the last remaining entry.
		std::atomic_thread_fence(std::memory_order_seq_cst);
		auto limit = top.load(std::memory_order_acquire);
		if (limit > next)
		{
			bottom.store(current, std::memory_order_relaxed);
			return false;
		}
		if (limit == next)
		{
			const bool claimed = top.compare_exchange_strong(limit, limit + 1, std::memory_order_seq_cst, std::memory_order_relaxed);
			bottom.store(current, std::memory_order_relaxed);
			if (!claimed)
				return false;
		}
		job = entries[next];
		return true;
	}

	bool stealTop(Job &job) noexcept
	{
		auto current = top.load(std::memory_order_acquire);
		std::atomic_thread_fence(std::memory_order_seq_cst);
		const auto limit = bottom.load(std::memory_order_acquire);
		if (current >= limit)
			return false;
		job = entries[current];
		return top.compare_exchange_strong(current, current + 1, std::memory_order_seq_cst, std::memory_order_relaxed);
	}

	bool hasWork() const noexcept { return top.load(std::memory_order_acquire) < bottom.load(std::memory_order_acquire); }
};

JobSystem::JobSystem(const JobSystemConfig config)
{
	m_workerCount = config.workerCount;
	if (m_workerCount == 0)
	{
		const unsigned int logicalCores = std::thread::hardware_concurrency();
		m_workerCount = logicalCores > 1 ? static_cast<std::size_t>(logicalCores / 2) : 1;
	}

	try
	{
		m_workers.reserve(m_workerCount);
		m_deques.reserve(m_workerCount);
		for (std::size_t index = 0; index < m_workerCount; ++index)
			m_deques.emplace_back(std::make_unique<WorkDeque>());
		for (std::size_t index = 0; index < m_workerCount; ++index)
			m_workers.emplace_back([this, index] { WorkerLoop(index); });
	}
	catch (...)
	{
		Shutdown();
		throw;
	}
}

JobSystem::~JobSystem() noexcept
{
	Shutdown();
}

void JobSystem::CloseBatch() noexcept
{
	m_batch.store(m_batch.load(std::memory_order_relaxed) & ~OpenBit, std::memory_order_seq_cst);
}

void JobSystem::Shutdown() noexcept
{
	std::size_t spins = 0;
	for (;;)
	{
		const std::uint32_t state = m_lifecycle.load(std::memory_order_acquire);
		if (state == 2)
			return;
		// A submitted batch is drained first: its jobs all run (or are skipped after a failure).
		if (state == 3 || (state == 1 && m_pending.load(std::memory_order_acquire) != 0))
		{
			spinPause(spins);
			continue;
		}
		std::uint32_t expected = state;
		if (m_lifecycle.compare_exchange_weak(expected, 2, std::memory_order_acq_rel))
			break;
	}
	CloseBatch();
	m_stopping.store(true, std::memory_order_seq_cst);
	{
		const std::lock_guard lock(m_wakeMutex);
	}
	m_wake.notify_all();
	for (std::thread &worker : m_workers)
	{
		if (worker.joinable())
			worker.join();
	}
}

void JobSystem::Execute(const std::span<Job> jobs)
{
	Submit(jobs);
	Wait();
}

void JobSystem::Submit(const std::span<Job> jobs)
{
	for (const Job &job : jobs)
	{
		if (job.function == nullptr)
			throw std::invalid_argument("Engine job batches cannot contain an empty job");
	}
	if (jobs.empty())
		return;

	std::uint32_t expected = 0;
	if (!m_lifecycle.compare_exchange_strong(expected, 3, std::memory_order_acq_rel))
	{
		if (expected == 2)
			throw std::logic_error("Cannot execute jobs after job-system shutdown");
		throw std::logic_error("Engine job-system execution is not reentrant");
	}

	// The last batch is closed; workers still inside it (finding no work) leave before the deques are refilled.
	std::size_t spins = 0;
	while (m_inside.load(std::memory_order_seq_cst) != 0)
		spinPause(spins);

	m_failure = nullptr;
	// A one- or two-request batch must not wake every persistent worker. The
	// participating prefix still uses the same Chase--Lev deques and stealing;
	// this only removes idle workers from the batch's active set.
	const auto activeWorkers = (std::min)((std::min)(m_workerCount, jobs.size()), static_cast<std::size_t>(ParticipantMask));
	const auto perWorker = (jobs.size() + activeWorkers - 1) / activeWorkers;
	try
	{
		for (auto &deque : m_deques)
			deque->prepare(perWorker);
	}
	catch (...)
	{
		// Nothing has been published: workers cannot see this batch.
		m_lifecycle.store(0, std::memory_order_release);
		throw;
	}
	// The owner side of a Chase--Lev deque is LIFO. Populate each local deque
	// in reverse canonical order so a one-worker batch executes FIFO, which is
	// required by deterministic callers such as the ECS scheduler. Thieves
	// still take the opposite end and retain normal work-stealing behaviour.
	for (std::size_t remaining = jobs.size(); remaining != 0; --remaining)
	{
		const auto index = remaining - 1;
		const auto owner = index % activeWorkers;
		m_deques[owner]->entries.push_back(jobs[index]);
	}
	for (auto &deque : m_deques)
	{
		deque->top.store(0, std::memory_order_relaxed);
		deque->bottom.store(deque->entries.size(), std::memory_order_relaxed);
	}
	m_batchFailed.store(false, std::memory_order_relaxed);
	m_failureReady.store(false, std::memory_order_relaxed);
	m_pending.store(jobs.size(), std::memory_order_relaxed);
	m_lifecycle.store(1, std::memory_order_release);
	const std::uint64_t sequence = (m_batch.load(std::memory_order_relaxed) >> SequenceShift) + 1;
	m_batch.store((sequence << SequenceShift) | (static_cast<std::uint64_t>(activeWorkers) << ParticipantShift) | OpenBit, std::memory_order_seq_cst);
	if (m_sleepers.load(std::memory_order_seq_cst) != 0)
	{
		{
			const std::lock_guard lock(m_wakeMutex);
		}
		m_wake.notify_all();
	}
}

bool JobSystem::TryComplete()
{
	auto state = m_lifecycle.load(std::memory_order_acquire);
	if (state == 0 || state == 2)
		return true;
	if (state == 3 || m_pending.load(std::memory_order_acquire) != 0)
		return false;

	const std::exception_ptr failure = m_failureReady.load(std::memory_order_acquire) ? m_failure : nullptr;
	// A completed poll must never reopen a system that shutdown has closed.
	if (!m_lifecycle.compare_exchange_strong(state, 0, std::memory_order_acq_rel))
		return state == 2;
	CloseBatch();
	if (failure != nullptr)
		std::rethrow_exception(failure);
	return true;
}

void JobSystem::RunClaimed(const Job &job) noexcept
{
	if (!m_batchFailed.load(std::memory_order_acquire))
	{
		try
		{
			job.function(job.context);
		}
		catch (...)
		{
			if (!m_batchFailed.exchange(true, std::memory_order_acq_rel))
			{
				m_failure = std::current_exception();
				m_failureReady.store(true, std::memory_order_release);
			}
		}
	}
	if (m_pending.fetch_sub(1, std::memory_order_acq_rel) == 1 && m_doneWaiters.load(std::memory_order_seq_cst) != 0)
	{
		{
			const std::lock_guard lock(m_doneMutex);
		}
		m_done.notify_all();
	}
}

void JobSystem::Wait()
{
	// With more than one worker, the caller takes jobs of its own batch while it waits (stealing, as a
	// worker would): a one- or two-job batch runs at once instead of waiting for a worker to wake, and jobs
	// queued for a worker that is not running are done anyway. (A one-worker system stays strictly serial.)
	// Then spin for a fast hand-off, then block until the last job is done.
	if (m_workerCount > 1 && m_lifecycle.load(std::memory_order_acquire) == 1)
	{
		for (;;)
		{
			Job job{};
			bool claimed = false;
			for (const auto &deque : m_deques)
				if (deque->stealTop(job))
				{
					claimed = true;
					break;
				}
			if (!claimed)
				break;
			RunClaimed(job);
		}
	}
	std::size_t spins = 0;
	while (!TryComplete())
	{
		if (spins >= SpinLimit)
		{
			m_doneWaiters.fetch_add(1, std::memory_order_seq_cst);
			{
				std::unique_lock lock(m_doneMutex);
				m_done.wait_for(lock, std::chrono::milliseconds(1), [&] { return m_pending.load(std::memory_order_seq_cst) == 0; });
			}
			m_doneWaiters.fetch_sub(1, std::memory_order_seq_cst);
			continue;
		}
		spinPause(spins);
	}
}

void JobSystem::WorkerLoop(const std::size_t workerIndex)
{
	std::uint64_t observed = 0;
	for (;;)
	{
		std::size_t spins = 0;
		std::uint64_t batch = m_batch.load(std::memory_order_acquire);
		while (!m_stopping.load(std::memory_order_acquire) && batch == observed)
		{
			// Idle workers block instead of spinning once the hand-off window passes (not during a burst).
			if (spins >= SpinLimit && m_bursts.load(std::memory_order_acquire) == 0)
			{
				m_sleepers.fetch_add(1, std::memory_order_seq_cst);
				{
					std::unique_lock lock(m_wakeMutex);
					m_wake.wait_for(lock, std::chrono::milliseconds(1), [&] {
						return m_stopping.load(std::memory_order_seq_cst) || m_batch.load(std::memory_order_seq_cst) != observed;
					});
				}
				m_sleepers.fetch_sub(1, std::memory_order_seq_cst);
			}
			else
				spinPause(spins);
			batch = m_batch.load(std::memory_order_acquire);
		}
		observed = batch;
		if (!IsOpen(batch) || workerIndex >= ParticipantsOf(batch))
		{
			if (m_stopping.load(std::memory_order_acquire))
				return;
			continue;
		}
		// Announce, then look again: the caller closes a batch before it waits for the workers inside to leave, so
		// either it waits for this one, or this one sees the batch closed and keeps out of the deques.
		m_inside.fetch_add(1, std::memory_order_seq_cst);
		if (m_batch.load(std::memory_order_seq_cst) != batch)
		{
			m_inside.fetch_sub(1, std::memory_order_seq_cst);
			continue;
		}
		const std::size_t participants = ParticipantsOf(batch);
		for (;;)
		{
			Job job{};
			bool claimed = m_deques[workerIndex]->popBottom(job);
			for (std::size_t offset = 1; offset <= participants && !claimed; ++offset)
				claimed = m_deques[(workerIndex + offset) % participants]->stealTop(job);
			if (!claimed)
			{
				bool workRemains = false;
				for (std::size_t index = 0; index < participants && !workRemains; ++index)
					workRemains = m_deques[index]->hasWork();
				if (!workRemains)
					break;
				spinPause(spins);
				continue;
			}
			RunClaimed(job);
		}
		m_inside.fetch_sub(1, std::memory_order_seq_cst);
	}
}

} // namespace engine::jobs
