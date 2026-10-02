export module games.generalszh.scripting.core_vocabulary;
import std;

export import engine.scripting.runtime.script_runtime;
import games.generalszh.scripting.script_parameters;

// The Zero Hour names and parameter layouts of the generic script
// operations (flags, counters, timers, script enabling, subroutines),
// mapped onto engine::scripting. Game-specific vocabularies (camera, teams,
// units, audio, ...) register alongside, each in its own module.
export namespace generalszh::scripting
{
namespace detail
{
using engine::scripting::ScriptCallContext;
using parameters::Integer;
using parameters::Number;
using parameters::Text;

// Seconds -> ticks, rounded up (the original's "millisecond" timers take seconds).
inline std::int64_t SecondsToTicks(const ScriptCallContext &context, Engine::Math::Fixed seconds)
{
	const Engine::Math::Fixed ticks = seconds * static_cast<std::int64_t>(context.runtime.TicksPerSecond());
	return ticks.Ceil();
}

// Comparison codes stored in counter conditions.
inline bool Compare(std::int64_t value, std::int64_t code, std::int64_t reference)
{
	switch (code)
	{
	case 0: return value < reference;
	case 1: return value <= reference;
	case 2: return value == reference;
	case 3: return value >= reference;
	case 4: return value > reference;
	case 5: return value != reference;
	default: return false;
	}
}
}

inline void AddCoreVocabulary(engine::scripting::Vocabulary &vocabulary)
{
	using engine::scripting::ScriptCallContext;
	using namespace detail;
	vocabulary.AddCondition("CONDITION_TRUE", [](ScriptCallContext &) { return true; });
	vocabulary.AddCondition("CONDITION_FALSE", [](ScriptCallContext &) { return false; });
	// FLAG(name, value): the flag equals value, or a UI interaction of that name happened this tick.
	vocabulary.AddCondition("FLAG", [](ScriptCallContext &c) {
		return c.runtime.Flag(Text(c, 0)) == (Integer(c, 1) != 0) || c.runtime.Signalled(Text(c, 0));
	});
	// COUNTER(name, comparison, value)
	vocabulary.AddCondition("COUNTER", [](ScriptCallContext &c) { return Compare(c.runtime.Counter(Text(c, 0)), Integer(c, 1), Integer(c, 2)); });
	vocabulary.AddCondition("TIMER_EXPIRED", [](ScriptCallContext &c) { return c.runtime.TimerExpired(Text(c, 0)); });

	vocabulary.AddAction("NO_OP", [](ScriptCallContext &) {});
	// DEBUG_STRING / DEBUG_MESSAGE_BOX (ScriptEngine::AppendDebugMessage: only into the script debugger DLL, which the game
	// does not ship) and DEBUG_CRASH_BOX (debug builds only): nothing in the game itself.
	for (const char *debug : {"DEBUG_STRING", "DEBUG_MESSAGE_BOX", "DEBUG_CRASH_BOX"})
		vocabulary.AddAction(debug, [](ScriptCallContext &) {});
	vocabulary.AddAction("SET_FLAG", [](ScriptCallContext &c) { c.runtime.SetFlag(Text(c, 0), Integer(c, 1) != 0); });
	vocabulary.AddAction("SET_COUNTER", [](ScriptCallContext &c) { c.runtime.SetCounter(Text(c, 0), Integer(c, 1)); });
	// INCREMENT/DECREMENT_COUNTER(amount, name)
	vocabulary.AddAction("INCREMENT_COUNTER", [](ScriptCallContext &c) { c.runtime.AddToCounter(Text(c, 1), Integer(c, 0)); });
	vocabulary.AddAction("DECREMENT_COUNTER", [](ScriptCallContext &c) { c.runtime.AddToCounter(Text(c, 1), -Integer(c, 0)); });
	// Timers: SET_TIMER(name, ticks), SET_MILLISECOND_TIMER(name, seconds), random variants add a maximum.
	vocabulary.AddAction("SET_TIMER", [](ScriptCallContext &c) { c.runtime.StartTimer(Text(c, 0), Integer(c, 1)); });
	vocabulary.AddAction("SET_MILLISECOND_TIMER", [](ScriptCallContext &c) { c.runtime.StartTimer(Text(c, 0), SecondsToTicks(c, Number(c, 1))); });
	vocabulary.AddAction("SET_RANDOM_TIMER", [](ScriptCallContext &c) {
		c.runtime.StartTimer(Text(c, 0), Engine::Math::UniformInt(c.runtime.Random(), Integer(c, 1), Integer(c, 2)));
	});
	vocabulary.AddAction("SET_RANDOM_MSEC_TIMER", [](ScriptCallContext &c) {
		const auto seconds = Engine::Math::UniformFixed(c.runtime.Random(), Number(c, 1), Number(c, 2));
		c.runtime.StartTimer(Text(c, 0), SecondsToTicks(c, seconds));
	});
	vocabulary.AddAction("STOP_TIMER", [](ScriptCallContext &c) { c.runtime.StopTimer(Text(c, 0)); });
	vocabulary.AddAction("RESTART_TIMER", [](ScriptCallContext &c) { c.runtime.RestartTimer(Text(c, 0)); });
	// ADD_TO/SUB_FROM_MSEC_TIMER(seconds, name)
	vocabulary.AddAction("ADD_TO_MSEC_TIMER", [](ScriptCallContext &c) { c.runtime.AddToCounter(Text(c, 1), SecondsToTicks(c, Number(c, 0))); });
	vocabulary.AddAction("SUB_FROM_MSEC_TIMER", [](ScriptCallContext &c) { c.runtime.AddToCounter(Text(c, 1), -SecondsToTicks(c, Number(c, 0))); });
	vocabulary.AddAction("ENABLE_SCRIPT", [](ScriptCallContext &c) { c.runtime.SetActive(Text(c, 0), true); });
	vocabulary.AddAction("DISABLE_SCRIPT", [](ScriptCallContext &c) { c.runtime.SetActive(Text(c, 0), false); });
	vocabulary.AddAction("CALL_SUBROUTINE", [](ScriptCallContext &c) { c.runtime.CallSubroutine(Text(c, 0), c.subject); });
}
}
