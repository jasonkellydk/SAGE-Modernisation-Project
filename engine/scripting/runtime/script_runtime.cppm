export module engine.scripting.runtime.script_runtime;
import std;

export import engine.level.model.level;
export import Engine.Core.Math.FixedRandom;
export import engine.core.serialization.byte_stream;

// Generic trigger-script runtime over the level model's scripts: scripts in
// participant lists and groups, conditions as an OR of AND clauses, actions
// and false actions, one-shot and subroutine scripts, evaluation delays,
// difficulty filtering, and named flags / counters / countdown timers. What a
// condition or action *means* is supplied by a game vocabulary; the runtime
// only resolves names to handlers (once, at load) and runs them in the
// deterministic order: per participant, loose scripts then active
// non-subroutine groups, in authored order.
export namespace engine::scripting
{
namespace Math = Engine::Math;
class ScriptRuntime;

enum class Difficulty : std::uint8_t
{
	Easy,
	Normal,
	Hard
};

// Handed to condition/action handlers.
struct ScriptCallContext
{
	ScriptRuntime &runtime;
	std::size_t participant;
	// What the script is being evaluated for (e.g. one team instance), or
	// NoSubject for scripts evaluated once.
	std::uint64_t subject;
	const level::ScriptCall &call;
	std::uint64_t tick;
	// A value the call keeps between evaluations (the original writes some into its own parameter: Parameter::
	// friend_setInt, e.g. the next frame worth looking again); checkpointed with the runtime. Null when none.
	std::int64_t *memo{nullptr};
};

inline constexpr std::uint64_t NoSubject = ~std::uint64_t{0};

using ConditionHandler = std::function<bool(ScriptCallContext &)>;
using ActionHandler = std::function<void(ScriptCallContext &)>;

// Name -> handler tables, built by the game from its script vocabulary.
class Vocabulary
{
public:
	void AddCondition(std::string name, ConditionHandler handler) { m_conditions.insert_or_assign(std::move(name), std::move(handler)); }
	void AddAction(std::string name, ActionHandler handler) { m_actions.insert_or_assign(std::move(name), std::move(handler)); }
	const ConditionHandler *FindCondition(std::string_view name) const
	{
		const auto found = m_conditions.find(name);
		return found == m_conditions.end() ? nullptr : &found->second;
	}
	const ActionHandler *FindAction(std::string_view name) const
	{
		const auto found = m_actions.find(name);
		return found == m_actions.end() ? nullptr : &found->second;
	}
	// The format's codes in order, for calls stored without a name (an old map's chunks keep only the code): code ->
	// name.
	void SetConditionKinds(std::vector<std::string> names) { m_conditionKinds = std::move(names); }
	void SetActionKinds(std::vector<std::string> names) { m_actionKinds = std::move(names); }
	// A call's name: its stored one, else its code's (none for an unknown code).
	std::string_view ConditionName(const level::ScriptCall &call) const noexcept { return NameOf(call, m_conditionKinds); }
	std::string_view ActionName(const level::ScriptCall &call) const noexcept { return NameOf(call, m_actionKinds); }
	// How the game reads its stored calls (its format's fix-ups for old files, its checks): given a call with its name
	// filled in, it may change it in place (its name, its parameters).
	using CallHealer = std::function<void(level::ScriptCall &call)>;
	void SetConditionHealer(CallHealer healer) { m_conditionHealer = std::move(healer); }
	void SetActionHealer(CallHealer healer) { m_actionHealer = std::move(healer); }
	const CallHealer &ConditionHealer() const noexcept { return m_conditionHealer; }
	const CallHealer &ActionHealer() const noexcept { return m_actionHealer; }

private:
	static std::string_view NameOf(const level::ScriptCall &call, const std::vector<std::string> &kinds) noexcept
	{
		if (!call.name.empty())
			return call.name;
		return call.kind < kinds.size() ? std::string_view{kinds[call.kind]} : std::string_view{};
	}

