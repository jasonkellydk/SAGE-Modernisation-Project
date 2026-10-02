export module games.generalszh.session.composition.world;
import std;
import games.generalszh.gameplay.world.resources.match_rules;
import engine.gameplay.common.random.resources.random_seed;
import games.generalszh.gameplay.world.resources.map_scenery;
import games.generalszh.gameplay.world.resources.music_progress;
import games.generalszh.gameplay.world.resources.chat_inbox;
import games.generalszh.gameplay.orders.resources.hotkey_squads;
import games.generalszh.gameplay.world.resources.water_changes;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
import games.generalszh.gameplay.world.components.difficulty_bonus;

// The world domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The world domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceWorldResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<generalszh::gameplay::WaterChanges>();
	world.EmplaceResource<generalszh::gameplay::MusicProgress>();
	world.EmplaceResource<generalszh::gameplay::ChatInbox>(); // chat lines through the command stream, for the host
	world.EmplaceResource<generalszh::gameplay::HotkeySquads>();
	world.EmplaceResource<generalszh::gameplay::MapSceneryRules>();
	world.EmplaceResource<generalszh::gameplay::SceneryClearings>();
	world.EmplaceResource<engine::gameplay::RandomSeed>(engine::gameplay::RandomSeed{setup.seed});
	// The match's own rules from its setup (the level's superweaponRestriction).
	world.EmplaceResource<generalszh::gameplay::MatchRules>(generalszh::gameplay::MatchRules{
		static_cast<std::uint32_t>(std::max<std::int64_t>(0, setup.level.properties.Get<std::int64_t>("superweaponRestriction").value_or(0)))});
}

inline void RegisterWorldComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::DifficultyBonus>();
}
}
