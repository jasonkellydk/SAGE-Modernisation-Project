export module games.generalszh.session.setup.script_qualify;
import std;

export import engine.level.model.level;

// A skirmish side's scripts made a player's own (ScriptList / ScriptGroup / Script / Parameter::duplicateAndQualify):
// every script's and group's name, and every team, script, counter, flag and subroutine named in a call, gets the
// qualifier (the player's start index) appended, so each computer player keeps its own; a side named in a call that
// is the side's qualified name becomes the player's name. "<This Team>" stays as it is.
export namespace generalszh::session::setup
{
namespace script_parameter
{
// Parameter::ParameterType.
inline constexpr std::uint32_t Script = 2;
inline constexpr std::uint32_t Team = 3;
inline constexpr std::uint32_t Counter = 4;
inline constexpr std::uint32_t Flag = 5;
inline constexpr std::uint32_t Side = 11;
inline constexpr std::uint32_t ScriptSubroutine = 13;
}

inline void QualifyCall(engine::level::ScriptCall &call, const std::string &qualifier, const std::string &templatePlayer, const std::string &player)
{
	namespace kind = script_parameter;
	for (engine::level::ScriptParameter &parameter : call.parameters)
		switch (parameter.kind)
		{
		case kind::Side:
			if (parameter.text + qualifier == templatePlayer)
				parameter.text = player;
			break;
		case kind::Team:
			if (parameter.text == "<This Team>")
				break;
			[[fallthrough]];
		case kind::Script:
		case kind::Counter:
		case kind::Flag:
		case kind::ScriptSubroutine: parameter.text += qualifier; break;
		default: break;
		}
}

inline engine::level::Script QualifyScript(engine::level::Script script, const std::string &qualifier, const std::string &templatePlayer, const std::string &player)
{
	script.name += qualifier;
	for (auto &clause : script.conditions)
		for (auto &call : clause)
			QualifyCall(call, qualifier, templatePlayer, player);
	for (auto &call : script.actions)
		QualifyCall(call, qualifier, templatePlayer, player);
	for (auto &call : script.falseActions)
		QualifyCall(call, qualifier, templatePlayer, player);
	return script;
}

inline engine::level::ScriptList QualifyScripts(const engine::level::ScriptList &list, const std::string &qualifier, const std::string &templatePlayer,
	const std::string &player)
{
	engine::level::ScriptList out;
	for (const auto &script : list.scripts)
		out.scripts.push_back(QualifyScript(script, qualifier, templatePlayer, player));
	for (const auto &group : list.groups)
	{
		engine::level::ScriptGroup copy;
		copy.name = group.name + qualifier;
		copy.active = group.active;
		copy.subroutine = group.subroutine;
		for (const auto &script : group.scripts)
			copy.scripts.push_back(QualifyScript(script, qualifier, templatePlayer, player));
		out.groups.push_back(std::move(copy));
	}
	return out;
}

// A skirmish side's team made the player's (Player::initFromDict): its name and the scripts it names (on create, on
// idle, on a unit destroyed, on destroyed, enemy sighted, all clear, production condition, the generic hooks 0..15)
// get the qualifier; the player owns it.
inline engine::level::Properties QualifyTeam(engine::level::Properties team, const std::string &qualifier, const std::string &player)
{
	team.Set("teamName", team.Get<std::string>("teamName").value_or("") + qualifier);
	team.Set("teamOwner", player);
	std::vector<std::string> keys{"teamOnCreateScript", "teamOnIdleScript", "teamOnUnitDestroyedScript", "teamOnDestroyedScript", "teamEnemySightedScript",
		"teamAllClearScript", "teamProductionCondition"};
	for (int hook = 0; hook < 16; ++hook)
		keys.push_back("teamGenericScriptHook" + std::to_string(hook));
	for (const std::string &key : keys)
		if (const auto value = team.Get<std::string>(key); value && !value->empty())
			team.Set(key, *value + qualifier);
	return team;
}
}