	std::vector<std::string> m_conditionKinds;
	std::vector<std::string> m_actionKinds;
	CallHealer m_conditionHealer;
	CallHealer m_actionHealer;
	std::map<std::string, ConditionHandler, std::less<>> m_conditions;
	std::map<std::string, ActionHandler, std::less<>> m_actions;
};

// Game-side context the runtime asks for; all optional.
struct ScriptHooks
{
	// Difficulty a participant plays at (scripts can be disabled per level).
	std::function<Difficulty(std::size_t participant)> difficulty;
	// Subjects to evaluate a script for; empty means evaluate once.
	std::function<void(const level::Script &script, std::size_t participant, std::vector<std::uint64_t> &subjects)> subjects;
	// What a sequential script's subject is doing (a team, or whatever the game makes subjects of): gone (its sequence
	// ends, the next of its chain goes on), busy, idle (its next action may run), or dead (the whole chain ends). None:
	// every subject counts as idle.
	enum class SubjectState : std::uint8_t
	{
		Gone,
		Busy,
		Idle,
		Dead,
	};
	std::function<SubjectState(std::uint64_t subject)> sequenceSubject;
	// Each action run, before it runs (diagnostics: which script did what).
	std::function<void(const level::Script &script, const level::ScriptCall &action, std::uint64_t tick)> actionRun;
};

struct ScriptRuntimeOptions
{
	std::uint32_t ticksPerSecond{30};
	std::uint64_t seed{0};
};

// The scenario and vocabulary must outlive the runtime (it refers to their scripts and handlers).
class ScriptRuntime
{
public:
	ScriptRuntime(const level::Scenario &scenario, const Vocabulary &vocabulary, ScriptHooks hooks = {}, ScriptRuntimeOptions options = {}) :
		m_vocabulary(vocabulary), m_hooks(std::move(hooks)), m_options(options), m_random(Math::Stream(options.seed, {0x5C1297u}))
	{
		for (std::size_t participant = 0; participant < scenario.participants.size(); ++participant)
		{
			const level::ScriptList &list = scenario.participants[participant].scripts;
			for (const level::Script &script : list.scripts)
				AddScript(script, participant, std::nullopt);
			for (const level::ScriptGroup &group : list.groups)
			{
				const std::size_t groupIndex = m_groups.size();
				m_groups.push_back({&group, group.active});
				m_groupsByName.try_emplace(group.name, groupIndex);
				for (const level::Script &script : group.scripts)
					AddScript(script, participant, groupIndex);
			}
		}
	}

	ScriptRuntime(const ScriptRuntime &) = delete;
	ScriptRuntime &operator=(const ScriptRuntime &) = delete;

	// One logic tick: countdown timers, all scripts, the sequential scripts, then this tick's signals expire.
	void Tick(std::uint64_t tick)
	{
		m_tick = tick;
		for (auto &[name, counter] : m_counters)
			if (counter.countdown && counter.value >= 0)
				--counter.value;
		for (std::size_t index = 0; index < m_scripts.size(); ++index)
		{
			ScriptSlot &slot = m_scripts[index];
			if (slot.script->subroutine)
				continue;
			if (slot.group && (!m_groups[*slot.group].active || m_groups[*slot.group].group->subroutine))
				continue;
			Execute(index);
		}
		ProgressSequences();
		m_signals.clear();
	}

	// --- sequential scripts (ScriptEngine::appendSequentialScript / evaluateAndProgressAllSequentialScripts) ---
	// A script run one action at a time on a subject: each action once the subject is idle again (or after the frames an
	// action set: SetSequenceWait), `loops` more times after the first (-1: for ever). A subject already running one
	// queues this after it.
	void AppendSequence(std::string_view script, std::uint64_t subject, std::int64_t loops)
	{
		Sequence sequence{std::string(script), subject, loops};
		sequence.serial = ++m_sequenceSerial;
		for (auto &chain : m_sequences)
			if (!chain.empty() && chain.front().subject == subject)
			{
				chain.push_back(std::move(sequence));
				return;
			}
		m_sequences.push_back({std::move(sequence)});
	}
	// removeAllSequentialScripts: the subject's running sequence and those queued after it end.
	void StopSequences(std::uint64_t subject)
	{
		std::erase_if(m_sequences, [&](const auto &chain) { return !chain.empty() && chain.front().subject == subject; });
	}
	// setSequentialTimer: the subject's running sequence waits `frames` before its next action (not until idle).
	void SetSequenceWait(std::uint64_t subject, std::int64_t frames)
	{
		for (auto &chain : m_sequences)
			if (!chain.empty() && chain.front().subject == subject)
			{
				chain.front().wait = frames;
				return;
			}
	}
	// The sequence running this action does not move on to its next one (m_dontAdvanceInstruction: a wait action whose
	// condition does not hold yet runs again).
	void HoldSequence()
	{
		if (m_running != nullptr)
			m_running->hold = true;
	}
	std::size_t SequenceCount() const noexcept { return m_sequences.size(); }

