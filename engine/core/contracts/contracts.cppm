export module engine.core.contracts;
import std;

// Checks that hold in debug builds (what `assert` did, as a function: no
// macros cross module boundaries). Release builds (NDEBUG) check nothing,
// as assert; the condition is still evaluated, so keep it free of effects.
export namespace engine::core
{
inline void Assert(bool condition, std::source_location where = std::source_location::current()) noexcept
{
#ifndef NDEBUG
	if (!condition)
	{
		std::cerr << where.file_name() << ':' << where.line() << ": assertion failed in " << where.function_name() << '\n';
		std::abort();
	}
#else
	(void)condition;
	(void)where;
#endif
}
}
