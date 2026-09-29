export module games.generalszh.gameplay.teams.resources.team_templates;
import std;

export import engine.level.model.level;
import engine.ecs.system.system;

// What the level authored for each team (unit types and counts,
// reinforcement origin, ...), by TeamRoster index of the team (an instance: of the team it is one of).
export namespace generalszh::gameplay
{
class TeamTemplates
{
public:
	void Add(const engine::level::Properties *properties) { m_properties.push_back(properties != nullptr ? properties : &m_none); }
	const engine::level::Properties &At(std::uint32_t team) const { return team < m_properties.size() ? *m_properties[team] : m_none; }

private:
	std::vector<const engine::level::Properties *> m_properties;
	engine::level::Properties m_none;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::TeamTemplates>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.team_templates";
};
}