	// --- variables (named, created on first use, as the original) ---
	bool Flag(std::string_view name) const
	{
		const auto found = m_flags.find(name);
		return found != m_flags.end() && found->second;
	}
	void SetFlag(std::string_view name, bool value) { m_flags[std::string(name)] = value; }
	// A UI interaction signalled this tick makes flag conditions on it true.
	void Signal(std::string_view name) { m_signals.emplace(name); }
	bool Signalled(std::string_view name) const { return m_signals.contains(name); }

	std::int64_t Counter(std::string_view name) const
	{
		const auto found = m_counters.find(name);
		return found == m_counters.end() ? 0 : found->second.value;
	}
	void SetCounter(std::string_view name, std::int64_t value) { CounterSlot(name).value = value; }
	void AddToCounter(std::string_view name, std::int64_t delta) { CounterSlot(name).value += delta; }

	// Countdown timers share the counter table and tick down once per tick.
	void StartTimer(std::string_view name, std::int64_t ticks)
	{
		auto &counter = CounterSlot(name);
		counter.value = ticks;
		counter.countdown = true;
	}
	void StopTimer(std::string_view name) { CounterSlot(name).countdown = false; }
	void RestartTimer(std::string_view name)
	{
		auto &counter = CounterSlot(name);
		if (counter.value > 0)
			counter.countdown = true;
	}
	bool TimerExpired(std::string_view name) const
	{
		const auto found = m_counters.find(name);
		return found != m_counters.end() && found->second.countdown && found->second.value < 1;
	}

	// --- scripts ---
	// Enables/disables every group and script with this name.
	void SetActive(std::string_view name, bool active)
	{
		if (const auto group = m_groupsByName.find(name); group != m_groupsByName.end())
			m_groups[group->second].active = active;
		if (const auto script = m_scriptsByName.find(name); script != m_scriptsByName.end())
			m_scripts[script->second].active = active;
	}

	bool IsActive(std::string_view name) const
	{
		if (const auto script = m_scriptsByName.find(name); script != m_scriptsByName.end())
			return m_scripts[script->second].active;
		if (const auto group = m_groupsByName.find(name); group != m_groupsByName.end())
			return m_groups[group->second].active;
		return false;
	}

	// Runs a subroutine group (if active) or subroutine script now.
	// ScriptEngine::runScript with a calling team: the subroutine (script or group) runs for `subject` alone, as do the
	// subroutines it calls in turn.
	bool CallSubroutine(std::string_view name, std::uint64_t subject)
	{
		const std::optional<std::uint64_t> saved = m_forcedSubject;
		m_forcedSubject = subject == NoSubject ? saved : std::optional<std::uint64_t>(subject);
		const bool called = CallSubroutine(name);
		m_forcedSubject = saved;
		return called;
	}

	bool CallSubroutine(std::string_view name)
	{
		if (const auto group = m_groupsByName.find(name); group != m_groupsByName.end())
		{
			const GroupSlot &slot = m_groups[group->second];
			if (!slot.group->subroutine)
				return Warn("'" + std::string(name) + "' is not a subroutine"), false;
			if (!slot.active)
				return true;
			for (std::size_t index = 0; index < m_scripts.size(); ++index)
				if (m_scripts[index].group == group->second && !m_scripts[index].script->subroutine)
					Execute(index);
			return true;
		}
		if (const auto script = m_scriptsByName.find(name); script != m_scriptsByName.end())
		{
			if (!m_scripts[script->second].script->subroutine)
				return Warn("'" + std::string(name) + "' is not a subroutine"), false;
			Execute(script->second);
			return true;
		}
		return Warn("script '" + std::string(name) + "' is not defined"), false;
	}

