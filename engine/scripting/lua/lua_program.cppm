module;
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
export module engine.scripting.lua.lua_program;
import std;

export namespace engine::scripting::lua
{
using Value = std::variant<std::int64_t, std::string>;
struct Command { std::string name; std::vector<Value> arguments; };

// A private Lua VM for a script instance; persistent state is its returned
// table. The host dispatches events and consumes commands only after success.
// No OS, file, module-loading, native DLL or random libraries are installed.
class Program
{
public:
	Program() : m_state(luaL_newstate())
	{
		if (!m_state) throw std::bad_alloc();
		lua_pushlightuserdata(m_state, this);
		lua_pushcclosure(m_state, &Emit, 1);
		lua_setglobal(m_state, "emit");
		*static_cast<Program **>(lua_getextraspace(m_state)) = this;
		lua_sethook(m_state, &Budget, LUA_MASKCOUNT, 1000);
	}
	~Program() { lua_close(m_state); }
	Program(const Program &) = delete;
	Program &operator=(const Program &) = delete;
	std::expected<void, std::string> Load(std::string_view source, std::string_view name)
	{
		lua_settop(m_state, 0);
		m_commands.clear();
		m_budget = 100;
		const std::string label(name);
		if (luaL_loadbufferx(m_state, source.data(), source.size(), label.c_str(), "t") != LUA_OK ||
			lua_pcall(m_state, 0, 1, 0) != LUA_OK) return Error();
		if (!lua_istable(m_state, -1)) { lua_settop(m_state, 0); return std::unexpected("script must return a callback table"); }
		if (m_script != LUA_NOREF) luaL_unref(m_state, LUA_REGISTRYINDEX, m_script);
		m_script = luaL_ref(m_state, LUA_REGISTRYINDEX);
		m_commands.clear();
		return {};
	}
	std::expected<std::vector<Command>, std::string> Invoke(std::string_view event, std::span<const Value> arguments = {})
	{
		if (m_script == LUA_NOREF) return std::unexpected("script is not loaded");
		lua_settop(m_state, 0);
		m_commands.clear();
		m_budget = 100;
		lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_script);
		const std::string key(event);
		lua_getfield(m_state, -1, key.c_str());
		if (lua_isnil(m_state, -1)) { lua_settop(m_state, 0); return std::vector<Command>{}; }
		lua_pushvalue(m_state, -2); // self
		for (const auto &argument : arguments)
			std::visit([&](const auto &value) {
				using T = std::decay_t<decltype(value)>;
				if constexpr (std::is_same_v<T, std::int64_t>) lua_pushinteger(m_state, value);
				else lua_pushlstring(m_state, value.data(), value.size());
			}, argument);
		if (lua_pcall(m_state, static_cast<int>(arguments.size()) + 1, 0, 0) != LUA_OK)
		{
			const auto error = Error();
			m_commands.clear();
			return std::unexpected(error.error());
		}
		lua_settop(m_state, 0);
		return std::move(m_commands);
	}
private:
	std::expected<void, std::string> Error()
	{
		const char *message = lua_tostring(m_state, -1);
		std::string error = message ? message : "Lua callback failed";
		lua_settop(m_state, 0);
		return std::unexpected(std::move(error));
	}
	static void Budget(lua_State *state, lua_Debug *)
	{
		auto *self = *static_cast<Program **>(lua_getextraspace(state));
		if (--self->m_budget <= 0) luaL_error(state, "script instruction budget exceeded");
	}
	static int Emit(lua_State *state)
	{
		auto *self = static_cast<Program *>(lua_touserdata(state, lua_upvalueindex(1)));
		// Validate before constructing C++ objects: luaL_error uses longjmp.
		if (lua_type(state, 1) != LUA_TSTRING || self->m_commands.size() >= 1024)
			return luaL_error(state, "invalid command or command budget exceeded");
		for (int i = 2; i <= lua_gettop(state); ++i)
			if (!lua_isinteger(state, i) && lua_type(state, i) != LUA_TSTRING)
				return luaL_error(state, "command arguments must be integers or strings");
		bool failed = false;
		try
		{
			std::size_t size = 0;
			const char *name = lua_tolstring(state, 1, &size);
			Command command{std::string(name, size), {}};
			for (int i = 2; i <= lua_gettop(state); ++i)
				if (lua_isinteger(state, i)) command.arguments.emplace_back(static_cast<std::int64_t>(lua_tointeger(state, i)));
				else
				{
					const char *text = lua_tolstring(state, i, &size);
					command.arguments.emplace_back(std::string(text, size));
				}
			self->m_commands.push_back(std::move(command));
		}
		catch (...) { failed = true; }
		if (failed) return luaL_error(state, "cannot allocate command");
		return 0;
	}
	lua_State *m_state;
	int m_script{LUA_NOREF};
	int m_budget{100};
	std::vector<Command> m_commands;
};
}
