module;

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define ENGINE_ECS_SCHEDULER_TSC 1
#endif

export module engine.ecs.scheduler.scheduler;
import std;

export import engine.jobs.job_system;
export import engine.ecs.system.system;
export import engine.ecs.system.job_pool;

export namespace ecs
{

class DependencyGraph
{
public:
	std::size_t NodeCount() const noexcept { return m_edges.size(); }
	const std::vector<SystemId> &Edges(SystemId system) const { return m_edges.at(system); }

	bool HasPath(SystemId from, SystemId to) const;

private:
	void AddEdge(SystemId from, SystemId to);

	std::vector<std::vector<SystemId>> m_edges;

	friend class Scheduler;
};

struct ExecutionPlan
{
	struct PhasePlan
	{
		SystemPhase phase{SystemPhase::Simulation};
		std::vector<std::vector<SystemId>> waves;
	};

	const std::vector<PhasePlan> &Phases() const noexcept { return phases; }

private:
	std::vector<PhasePlan> phases;

	friend class Scheduler;
};

extern "C++"
{
class Scheduler
{
public:
	Scheduler(World &world, SystemRegistry &systems) :
		Scheduler(world, systems, engine::jobs::JobSystemConfig{})
	{
	}
	Scheduler(World &world, SystemRegistry &systems, engine::jobs::JobSystemConfig config);
	Scheduler(World &world, SystemRegistry &systems, engine::jobs::JobSystem &jobs) noexcept;

	~Scheduler() noexcept;

	Scheduler(const Scheduler &) = delete;
	Scheduler &operator=(const Scheduler &) = delete;

	void Finalize(engine::time::FixedStep step);
	bool IsFinalized() const noexcept { return m_finalized; }
	bool IsFailed() const noexcept { return m_failed; }

	const DependencyGraph &Graph() const noexcept { return m_graph; }
	const ExecutionPlan &Plan() const noexcept { return m_plan; }

	void Execute(engine::time::SimulationTime time);

	// Where a tick's time goes (off by default: no timing cost). Per system: its chunks' and batch run's summed
	// CPU nanoseconds and how often it ran; `waveNanos`: wall time of the waves (scheduling and commits included).
	struct SystemTiming
	{
		std::string_view name;
		std::uint64_t cpuNanos{0};
		std::uint64_t runs{0};
		std::uint64_t hookNanos{0}; // of cpuNanos: its caller-side hooks
	};
	void EnableProfiling(bool enabled);
	std::vector<SystemTiming> Profile() const;
	std::uint64_t WaveNanos() const noexcept { return m_waveNanos; }
	// Where the waves' wall time goes besides the systems' own work (profiling on): building the wave's contexts and
	// buffers, the caller-side hooks, running the jobs (wall), committing; and how many jobs and non-empty waves ran.
	struct WaveOverhead
	{
		std::uint64_t prepareNanos{0}, setupNanos{0}, hookNanos{0}, jobNanos{0}, commitNanos{0};
		std::uint64_t jobs{0}, waves{0}, commits{0};
	};
	WaveOverhead Overhead() const noexcept { return m_overhead; }
	// Each wave's summed wall nanoseconds (profiling on), in plan order (as WavePlan).
	const std::vector<std::uint64_t> &WaveWallNanos() const noexcept { return m_waveWall; }
	// Per wave (profiling on): its longest job's summed nanoseconds (the wave's critical path through the pool) and
	// its summed job count.
	const std::vector<std::uint64_t> &WaveLongestJobNanos() const noexcept { return m_waveLongest; }
	const std::vector<std::uint64_t> &WaveJobCounts() const noexcept { return m_waveJobCounts; }
	// Waves a tick runs (each a barrier and a commit), and whether a system runs as a batch (on the caller, alone).
	std::size_t WaveCount() const noexcept
	{
		std::size_t count = 0;
		for (const auto &phase : m_plan.Phases())
			count += phase.waves.size();
		return count;
	}
	// The plan's waves in the order they run, each its systems' stable names (for profiling the wave structure).
	std::vector<std::vector<std::string_view>> WavePlan() const
	{
		std::vector<std::vector<std::string_view>> plan;
		for (const auto &phase : m_plan.Phases())
			for (const auto &wave : phase.waves)
			{
				auto &names = plan.emplace_back();
				for (const SystemId system : wave)
					names.push_back(m_systems->Get(system).stableName);
			}
		return plan;
	}
	bool IsBatch(std::string_view name) const
	{
		for (SystemId system = 0; system < m_systems->Count(); ++system)
			if (m_systems->Get(system).stableName == name)
				return m_systems->Get(system).batch;
		return false;
	}

private:
	struct QueryBinding
	{
		void *query{nullptr};
		SystemInfo::DestroyQueryFunction destroy{nullptr};
	};

	static DependencyGraph BuildGraph(const SystemRegistry &systems);
	static ExecutionPlan BuildPlan(const SystemRegistry &systems, const DependencyGraph &graph);
	struct PreparedSystem
	{
		const SystemInfo *info{nullptr};
		void *query{nullptr};
		std::size_t chunkCount{0};
		std::size_t rows{0};
		std::uint64_t estimate{0}; // predicted cycles (cost model)
		std::size_t beforeContext{0}, afterContext{0};
	};

	// One job: a contiguous run of one system's prepared chunks (a batch system's single run), with one command
	// buffer and one context whose chunk order follows the chunk being run.
	struct ChunkExecution
	{
		SystemInfo::ExecuteChunkFunction execute{nullptr};
		void *instance{nullptr};
		void *query{nullptr};
		std::uint32_t firstChunk{0}, lastChunk{0};
		std::uint32_t orderOffset{0}; // 1 when the system has lifecycle hooks (their buffers take the ends)
		SystemContext *context{nullptr};
		std::atomic<std::uint64_t> *cycles{nullptr}; // the cost model: where its time adds up
		std::uint32_t *chunkCycles{nullptr}; // the cost model: each of its chunks' last cost (by prepared chunk index)
		std::atomic<std::uint64_t> *cpuNanos{nullptr}; // profiling: where its time adds up
		std::uint64_t nanos{0}; // profiling: this job's time
	};

	static void ExecuteChunkJob(void *context);
	void ExecuteWave(const ExecutionPlan::PhasePlan &phase,
		const std::vector<SystemId> &wave,
		engine::time::SimulationTime time);
	void DestroyQueries() noexcept;

	World *m_world;
	SystemRegistry *m_systems;
	engine::jobs::JobSystem *m_jobSystem{nullptr};
	std::unique_ptr<engine::jobs::JobSystem> m_ownedJobSystem;
	DependencyGraph m_graph;
	ExecutionPlan m_plan;
	std::vector<QueryBinding> m_queries;
	// A wave's working set, kept between waves and ticks so running a wave allocates nothing once warm.
	std::vector<PreparedSystem> m_prepared;
	std::vector<CommandBuffer> m_waveCommands;
	std::vector<SystemContext> m_waveContexts;
	std::vector<ChunkExecution> m_waveExecutions;
	std::vector<engine::jobs::Job> m_waveJobs;
	std::vector<CommandBuffer *> m_waveCommandPointers;
	// The job-size cost model (scheduling only, never seen by results): per system id, the measured cycles of the
	// current wave, and the smoothed cycles per prepared row (8 fraction bits).
	std::unique_ptr<std::atomic<std::uint64_t>[]> m_waveCycles;
	std::vector<std::uint64_t> m_cyclesPerRow;
	// Per system, each prepared chunk's cost as last measured (0: not yet): rows cost unevenly (a structure's footprint
	// against an infantryman's, a chunk's fixed cost against its rows), so jobs are cut by what the chunks took.
	std::vector<std::vector<std::uint32_t>> m_chunkCycles;
	// Profiling (EnableProfiling): per system id.
	bool m_profiling{false};
	std::unique_ptr<std::atomic<std::uint64_t>[]> m_cpuNanos;
	std::vector<std::uint64_t> m_runs;
	std::vector<std::uint64_t> m_hookNanos; // per system: its BeforeChunks/AfterChunks time (on the caller)
	std::uint64_t m_waveNanos{0};
	WaveOverhead m_overhead{};
	std::vector<std::uint64_t> m_waveWall;
	std::vector<std::uint64_t> m_waveLongest;
	std::vector<std::uint64_t> m_waveJobCounts;
	std::size_t m_waveIndex{0}; // the wave running (profiling)
	bool m_finalized{false};
	std::optional<engine::time::FixedStep> m_step;
	bool m_failed{false};
	bool m_executing{false};
};
} // extern "C++"

} // namespace ecs