	// Checkpoints: the runtime's evolving state (tick, random stream, which
	// scripts and groups are active and when they next evaluate, flags,
	// counters, signals). Loads into a runtime built from the same level and
	// vocabulary; false (unchanged) when the data does not fit it.
	void SaveState(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U64(m_tick);
		writer.U64(std::bit_cast<std::uint64_t>(m_random));
		writer.U64(m_scripts.size());
		for (const ScriptSlot &slot : m_scripts)
		{
			writer.Flag(slot.active);
			writer.U64(slot.evaluateAt);
		}
		writer.U64(m_groups.size());
		for (const GroupSlot &slot : m_groups)
			writer.Flag(slot.active);
		writer.U64(m_flags.size());
		for (const auto &[name, value] : m_flags)
		{
			writer.Text(name);
			writer.Flag(value);
		}
		writer.U64(m_counters.size());
		for (const auto &[name, counter] : m_counters)
		{
			writer.Text(name);
			writer.I64(counter.value);
			writer.Flag(counter.countdown);
		}
		writer.U64(m_signals.size());
		for (const auto &name : m_signals)
			writer.Text(name);
		for (const ScriptSlot &slot : m_scripts)
			for (const auto &clause : slot.conditions)
				for (const CompiledCall &call : clause)
					writer.I64(call.memo);
		writer.U64(m_sequences.size());
		for (const auto &chain : m_sequences)
		{
			writer.U64(chain.size());
			for (const Sequence &sequence : chain)
			{
				writer.Text(sequence.script);
				writer.U64(sequence.subject);
				writer.I64(sequence.loops);
				writer.I64(sequence.instruction);
				writer.I64(sequence.wait);
				writer.Flag(sequence.hold);
			}
		}
	}

	bool LoadState(engine::core::serialization::ByteReader &reader)
	{
		const auto tick = reader.U64();
		const auto random = reader.U64();
		const auto scripts = reader.U64();
		if (!tick || !random || scripts != m_scripts.size())
			return false;
		std::vector<std::pair<bool, std::uint64_t>> scriptStates;
		for (std::size_t index = 0; index < m_scripts.size(); ++index)
		{
			const auto active = reader.Flag();
			const auto evaluateAt = reader.U64();
			if (!active || !evaluateAt)
				return false;
			scriptStates.emplace_back(*active, *evaluateAt);
		}
		const auto groups = reader.U64();
		if (groups != m_groups.size())
			return false;
		std::vector<bool> groupStates;
		for (std::size_t index = 0; index < m_groups.size(); ++index)
			groupStates.push_back(reader.Flag().value_or(false));
		std::map<std::string, bool, std::less<>> flags;
		const auto flagCount = reader.U64();
		for (std::uint64_t index = 0; flagCount && index < *flagCount && !reader.Failed(); ++index)
		{
			std::string name = reader.Text().value_or("");
			flags[std::move(name)] = reader.Flag().value_or(false);
		}
		std::map<std::string, CounterValue, std::less<>> counters;
		const auto counterCount = reader.U64();
		for (std::uint64_t index = 0; counterCount && index < *counterCount && !reader.Failed(); ++index)
		{
			std::string name = reader.Text().value_or("");
			const std::int64_t value = reader.I64().value_or(0);
			counters[std::move(name)] = {value, reader.Flag().value_or(false)};
		}
		std::set<std::string, std::less<>> signals;
		const auto signalCount = reader.U64();
		for (std::uint64_t index = 0; signalCount && index < *signalCount && !reader.Failed(); ++index)
			signals.insert(reader.Text().value_or(""));
		std::vector<std::int64_t> memos;
		for (const ScriptSlot &slot : m_scripts)
			for (const auto &clause : slot.conditions)
				for (std::size_t call = 0; call < clause.size(); ++call)
					memos.push_back(reader.I64().value_or(0));
		std::vector<std::deque<Sequence>> sequences;
		const auto chainCount = reader.U64();
		for (std::uint64_t chain = 0; chainCount && chain < *chainCount && !reader.Failed(); ++chain)
		{
			auto &loaded = sequences.emplace_back();
			const std::uint64_t length = reader.U64().value_or(0);
			for (std::uint64_t index = 0; index < length && !reader.Failed(); ++index)
			{
				Sequence sequence;
				sequence.script = reader.Text().value_or("");
				sequence.subject = reader.U64().value_or(0);
				sequence.loops = reader.I64().value_or(0);
				sequence.instruction = reader.I64().value_or(-1);
				sequence.wait = reader.I64().value_or(-1);
				sequence.hold = reader.Flag().value_or(false);
				sequence.serial = ++m_sequenceSerial;
				loaded.push_back(std::move(sequence));
			}
		}
		if (reader.Failed() || !flagCount || !counterCount || !signalCount || !chainCount)
			return false;
		m_sequences = std::move(sequences);
		std::size_t memo = 0;
		for (const ScriptSlot &slot : m_scripts)
			for (const auto &clause : slot.conditions)
				for (const CompiledCall &call : clause)
					call.memo = memos[memo++];
		m_tick = *tick;
		m_random = std::bit_cast<Math::RandomStream>(*random);
		for (std::size_t index = 0; index < m_scripts.size(); ++index)
			std::tie(m_scripts[index].active, m_scripts[index].evaluateAt) = scriptStates[index];
		for (std::size_t index = 0; index < m_groups.size(); ++index)
			m_groups[index].active = groupStates[index];
		m_flags = std::move(flags);
		m_counters = std::move(counters);
		m_signals = std::move(signals);
		return true;
	}

