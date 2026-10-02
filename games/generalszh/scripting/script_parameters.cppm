export module games.generalszh.scripting.script_parameters;
import std;

export import engine.scripting.runtime.script_runtime;

// Typed access to a script call's parameters (missing ones read as empty/zero).
export namespace generalszh::scripting::parameters
{
using engine::scripting::ScriptCallContext;

inline const std::string &Text(const ScriptCallContext &context, std::size_t index)
{
	static const std::string empty;
	return index < context.call.parameters.size() ? context.call.parameters[index].text : empty;
}

inline std::int64_t Integer(const ScriptCallContext &context, std::size_t index)
{
	return index < context.call.parameters.size() ? context.call.parameters[index].integer : 0;
}

inline Engine::Math::Fixed Number(const ScriptCallContext &context, std::size_t index)
{
	return index < context.call.parameters.size() ? context.call.parameters[index].number : Engine::Math::Fixed{};
}

// A position parameter (COORD3D).
inline Engine::Math::FixedVector3 Position(const ScriptCallContext &context, std::size_t index)
{
	return index < context.call.parameters.size() ? context.call.parameters[index].position : Engine::Math::FixedVector3{};
}

// Seconds parameter -> whole milliseconds.
inline std::int64_t Milliseconds(const ScriptCallContext &context, std::size_t index)
{
	return (Number(context, index) * 1000).Round();
}
}