namespace ecs
{

void DependencyGraph::AddEdge(const SystemId from, const SystemId to)
{
	if (from >= m_edges.size() || to >= m_edges.size())
		throw std::out_of_range("Invalid ECS scheduler dependency node");
	m_edges[from].push_back(to);
}

namespace
{

SystemId ResolveDependency(const SystemRegistry &systems,
	const SystemInfo &source,
	const SystemDependency &dependency)
{
	if (dependency.type == nullptr || *dependency.type == typeid(void))
		throw std::logic_error("ECS scheduler dependency is missing its system type");

	const SystemId targetId = systems.TryGet(*dependency.type);
	if (targetId == InvalidSystemId)
	{
		throw std::logic_error("ECS scheduler system '" + std::string(source.stableName) +
			"' references unregistered system '" + std::string(dependency.stableName) + "'");
	}

	const SystemInfo &target = systems.Get(targetId);
	if (target.stableKey != dependency.stableKey)
	{
		throw std::logic_error("ECS scheduler dependency system type and stable key disagree for '" +
			std::string(dependency.stableName) + "'");
	}
	return targetId;
}

bool FindCycle(const DependencyGraph &graph,
	const SystemId node,
	std::vector<std::uint8_t> &colors,
	std::vector<SystemId> &stack,
	std::vector<SystemId> &cycle)
{
	colors[node] = 1;
	stack.push_back(node);
	for (const SystemId next : graph.Edges(node))
	{
		if (colors[next] == 0)
		{
			if (FindCycle(graph, next, colors, stack, cycle))
				return true;
		}
		else if (colors[next] == 1)
		{
			const auto start = std::find(stack.begin(), stack.end(), next);
			cycle.assign(start, stack.end());
			cycle.push_back(next);
			return true;
		}
	}
	stack.pop_back();
	colors[node] = 2;
	return false;
}

std::string CycleDescription(const SystemRegistry &systems, const std::vector<SystemId> &cycle)
{
	std::string result = "ECS scheduler dependency cycle:";
	for (std::size_t index = 0; index < cycle.size(); ++index)
	{
		result += " " + std::string(systems.Get(cycle[index]).stableName);
		if (index + 1 < cycle.size())
			result += " ->";
	}
	return result;
}

SystemPhase PhaseFromIndex(const std::size_t index)
{
	switch (index)
	{
	case 0:
		return SystemPhase::PreSimulation;
	case 1:
		return SystemPhase::Simulation;
	case 2:
		return SystemPhase::PostSimulation;
	default:
		throw std::out_of_range("Invalid ECS scheduler phase index");
	}
}

} // namespace

bool DependencyGraph::HasPath(const SystemId from, const SystemId to) const
{
	if (from >= m_edges.size() || to >= m_edges.size())
		return false;
	if (from == to)
		return true;

	std::vector<bool> visited(m_edges.size(), false);
	std::vector<SystemId> pending{from};
	visited[from] = true;
	while (!pending.empty())
	{
		const SystemId current = pending.back();
		pending.pop_back();
		for (const SystemId next : m_edges[current])
		{
			if (next == to)
				return true;
			if (!visited[next])
			{
				visited[next] = true;
				pending.push_back(next);
			}
		}
	}
	return false;
}

extern "C++"
{
DependencyGraph Scheduler::BuildGraph(const SystemRegistry &systems)
{
	DependencyGraph graph;
	graph.m_edges.resize(systems.Count());
	const auto &ordered = systems.OrderedSystems();

	for (const SystemInfo *left : ordered)
	{
		for (const SystemInfo *right : ordered)
		{
			if (SystemPhaseIndex(left->phase) < SystemPhaseIndex(right->phase))
				graph.AddEdge(left->id, right->id);
		}
	}

	for (const SystemInfo *info : ordered)
	{
		for (const SystemDependency &dependency : info->Before())
			graph.AddEdge(info->id, ResolveDependency(systems, *info, dependency));
		for (const SystemDependency &dependency : info->After())
			graph.AddEdge(ResolveDependency(systems, *info, dependency), info->id);
	}
	// Group ordering: every member (nested members included) of the earlier
	// group before every member of the later one, within a phase.
	for (const auto &[earlier, later] : systems.m_groupOrdering)
		for (const SystemInfo *left : ordered)
		{
			if (std::find(left->groups.begin(), left->groups.end(), earlier) == left->groups.end())
				continue;
			for (const SystemInfo *right : ordered)
				if (right != left && right->phase == left->phase &&
					std::find(right->groups.begin(), right->groups.end(), later) != right->groups.end())
					graph.AddEdge(left->id, right->id);
		}
	for (const auto &[before, after] : systems.m_ordering)
	{
		const SystemId source = systems.TryGet(*before.type);
		if (source == InvalidSystemId)
			throw std::logic_error("ECS composition ordering references unregistered system '" + std::string(before.stableName) + "'");
		const SystemInfo &info = systems.Get(source);
		graph.AddEdge(ResolveDependency(systems, info, before), ResolveDependency(systems, info, after));
	}

	for (std::vector<SystemId> &edges : graph.m_edges)
	{
		std::sort(edges.begin(), edges.end());
		edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
	}

	std::vector<std::uint8_t> colors(graph.NodeCount(), 0);
	std::vector<SystemId> stack;
	std::vector<SystemId> cycle;
	for (SystemId system = 0; system < graph.NodeCount(); ++system)
	{
		if (colors[system] == 0 && FindCycle(graph, system, colors, stack, cycle))
			throw std::logic_error(CycleDescription(systems, cycle));
	}

	// Every unordered conflict at once (the first as the message has always started; the rest after it, one a line).
	std::string conflicts;
	for (SystemId leftId = 0; leftId < graph.NodeCount(); ++leftId)
	{
		const SystemInfo &left = systems.Get(leftId);
		for (SystemId rightId = leftId + 1; rightId < graph.NodeCount(); ++rightId)
		{
			const SystemInfo &right = systems.Get(rightId);
			if (left.phase != right.phase || !left.access.ConflictsWith(right.access))
				continue;
			if (!graph.HasPath(leftId, rightId) && !graph.HasPath(rightId, leftId))
			{
				if (!conflicts.empty())
					conflicts += '\n';
				conflicts += "Ambiguous ECS scheduler conflict between systems '" + std::string(left.stableName) + "' and '" +
					std::string(right.stableName) + "'; add an explicit Before/After dependency";
			}
		}
	}
	if (!conflicts.empty())
		throw std::logic_error(conflicts);

	return graph;
}

ExecutionPlan Scheduler::BuildPlan(const SystemRegistry &systems, const DependencyGraph &graph)
{
	std::vector<std::uint32_t> indegrees(graph.NodeCount(), 0);
	for (SystemId system = 0; system < graph.NodeCount(); ++system)
	{
		for (const SystemId next : graph.Edges(system))
			++indegrees[next];
	}

	std::vector<std::vector<SystemId>> waves;
	std::size_t remaining = graph.NodeCount();
	while (remaining != 0)
	{
		std::vector<SystemId> ready;
		for (SystemId system = 0; system < graph.NodeCount(); ++system)
		{
			if (indegrees[system] == 0)
				ready.push_back(system);
		}
		if (ready.empty())
			throw std::logic_error("ECS scheduler cannot build an execution plan because the graph contains a cycle");

		// Execution may join chunk groups around caller-side batch work, but
		// never introduce extra structural visibility boundaries in a ready set.
		waves.push_back(ready);
		for (const SystemId system : ready)
		{
			indegrees[system] = (std::numeric_limits<std::uint32_t>::max)();
			--remaining;
			for (const SystemId next : graph.Edges(system))
				--indegrees[next];
		}
	}

	ExecutionPlan plan;
	plan.phases.reserve(SystemPhaseCount);
	for (std::size_t index = 0; index < SystemPhaseCount; ++index)
		plan.phases.push_back(ExecutionPlan::PhasePlan{PhaseFromIndex(index), {}});

	for (const std::vector<SystemId> &wave : waves)
	{
		if (wave.empty())
			continue;
		const SystemPhase phase = systems.Get(wave.front()).phase;
		for (const SystemId system : wave)
		{
			if (systems.Get(system).phase != phase)
				throw std::logic_error("ECS scheduler produced a wave spanning multiple phases");
		}
		plan.phases[SystemPhaseIndex(phase)].waves.push_back(wave);
	}

	return plan;
}

Scheduler::~Scheduler() noexcept
{
	DestroyQueries();
}

Scheduler::Scheduler(World &world,
	SystemRegistry &systems,
	const engine::jobs::JobSystemConfig config) :
	m_world(&world),
	m_systems(&systems),
	m_ownedJobSystem(std::make_unique<engine::jobs::JobSystem>(config))
{
	m_jobSystem = m_ownedJobSystem.get();
}

Scheduler::Scheduler(World &world,
	SystemRegistry &systems,
	engine::jobs::JobSystem &jobs) noexcept :
	m_world(&world),
	m_systems(&systems),
	m_jobSystem(&jobs)
{
}

void Scheduler::DestroyQueries() noexcept
{
	for (std::size_t index = 0; index < m_queries.size(); ++index)
	{
		if (m_queries[index].query != nullptr)
		{
			m_queries[index].destroy(m_queries[index].query);
			m_queries[index].query = nullptr;
		}
	}
	m_queries.clear();
}

void Scheduler::Finalize(const engine::time::FixedStep step)
{
	if (m_finalized)
	{
		if (*m_step != step)
			throw std::logic_error("Cannot change a finalized ECS scheduler's simulation step");
		return;
	}
	if (!m_world->ComponentsFinalized())
		throw std::logic_error("ECS component registry must be finalized before scheduler finalization");
	if (!m_systems->IsFrozen())
		throw std::logic_error("ECS system registry must be finalized before scheduler finalization");
	if (m_systems->ComponentSchema() != m_world->Components().SchemaHash())
		throw std::logic_error("ECS system access metadata was finalized against a different component schema");

	DependencyGraph graph = BuildGraph(*m_systems);
	ExecutionPlan plan = BuildPlan(*m_systems, graph);

	std::vector<QueryBinding> queries(m_systems->Count());
	try
	{
		for (const SystemInfo *info : m_systems->OrderedSystems())
		{
			void *query = info->createQuery(*m_world);
			try
			{
				queries[info->id] = QueryBinding{query, info->destroyQuery};
			}
			catch (...)
			{
				info->destroyQuery(query);
				throw;
			}
		}
	}
	catch (...)
	{
		for (std::size_t index = 0; index < queries.size(); ++index)
		{
			if (queries[index].query != nullptr)
				queries[index].destroy(queries[index].query);
		}
		throw;
	}

	m_graph = std::move(graph);
	m_plan = std::move(plan);
	m_queries = std::move(queries);
	m_waveCycles = std::make_unique<std::atomic<std::uint64_t>[]>(m_systems->Count());
	m_cyclesPerRow.assign(m_systems->Count(), 0);
	m_chunkCycles.assign(m_systems->Count(), {});
	m_step = step;
	m_finalized = true;
}

namespace
{
// A cheap monotonic cycle count for the job-size cost model (scheduling only).
inline std::uint64_t CycleCount() noexcept
{
#if defined(ENGINE_ECS_SCHEDULER_TSC)
	return __rdtsc();
#else
	return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// A wave predicted cheaper than this runs on the caller alone: handing it to the pool and joining costs more.
constexpr std::uint64_t InlineWaveCycles = 40'000;
// No job is cut smaller than this.
constexpr std::uint64_t MinimumJobCycles = 12'000;
} // namespace

void Scheduler::ExecuteChunkJob(void *rawContext)
{
	ChunkExecution &job = *static_cast<ChunkExecution *>(rawContext);
	std::chrono::steady_clock::time_point start{};
	if (job.cpuNanos != nullptr)
		start = std::chrono::steady_clock::now();
	const std::uint64_t begin = CycleCount();
	SystemContext &context = *job.context;
	std::uint64_t chunkStart = begin;
	for (std::uint32_t chunk = job.firstChunk; chunk < job.lastChunk; ++chunk)
	{
		context.m_chunkOrder = chunk;
		context.m_jobOrder = chunk + job.orderOffset;
		job.execute(job.instance, job.query, chunk, context);
		// Each chunk's cost, smoothed (half the last, half this run), for cutting the next waves' jobs.
		const std::uint64_t chunkEnd = CycleCount();
		const auto taken = static_cast<std::uint32_t>((std::min)(chunkEnd - chunkStart, std::uint64_t{0x7FFFFFFF}));
		std::uint32_t &model = job.chunkCycles[chunk];
		model = (std::max)(model == 0 ? taken : (model >> 1) + (taken >> 1), 1u);
		chunkStart = chunkEnd;
	}
	job.cycles->fetch_add(chunkStart - begin, std::memory_order_relaxed);
	if (job.cpuNanos != nullptr)
	{
		job.nanos = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
		job.cpuNanos->fetch_add(job.nanos, std::memory_order_relaxed);
	}
}

void Scheduler::ExecuteWave(const ExecutionPlan::PhasePlan &phase,
	const std::vector<SystemId> &wave,
	const engine::time::SimulationTime time)
{
	using Clock = std::chrono::steady_clock;
	const auto nanosSince = [](const Clock::time_point from) {
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - from).count());
	};
	Clock::time_point mark{};
	if (m_profiling)
		mark = Clock::now();
	std::vector<PreparedSystem> &prepared = m_prepared;
	prepared.clear();
	prepared.reserve(wave.size());

	std::size_t totalChunks = 0;
	std::size_t lifecycleContexts = 0;
	std::uint64_t waveEstimate = 0;
	bool unmeasured = false;
	for (const SystemId systemId : wave)
	{
		const SystemInfo &info = m_systems->Get(systemId);
		if (info.prepareQuery == nullptr || info.executeChunk == nullptr || info.preparedChunkRows == nullptr)
			throw std::logic_error("ECS scheduler system is missing chunk execution metadata");
		void *query = m_queries[systemId].query;
		const std::size_t chunkCount = info.prepareQuery(query);
		const bool lifecycle = info.beforeChunks || info.afterChunks;
		if (chunkCount > (std::numeric_limits<std::uint32_t>::max)() - (lifecycle ? 2u : 0u))
			throw std::length_error("ECS scheduler logical chunk order exceeds its representation");
		if (totalChunks > (std::numeric_limits<std::size_t>::max)() - chunkCount)
			throw std::length_error("ECS scheduler chunk batch is too large");
		totalChunks += chunkCount;
		if (lifecycle)
		{
			if (lifecycleContexts > (std::numeric_limits<std::size_t>::max)() - 2)
				throw std::length_error("ECS lifecycle context capacity overflow");
			lifecycleContexts += 2;
		}
		std::size_t rows = 0;
		std::vector<std::uint32_t> &chunkCycles = m_chunkCycles[systemId];
		if (chunkCycles.size() < chunkCount)
			chunkCycles.resize(chunkCount, 0);
		std::uint64_t estimate = 0;
		for (std::size_t chunk = 0; chunk < chunkCount; ++chunk)
		{
			const std::size_t chunkRows = info.preparedChunkRows(query, chunk);
			rows += chunkRows;
			estimate += chunkCycles[chunk] != 0 ? chunkCycles[chunk] : (static_cast<std::uint64_t>(chunkRows) * m_cyclesPerRow[systemId]) >> 8;
		}
		waveEstimate += estimate;
		unmeasured = unmeasured || (chunkCount != 0 && m_cyclesPerRow[systemId] == 0);
		prepared.push_back(PreparedSystem{&info, query, chunkCount, rows, estimate});
	}

	if (m_profiling)
	{
		m_overhead.prepareNanos += nanosSince(mark);
		mark = Clock::now();
	}
	if (totalChunks == 0 && lifecycleContexts == 0)
		return;
	if (lifecycleContexts > (std::numeric_limits<std::size_t>::max)() - totalChunks)
		throw std::length_error("ECS lifecycle context capacity overflow");

	// Cheap waves run on the caller; others are cut into jobs of about an even share of the wave for every thread
	// taking part (the caller helps), and never below the job floor. A system with no measured cost yet (its first
	// run) is shared out a job per chunk, as every system was before the cost model.
	// Which jobs the chunks fall into never shows in the results: a system's chunk runs are contiguous and their
	// buffers commit in chunk order, and every chunk keeps its own chunk order (ChunkOutputs slots).
	const std::size_t threads = m_jobSystem->WorkerCount() + 1;
	const bool parallel = m_jobSystem->WorkerCount() > 1 && (unmeasured || waveEstimate >= InlineWaveCycles);
	const std::uint64_t jobTarget = (std::max)(MinimumJobCycles, waveEstimate / (threads * 2));

	// Addresses into these must hold for the whole wave: cleared, then reserved to the wave's size before any element.
	std::vector<CommandBuffer> &commands = m_waveCommands;
	std::vector<SystemContext> &contexts = m_waveContexts;
	std::vector<ChunkExecution> &executions = m_waveExecutions;
	std::vector<engine::jobs::Job> &jobs = m_waveJobs;
	std::vector<CommandBuffer *> &commandPointers = m_waveCommandPointers;
	commandPointers.clear();
	jobs.clear();
	executions.clear();
	contexts.clear();
	commands.clear();
	commands.reserve(totalChunks + lifecycleContexts);
	contexts.reserve(totalChunks + lifecycleContexts);
	executions.reserve(totalChunks);
	jobs.reserve(totalChunks);
	commandPointers.reserve(totalChunks + lifecycleContexts);

	const auto phaseIndex = static_cast<std::uint32_t>(SystemPhaseIndex(phase.phase));
	for (PreparedSystem &system : prepared)
	{
		const bool lifecycle = system.info->beforeChunks || system.info->afterChunks;
		const std::uint32_t offset = lifecycle ? 1u : 0u;
		if (lifecycle)
		{
			system.beforeContext = contexts.size();
			commands.emplace_back(CommandBufferOrder{phaseIndex, system.info->id, 0, 0});
			contexts.emplace_back(*m_world, commands.back(), time, system.info->id, phase.phase, 0, 0, system.info);
		}
		const std::uint64_t perRow = m_cyclesPerRow[system.info->id];
		const bool split = parallel && perRow != 0 && !system.info->batch && system.estimate > jobTarget;
		std::size_t first = 0;
		while (first < system.chunkCount)
		{
			std::size_t last = system.chunkCount;
			if (parallel && perRow == 0)
				last = first + 1;
			else if (split)
			{
				const std::uint32_t *const chunkCycles = m_chunkCycles[system.info->id].data();
				std::uint64_t cost = 0;
				last = first;
				while (last < system.chunkCount && (last == first || cost < jobTarget))
				{
					cost += chunkCycles[last] != 0 ? chunkCycles[last] : (static_cast<std::uint64_t>(system.info->preparedChunkRows(system.query, last)) * perRow) >> 8;
					++last;
				}
			}
			const auto logical = static_cast<std::uint32_t>(first);
			commands.emplace_back(CommandBufferOrder{phaseIndex, system.info->id, logical + offset, logical});
			contexts.emplace_back(*m_world, commands.back(), time, system.info->id, phase.phase, logical + offset, logical, system.info);
			executions.push_back(ChunkExecution{
				system.info->executeChunk,
				system.info->instance,
				system.query,
				logical,
				static_cast<std::uint32_t>(last),
				offset,
				&contexts.back(),
				&m_waveCycles[system.info->id],
				m_chunkCycles[system.info->id].data(),
				m_profiling ? &m_cpuNanos[system.info->id] : nullptr});
			jobs.push_back(engine::jobs::Job{&ExecuteChunkJob, &executions.back()});
			first = last;
		}
		if (lifecycle)
		{
			system.afterContext = contexts.size();
			const auto order = static_cast<std::uint32_t>(system.chunkCount + 1);
			commands.emplace_back(CommandBufferOrder{phaseIndex, system.info->id, order, order});
			contexts.emplace_back(*m_world, commands.back(), time, system.info->id, phase.phase, order, order, system.info);
		}
	}

	if (m_profiling)
	{
		m_overhead.setupNanos += nanosSince(mark);
		m_overhead.jobs += jobs.size();
		++m_overhead.waves;
		mark = Clock::now();
	}
	m_world->BeginScheduledExecution();
	bool scheduledExecutionActive = true;
	try
	{
		// Caller-side hooks run once, including empty queries. No structural
		// visibility changes here: every buffer commits only after the whole wave.
		for (const PreparedSystem &system : prepared)
			if (system.info->beforeChunks)
			{
				const Clock::time_point hookStart = m_profiling ? Clock::now() : Clock::time_point{};
				system.info->beforeChunks(system.info->instance, system.query, contexts[system.beforeContext]);
				if (m_profiling)
					m_hookNanos[system.info->id] += nanosSince(hookStart);
			}
		if (m_profiling)
		{
			m_overhead.hookNanos += nanosSince(mark);
			mark = Clock::now();
		}
		if (!parallel)
		{
			// Too little work to share: the caller runs it all (the wave's systems never conflict, and commands commit
			// in logical order whoever ran them).
			for (ChunkExecution &execution : executions)
				ExecuteChunkJob(&execution);
		}
		else
		{
			// A batch system is one job beside the chunk jobs of the others (the wave's systems never conflict, and every
			// command stays deferred until the whole wave has succeeded, then commits in logical order: which thread ran
			// what never shows). One that borrows the pool (BorrowsJobs) runs alone on the caller, outstanding work joined.
			std::size_t firstJob = 0, nextJob = 0;
			for (const PreparedSystem &system : prepared)
			{
				if (system.chunkCount == 0)
					continue;
				if (system.info->borrowsJobs)
				{
					if (nextJob != firstJob)
						m_jobSystem->Execute(std::span<engine::jobs::Job>(jobs).subspan(firstJob, nextJob - firstJob));
					ExecuteChunkJob(&executions[nextJob]);
					++nextJob;
					firstJob = nextJob;
				}
				else
					while (nextJob < executions.size() && executions[nextJob].instance == system.info->instance)
						++nextJob;
			}
			if (nextJob != firstJob)
				m_jobSystem->Execute(std::span<engine::jobs::Job>(jobs).subspan(firstJob, nextJob - firstJob));
		}
		if (m_profiling)
		{
			m_overhead.jobNanos += nanosSince(mark);
			mark = Clock::now();
		}
		for (const PreparedSystem &system : prepared)
			if (system.info->afterChunks)
			{
				const Clock::time_point hookStart = m_profiling ? Clock::now() : Clock::time_point{};
				system.info->afterChunks(system.info->instance, system.query, contexts[system.afterContext]);
				if (m_profiling)
					m_hookNanos[system.info->id] += nanosSince(hookStart);
			}
		if (m_profiling)
		{
			m_overhead.hookNanos += nanosSince(mark);
			mark = Clock::now();
		}
		m_world->EndScheduledExecution();
		scheduledExecutionActive = false;
	}
	catch (...)
	{
		if (scheduledExecutionActive)
			m_world->EndScheduledExecution();
		throw;
	}

	// The cost model learns each system's cycles per row (smoothed), for cutting the next waves' jobs.
	for (const PreparedSystem &system : prepared)
	{
		const std::uint64_t cycles = m_waveCycles[system.info->id].exchange(0, std::memory_order_relaxed);
		if (system.rows == 0 || system.chunkCount == 0)
			continue;
		const std::uint64_t perRow = (cycles << 8) / system.rows;
		std::uint64_t &model = m_cyclesPerRow[system.info->id];
		model = model == 0 ? perRow : (model * 3 + perRow) / 4;
		if (model == 0)
			model = 1;
	}

	for (CommandBuffer &command : commands)
	{
		if (!command.Empty())
			commandPointers.push_back(&command);
	}

	// Commit is a deterministic barrier. World::Commit sorts the worker-local
	// buffers by their logical scheduler order before applying them.
	if (!commandPointers.empty())
		m_world->Commit(std::span<CommandBuffer *>(commandPointers));
	if (m_profiling)
	{
		m_overhead.commitNanos += nanosSince(mark);
		m_overhead.commits += commandPointers.empty() ? 0u : 1u;
		if (m_waveIndex < m_waveLongest.size())
		{
			std::uint64_t longest = 0;
			for (const ChunkExecution &execution : executions)
				longest = (std::max)(longest, execution.nanos);
			m_waveLongest[m_waveIndex] += longest;
			m_waveJobCounts[m_waveIndex] += executions.size();
		}
	}
}

void Scheduler::Execute(const engine::time::SimulationTime time)
{
	if (!m_finalized)
		throw std::logic_error("ECS scheduler must be finalized before execution");
	if (m_failed)
		throw std::logic_error("ECS scheduler cannot execute after a failed simulation tick");
	if (m_executing)
		throw std::logic_error("ECS scheduler execution is not reentrant");
	if (time.Step() != *m_step)
		throw std::invalid_argument("ECS execution time does not match the finalized simulation step");

	m_executing = true;
	// Batch systems that share out work of their own (BorrowsJobs) do it on this schedule's pool.
	if (JobPool *pool = m_world->FindResource<JobPool>())
		pool->jobs = m_jobSystem;
	else
		m_world->EmplaceResource<JobPool>(JobPool{m_jobSystem});
	// The tick's waves follow each other closely: its workers stay hot for all of them.
	const engine::jobs::JobSystem::Burst burst(*m_jobSystem);
	try
	{
		std::size_t waveIndex = 0;
		for (const ExecutionPlan::PhasePlan &phase : m_plan.Phases())
		{
			for (const std::vector<SystemId> &wave : phase.waves)
			{
				const std::size_t thisWave = waveIndex++;
				m_waveIndex = thisWave;
				// Every wave has its own job-local command buffers. This is the
				// structural visibility boundary: later waves see this commit,
				// while jobs in this wave cannot see one another's commands.
				if (!m_profiling)
				{
					ExecuteWave(phase, wave, time);
					continue;
				}
				const auto start = std::chrono::steady_clock::now();
				ExecuteWave(phase, wave, time);
				const auto waveNanos = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
				m_waveNanos += waveNanos;
				if (thisWave < m_waveWall.size())
					m_waveWall[thisWave] += waveNanos;
				for (const SystemId system : wave)
					++m_runs[system];
			}
		}
		m_executing = false;
	}
	catch (...)
	{
		m_executing = false;
		m_failed = true;
		throw;
	}
}

void Scheduler::EnableProfiling(bool enabled)
{
	m_profiling = enabled;
	const std::size_t count = m_systems->Count();
	m_cpuNanos = std::make_unique<std::atomic<std::uint64_t>[]>(count);
	m_runs.assign(count, 0);
	m_hookNanos.assign(count, 0);
	m_waveNanos = 0;
	m_overhead = {};
	m_waveWall.assign(WaveCount(), 0);
	m_waveLongest.assign(WaveCount(), 0);
	m_waveJobCounts.assign(WaveCount(), 0);
}

std::vector<Scheduler::SystemTiming> Scheduler::Profile() const
{
	std::vector<SystemTiming> out;
	if (!m_cpuNanos)
		return out;
	for (std::size_t system = 0; system < m_runs.size(); ++system)
		out.push_back({m_systems->Get(static_cast<SystemId>(system)).stableName, m_cpuNanos[system].load(std::memory_order_relaxed) + m_hookNanos[system],
			m_runs[system], m_hookNanos[system]});
	return out;
}

} // extern "C++"
} // namespace ecs