	// A script looked at by name for `subject` rather than run (the original's evaluateConditions for a team's
	// production condition): false when it does not exist, is not for its participant's difficulty, or waits out its
	// evaluation delay (which a look restarts); else whether its conditions hold. Active or not, it may be looked at.
	bool EvaluateNamed(std::string_view name, std::uint64_t subject = NoSubject)
	{
		const auto found = m_scriptsByName.find(name);
		if (found == m_scriptsByName.end())
			return false;
		ScriptSlot &slot = m_scripts[found->second];
		const Difficulty difficulty = m_hooks.difficulty ? m_hooks.difficulty(slot.participant) : Difficulty::Normal;
		if (!slot.script->difficulty[static_cast<std::size_t>(difficulty)])
			return false;
		if (m_tick < slot.evaluateAt)
			return false;
		if (slot.script->evaluationDelaySeconds > 0)
			slot.evaluateAt = m_tick + std::uint64_t{slot.script->evaluationDelaySeconds} * m_options.ticksPerSecond;
		return Conditions(slot, subject);
	}

	// Whether a script of that name exists and is for its participant's difficulty.
	bool AllowedAtDifficulty(std::string_view name) const
	{
		const auto found = m_scriptsByName.find(name);
		if (found == m_scriptsByName.end())
			return false;
		const ScriptSlot &slot = m_scripts[found->second];
		const Difficulty difficulty = m_hooks.difficulty ? m_hooks.difficulty(slot.participant) : Difficulty::Normal;
		return slot.script->difficulty[static_cast<std::size_t>(difficulty)];
	}

	// A script's conditions looked at by name for `subject`, whatever its delay (a caller keeping its own copy's timing:
	// a team prototype's production condition).
	bool ConditionsHold(std::string_view name, std::uint64_t subject = NoSubject)
	{
		const auto found = m_scriptsByName.find(name);
		return found != m_scriptsByName.end() && Conditions(m_scripts[found->second], subject);
	}

	// A script's actions run by name for `subject` (friend_executeAction: a team's start actions, its on-create script).
	bool RunNamedActions(std::string_view name, std::uint64_t subject = NoSubject)
	{
		const auto found = m_scriptsByName.find(name);
		if (found == m_scriptsByName.end())
			return false;
		const ScriptSlot &slot = m_scripts[found->second];
		Run(slot.actions, slot, subject);
		return true;
	}

	bool HasScript(std::string_view name) const { return m_scriptsByName.contains(name); }
	const level::Script *FindScript(std::string_view name) const
	{
		const auto found = m_scriptsByName.find(name);
		return found == m_scriptsByName.end() ? nullptr : m_scripts[found->second].script;
	}

	std::uint64_t CurrentTick() const noexcept { return m_tick; }
	std::uint32_t TicksPerSecond() const noexcept { return m_options.ticksPerSecond; }
	Math::RandomStream &Random() noexcept { return m_random; }

	// Call names the vocabulary does not implement (reported once, at load).
	const std::set<std::string, std::less<>> &UnknownConditions() const noexcept { return m_unknownConditions; }
	const std::set<std::string, std::less<>> &UnknownActions() const noexcept { return m_unknownActions; }
	const std::vector<std::string> &Warnings() const noexcept { return m_warnings; }
	std::size_t ScriptCount() const noexcept { return m_scripts.size(); }

