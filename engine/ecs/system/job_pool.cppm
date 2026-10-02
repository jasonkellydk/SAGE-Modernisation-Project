export module engine.ecs.system.job_pool;
import std;

export import engine.jobs.job_system;
export import engine.ecs.core.resource_store;

// The pool the running schedule executes on, for a batch system that shares out work of its own (SystemTraits
// BorrowsJobs: it runs alone on the caller, the rest of its wave joined first). The scheduler sets it before each run.
// Deterministic use: every piece of work writes only its own output slot, and the caller merges the slots in index
// order, so which thread ran what never shows.
export namespace ecs
{
struct JobPool
{
	engine::jobs::JobSystem *jobs{nullptr};
};

// `work(index)` for every index in [0, count): shared out on the pool, one job each, or run here in order when there
// is no pool to share with (none, a single worker, a single piece).
template<typename Work>
void ParallelFor(const JobPool &pool, std::size_t count, Work &&work)
{
	if (count == 0)
		return;
	if (pool.jobs == nullptr || pool.jobs->WorkerCount() <= 1 || count == 1)
	{
		for (std::size_t index = 0; index < count; ++index)
			work(index);
		return;
	}
	struct Piece
	{
		std::remove_reference_t<Work> *work;
		std::size_t index;
	};
	std::vector<Piece> pieces(count);
	std::vector<engine::jobs::Job> jobs(count);
	for (std::size_t index = 0; index < count; ++index)
	{
		pieces[index] = Piece{&work, index};
		jobs[index] = engine::jobs::Job{[](void *raw) {
			const Piece &piece = *static_cast<const Piece *>(raw);
			(*piece.work)(piece.index);
		}, &pieces[index]};
	}
	pool.jobs->Execute(jobs);
}

template<>
struct ResourceTraits<JobPool>
{
	static constexpr std::string_view StableName = "engine.ecs.job_pool";
};
}
