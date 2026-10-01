export module games.generalszh.gameplay.combat.components.enemy_near;
import std;

export import engine.ecs.core.component_registry;

// Something that shows when enemies are about (the original's EnemyNearUpdate: the GLA burning barrier's ENEMYNEAR
// look): the ticks until its next look (m_enemyScanDelay: a random share of ScanDelayTime at first) and whether an enemy
// was near at its last look (m_enemyNear). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct EnemyNear
{
	std::uint32_t scanDelay{0};
	std::uint32_t near{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::EnemyNear>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.enemy_near";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