	// Diagnostics (off by default): where the scripts' time goes, by condition and action name and for the subject
	// lists, in summed nanoseconds and calls.
	struct CallTiming
	{
		std::string name;
		std::uint64_t nanos{0};
		std::uint64_t calls{0};
	};
	void EnableProfiling(bool enabled)
	{
		m_profiling = enabled;
		m_timings.clear();
	}
	std::vector<CallTiming> Profile() const
	{
		std::vector<CallTiming> out;
		for (const auto &[name, timing] : m_timings)
			out.push_back({name, timing.first, timing.second});
		return out;
	}

private:
	template<typename Call>
	auto Timed(std::string_view name, Call &&call)
	{
		if (!m_profiling)
			return call();
		const auto begin = std::chrono::steady_clock::now();
		auto result = call();
		auto &timing = m_timings[std::string(name)];
		timing.first += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count());
		++timing.second;
		return result;
	}
	bool m_profiling{false};
	std::map<std::string, std::pair<std::uint64_t, std::uint64_t>, std::less<>> m_timings;

	struct CompiledCall
	{
		const level::ScriptCall *call;
		const ConditionHandler *condition{nullptr};
		const ActionHandler *action{nullptr};
		mutable std::int64_t memo{0};
	};

	struct ScriptSlot
	{
		const level::Script *script;
		std::size_t participant;
		std::optional<std::size_t> group;
		bool active;
		std::uint64_t evaluateAt{0};
		std::vector<std::vector<CompiledCall>> conditions;
		std::vector<CompiledCall> actions;
		std::vector<CompiledCall> falseActions;
	};

	struct GroupSlot
	{
		const level::ScriptGroup *group;
		bool active;
	};

	struct CounterValue
	{
		std::int64_t value{0};
		bool countdown{false};
	};

	// The call as the game reads it: named, then healed; the stored one when that changes nothing, else a copy kept
	// for the runtime's life.
	const level::ScriptCall &Healed(const level::ScriptCall &call, std::string_view name, const Vocabulary::CallHealer &healer)
	{
		if (name == call.name && !healer)
			return call;
		level::ScriptCall read = call;
		read.name = std::string(name);
		if (healer)
			healer(read);
		if (read.name == call.name && read.kind == call.kind && SameParameters(read.parameters, call.parameters))
			return call;
		return m_healedCalls.emplace_back(std::move(read));
	}

	static bool SameParameters(const std::vector<level::ScriptParameter> &a, const std::vector<level::ScriptParameter> &b) noexcept
	{
		if (a.size() != b.size())
			return false;
		for (std::size_t index = 0; index < a.size(); ++index)
			if (a[index].kind != b[index].kind || a[index].integer != b[index].integer || a[index].number != b[index].number ||
				a[index].text != b[index].text || a[index].position != b[index].position)
				return false;
		return true;
	}

	void AddScript(const level::Script &script, std::size_t participant, std::optional<std::size_t> group)
	{
		ScriptSlot slot{&script, participant, group, script.active};
		// Delayed scripts start at a random tick in the first two seconds so
		// they do not all evaluate on the same tick.
		if (script.evaluationDelaySeconds > 0)
			slot.evaluateAt = static_cast<std::uint64_t>(Math::UniformInt(m_random, 0, 2 * static_cast<std::int64_t>(m_options.ticksPerSecond)));
		for (const auto &clause : script.conditions)
		{
			auto &compiled = slot.conditions.emplace_back();
			for (const level::ScriptCall &call : clause)
			{
				const level::ScriptCall &read = Healed(call, m_vocabulary.ConditionName(call), m_vocabulary.ConditionHealer());
				const ConditionHandler *handler = m_vocabulary.FindCondition(read.name);
				if (handler == nullptr)
					m_unknownConditions.insert(read.name);
				compiled.push_back({&read, handler, nullptr});
			}
		}
		const auto compileActions = [&](const std::vector<level::ScriptCall> &calls, std::vector<CompiledCall> &out) {
			for (const level::ScriptCall &call : calls)
			{
				const level::ScriptCall &read = Healed(call, m_vocabulary.ActionName(call), m_vocabulary.ActionHealer());
				const ActionHandler *handler = m_vocabulary.FindAction(read.name);
				if (handler == nullptr)
					m_unknownActions.insert(read.name);
				out.push_back({&read, nullptr, handler});
			}
		};
		compileActions(script.actions, slot.actions);
		compileActions(script.falseActions, slot.falseActions);
		m_scriptsByName.try_emplace(script.name, m_scripts.size());
		m_scripts.push_back(std::move(slot));
	}

