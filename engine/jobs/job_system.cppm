module;

#include <atomic>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

export module engine.jobs.job_system;

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

private:
 struct WorkDeque;

 void WorkerLoop(std::size_t workerIndex);

 std::vector<std::thread> m_workers;
 std::vector<std::unique_ptr<WorkDeque>> m_deques;
 alignas(64) std::atomic<std::size_t> m_activeWorkers{0};
 alignas(64) std::atomic<std::size_t> m_activeWorkerLimit{0};
 std::size_t m_workerCount{0};
	alignas(64) std::atomic<std::uint64_t> m_generation{0};
	std::exception_ptr m_failure{};
	alignas(64) std::atomic<bool> m_stopping{false};
	alignas(64) std::atomic<bool> m_failureReady{false};
	// 0 = idle, 1 = executing, 2 = stopping, 3 = preparing. Preparation must
	// finish before shutdown can drain a batch or release worker storage.
	alignas(64) std::atomic<std::uint32_t> m_lifecycle{0};

 alignas(64) std::atomic<bool> m_batchFailed{false};
};

} // namespace engine::jobs

namespace engine::jobs
{

namespace
{

inline void spinPause(std::size_t &iterations) noexcept
{
 const auto iteration=iterations++;
 // Spin briefly for hand-off latency, then back off so an idle game does not
 // burn a logical processor per worker. This scheduler has no direct OS APIs.
 if (iteration>=1024) {
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

} // namespace

// Fixed-size Chase-Lev work-stealing deque.  The owner removes work from the
// bottom; any other worker may steal from the top.  A batch is fully populated
// before its generation is published, so no allocation or producer lock is
// needed while workers are running.
struct JobSystem::WorkDeque
{
 std::vector<Job> entries;
 alignas(64) std::atomic<std::size_t> top{0};
 alignas(64) std::atomic<std::size_t> bottom{0};

 void prepare(std::size_t expected) {
  entries.clear();
  if (entries.capacity()<expected) entries.reserve(expected);
  top.store(0, std::memory_order_relaxed);
  bottom.store(0, std::memory_order_relaxed);
 }

 bool popBottom(Job &job) noexcept {
  const auto current = bottom.load(std::memory_order_relaxed);
  if (current == 0) return false;
  const auto next = current - 1;
  bottom.store(next, std::memory_order_relaxed);
  // The bottom decrement must become ordered before observing top. Without
  // this fence, an owner and thief can both claim the last remaining entry.
  std::atomic_thread_fence(std::memory_order_seq_cst);
  auto limit = top.load(std::memory_order_acquire);
  if (limit > next) {
   bottom.store(current, std::memory_order_relaxed);
   return false;
  }
  if (limit == next) {
   const bool claimed=top.compare_exchange_strong(limit, limit + 1,
          std::memory_order_seq_cst, std::memory_order_relaxed);
   bottom.store(current, std::memory_order_relaxed);
   if (!claimed) return false;
  }
  job = entries[next];
  return true;
 }

 bool stealTop(Job &job) noexcept {
  auto current = top.load(std::memory_order_acquire);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  const auto limit = bottom.load(std::memory_order_acquire);
  if (current >= limit) return false;
  job = entries[current];
  return top.compare_exchange_strong(current, current + 1,
      std::memory_order_seq_cst, std::memory_order_relaxed);
 }

 bool hasWork() const noexcept {
  return top.load(std::memory_order_acquire) < bottom.load(std::memory_order_acquire);
 }
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

void JobSystem::Shutdown() noexcept
{
	std::size_t spins = 0;
	for (;;)
	{
		const std::uint32_t state = m_lifecycle.load(std::memory_order_acquire);
		if (state == 2)
			return;
		if (state == 3 || (state == 1 && m_activeWorkers.load(std::memory_order_acquire) != 0))
		{
			spinPause(spins);
			continue;
		}
		std::uint32_t expected = state;
		if (m_lifecycle.compare_exchange_weak(expected, 2, std::memory_order_acq_rel))
			break;
	}
 // Workers are persistent and wait by spinning on this generation rather
 // than blocking on a mutex/condition variable.  spinPause periodically
 // yields so an idle scheduler does not pin every logical processor.
 m_stopping.store(true, std::memory_order_release);
	m_generation.fetch_add(1, std::memory_order_acq_rel);
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

	// Populate each worker's local deque before publishing the generation.
	// Workers acquire the generation and consequently see stable deque storage.
	m_failure = nullptr;
	// A one- or two-request batch must not wake every persistent worker. The
	// participating prefix still uses the same Chase--Lev deques and stealing;
	// this only removes idle workers from the batch's active set.
	const auto activeWorkers=(std::min)(m_workerCount,jobs.size());
	// Keep the per-worker backing arrays alive across batches.  The old path
	// cleared each deque and then appended one descriptor at a time, causing
	// repeated growth/copies during every 10k-unit admission burst.
	const auto perWorker=(jobs.size()+activeWorkers-1)/activeWorkers;
	try {
		for (auto &deque : m_deques) deque->prepare(perWorker);
	} catch (...) {
		// No generation has been published: workers cannot see this batch.
		m_lifecycle.store(0, std::memory_order_release);
		throw;
	}
	// The owner side of a Chase--Lev deque is LIFO. Populate each local deque
	// in reverse canonical order so a one-worker batch executes FIFO, which is
	// required by deterministic callers such as the ECS scheduler. Thieves
	// still take the opposite end and retain normal work-stealing behaviour.
	for (std::size_t remaining = jobs.size(); remaining != 0; --remaining) {
		const auto index = remaining - 1;
		const auto owner = index % activeWorkers;
		const Job job = jobs[index];
		auto &entries = m_deques[owner]->entries;
		entries.push_back(job);
	}
	for (auto &deque : m_deques) {
		deque->top.store(0, std::memory_order_relaxed);
		deque->bottom.store(deque->entries.size(), std::memory_order_relaxed);
	}
	m_batchFailed.store(false, std::memory_order_relaxed);
	m_failureReady.store(false, std::memory_order_relaxed);
	m_activeWorkerLimit.store(activeWorkers, std::memory_order_relaxed);
	m_activeWorkers.store(activeWorkers, std::memory_order_relaxed);
	m_lifecycle.store(1, std::memory_order_release);
	m_generation.fetch_add(1, std::memory_order_release);
}

bool JobSystem::TryComplete()
{
	auto state = m_lifecycle.load(std::memory_order_acquire);
	if (state == 0 || state == 2)
		return true;
	if (state == 3 || m_activeWorkers.load(std::memory_order_acquire) != 0)
		return false;

	const std::exception_ptr failure = m_failureReady.load(std::memory_order_acquire) ? m_failure : nullptr;
	// A completed poll must never reopen a system that shutdown has closed.
	if (!m_lifecycle.compare_exchange_strong(state, 0, std::memory_order_acq_rel))
		return state == 2;
	if (failure != nullptr)
		std::rethrow_exception(failure);
	return true;
}

void JobSystem::Wait()
{
	std::size_t spins = 0;
	while (!TryComplete())
		spinPause(spins);
}

void JobSystem::WorkerLoop(const std::size_t workerIndex)
{
	std::uint64_t observedGeneration = 0;
	for (;;)
	{
		std::size_t spins = 0;
		std::uint64_t generation = m_generation.load(std::memory_order_acquire);
		while (!m_stopping.load(std::memory_order_acquire) && generation == observedGeneration)
		{
			spinPause(spins);
			generation = m_generation.load(std::memory_order_acquire);
		}
		if (m_stopping.load(std::memory_order_acquire))
			return;
		observedGeneration = generation;
  const auto activeLimit=m_activeWorkerLimit.load(std::memory_order_acquire);
  if (workerIndex>=activeLimit) continue;
  while (!m_batchFailed.load(std::memory_order_acquire))
  {
   Job job{};
   bool claimed = m_deques[workerIndex]->popBottom(job);
   if (!claimed) {
    for (std::size_t offset = 1; offset <= activeLimit && !claimed; ++offset) {
     const auto victim = (workerIndex + offset) % activeLimit;
     claimed = m_deques[victim]->stealTop(job);
    }
   }
   if (!claimed) {
    bool workRemains = false;
    for (const auto &deque : m_deques) {
     if (deque->hasWork()) { workRemains = true; break; }
    }
    if (!workRemains) break;
    spinPause(spins);
    continue;
   }

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
    break;
   }
  }

		const std::size_t previous = m_activeWorkers.fetch_sub(1, std::memory_order_acq_rel);
		assert(previous > 0);
		(void)previous;
	}
}

} // namespace engine::jobs