	void Execute(std::size_t index)
	{
		ScriptSlot &slot = m_scripts[index];
		if (!slot.active)
			return;
		const Difficulty difficulty = m_hooks.difficulty ? m_hooks.difficulty(slot.participant) : Difficulty::Normal;
		if (!slot.script->difficulty[static_cast<std::size_t>(difficulty)])
			return;
		if (m_tick < slot.evaluateAt)
			return;
		if (slot.script->evaluationDelaySeconds > 0)
			slot.evaluateAt = m_tick + std::uint64_t{slot.script->evaluationDelaySeconds} * m_options.ticksPerSecond;

		std::vector<std::uint64_t> subjects;
		if (m_forcedSubject)
			subjects.push_back(*m_forcedSubject);
		else if (m_hooks.subjects)
			Timed("<subjects>", [&] {
				m_hooks.subjects(*slot.script, slot.participant, subjects);
				return true;
			});
		if (!subjects.empty())
		{
			// Evaluated per subject; a one-shot deactivates but the remaining
			// subjects of this tick still run, as in the original.
			for (const std::uint64_t subject : subjects)
			{
				if (Conditions(slot, subject))
				{
					Run(slot.actions, slot, subject);
					if (slot.script->oneShot)
						slot.active = false;
				}
				else
				{
					Run(slot.falseActions, slot, subject);
				}
			}
			return;
		}
		if (Conditions(slot, NoSubject))
		{
			Run(slot.actions, slot, NoSubject);
			if (slot.script->oneShot)
				slot.active = false;
		}
		else if (!slot.falseActions.empty())
		{
			Run(slot.falseActions, slot, NoSubject);
			if (slot.script->oneShot)
				slot.active = false;
		}
	}

	bool Conditions(const ScriptSlot &slot, std::uint64_t subject)
	{
		for (const auto &clause : slot.conditions)
		{
			if (clause.empty())
				continue;
			bool all = true;
			for (const CompiledCall &call : clause)
			{
				ScriptCallContext context{*this, slot.participant, subject, *call.call, m_tick, &call.memo};
				if (call.condition == nullptr || !Timed(call.call->name, [&] { return (*call.condition)(context); }))
				{
					all = false;
					break;
				}
			}
			if (all)
				return true;
		}
		return false;
	}

	void Run(const std::vector<CompiledCall> &calls, const ScriptSlot &slot, std::uint64_t subject)
	{
		for (const CompiledCall &call : calls)
		{
			if (call.action == nullptr)
				continue;
			ScriptCallContext context{*this, slot.participant, subject, *call.call, m_tick, &call.memo};
			if (m_hooks.actionRun && slot.script != nullptr)
				m_hooks.actionRun(*slot.script, *call.call, m_tick);
			Timed(call.call->name, [&] {
				(*call.action)(context);
				return true;
			});
		}
	}

	CounterValue &CounterSlot(std::string_view name)
	{
		const auto found = m_counters.find(name);
		if (found != m_counters.end())
			return found->second;
		return m_counters[std::string(name)];
	}

	void Warn(std::string message) { m_warnings.push_back(std::move(message)); }

	struct Sequence
	{
		std::string script;
		std::uint64_t subject{NoSubject};
		std::int64_t loops{0};
		std::int64_t instruction{-1}; // START_INSTRUCTION
		std::int64_t wait{-1};        // m_framesToWait: 0 go on, below 0 once idle, above 0 count down
		bool hold{false};             // m_dontAdvanceInstruction
		std::uint64_t serial{0};      // which one it is, while this tick runs (not saved)
	};

	// evaluateAndProgressAllSequentialScripts: each chain's running sequence, in order. Once its wait is over (or it
	// waits for idleness and its subject is idle) it runs its next action (the same one again when held), then waits for
	// idleness again unless the action set a wait; a subject that is idle straight away goes on in the same tick (up to
	// the original's MAX_SPIN_COUNT = 20 turns). Past its last action it is queued again while it has loops left, and
	// the next of its chain takes over. A gone subject's sequence ends; a dead one's whole chain.
	void ProgressSequences()
	{
		using State = ScriptHooks::SubjectState;
		const auto stateOf = [&](std::uint64_t subject) { return m_hooks.sequenceSubject ? m_hooks.sequenceSubject(subject) : State::Idle; };
		constexpr int MaxSpins = 20;
		int spins = 0;
		std::size_t last = ~std::size_t{0};
		for (std::size_t index = 0; index < m_sequences.size();)
		{
			spins = index == last ? spins + 1 : 0;
			last = index;
			if (spins > MaxSpins || m_sequences[index].empty())
			{
				if (m_sequences[index].empty())
					m_sequences.erase(m_sequences.begin() + static_cast<std::ptrdiff_t>(index));
				else
					++index;
				continue;
			}
			auto &chain = m_sequences[index];
			Sequence &sequence = chain.front();
			const State state = stateOf(sequence.subject);
			const auto endRunning = [&] {
				chain.erase(chain.begin());
				if (chain.empty())
					m_sequences.erase(m_sequences.begin() + static_cast<std::ptrdiff_t>(index));
			};
			if (state == State::Gone)
			{
				endRunning();
				continue;
			}
			if (sequence.wait > 0)
			{
				--sequence.wait;
				++index;
				continue;
			}
			// AIGroup::isIdle counts the dead as idle: a dead subject runs its next action (and then its chain ends).
			if (sequence.wait < 0 && state != State::Idle && state != State::Dead)
			{
				++index;
				continue;
			}
			if (sequence.hold)
				sequence.hold = false;
			else
				++sequence.instruction;
			const auto found = m_scriptsByName.find(sequence.script);
			const ScriptSlot *slot = found != m_scriptsByName.end() ? &m_scripts[found->second] : nullptr;
			if (slot != nullptr && sequence.instruction >= 0 && static_cast<std::size_t>(sequence.instruction) < slot->actions.size())
			{
				sequence.wait = -1;
				const CompiledCall &call = slot->actions[static_cast<std::size_t>(sequence.instruction)];
				const std::uint64_t serial = sequence.serial;
				m_running = &sequence;
				if (call.action != nullptr)
				{
					ScriptCallContext context{*this, slot->participant, sequence.subject, *call.call, m_tick, &call.memo};
					(*call.action)(context);
				}
				m_running = nullptr;
				// The action may have ended its own sequences (m_sequences changed): look again from here.
				if (index >= m_sequences.size() || m_sequences[index].empty() || m_sequences[index].front().serial != serial)
					continue;
				if (sequence.hold)
				{
					++index;
					continue;
				}
				const State after = stateOf(sequence.subject);
				if (after == State::Dead)
				{
					m_sequences.erase(m_sequences.begin() + static_cast<std::ptrdiff_t>(index));
					continue;
				}
				if (after != State::Idle || sequence.wait != -1)
					++index;
				continue;
			}
			// Past its last action: again while it has loops left, after the rest of its chain.
			if (sequence.loops != 0)
			{
				Sequence again{sequence.script, sequence.subject, sequence.loops == -1 ? -1 : sequence.loops - 1};
				again.serial = ++m_sequenceSerial;
				chain.push_back(std::move(again));
			}
			endRunning();
		}
	}

	const Vocabulary &m_vocabulary;
	ScriptHooks m_hooks;
	ScriptRuntimeOptions m_options;
	Math::RandomStream m_random;
	std::vector<ScriptSlot> m_scripts;
	std::vector<GroupSlot> m_groups;
	std::map<std::string, std::size_t, std::less<>> m_scriptsByName;
	std::optional<std::uint64_t> m_forcedSubject; // a subroutine's calling subject, while it runs
	std::map<std::string, std::size_t, std::less<>> m_groupsByName;
	std::map<std::string, bool, std::less<>> m_flags;
	std::map<std::string, CounterValue, std::less<>> m_counters;
	std::set<std::string, std::less<>> m_signals;
	std::set<std::string, std::less<>> m_unknownConditions;
	std::set<std::string, std::less<>> m_unknownActions;
	std::deque<level::ScriptCall> m_healedCalls; // calls the game's reading changed (stable addresses)
	std::vector<std::string> m_warnings;
	std::uint64_t m_tick{0};
	std::vector<std::deque<Sequence>> m_sequences; // chains of sequential scripts, each's front running (a deque: its
	                                               // members stay put as others join)
	std::uint64_t m_sequenceSerial{0};
	Sequence *m_running{nullptr};                   // the sequence whose action runs now
};
}
