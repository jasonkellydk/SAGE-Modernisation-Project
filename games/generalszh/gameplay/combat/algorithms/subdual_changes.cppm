export module games.generalszh.gameplay.combat.algorithms.subdual_changes;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import engine.gameplay.common.health.components.subdual;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.common.appearance.components.appearance;
import games.generalszh.content.objects.model_conditions;

// ActiveBody::onSubdualChange, the game's part, after the tick: a container becoming subdued orders all its passengers
// to go idle (orderAllPassengersToIdle, CMD_FROM_AI); a missile jammed shows it (projectileNowJammed:
// MODELCONDITION_JAMMED; MissileJamSystem scattered its goal). (DISABLED_SUBDUED itself is set or cleared by
// SubdualSystem.)
export namespace generalszh::gameplay
{
inline void ApplySubdualChanges(GameWorld &game)
{
	auto &world = game.world;
	const auto *changes = world.FindResource<engine::gameplay::SubdualChanges>();
	const auto *manifest = world.FindResource<engine::gameplay::CargoManifest>();
	if (changes == nullptr || manifest == nullptr)
		return;
	for (const engine::gameplay::SubdualChange &change : changes->list)
	{
		if (!change.subdued || !world.IsAlive(change.entity))
			continue;
		if (change.projectile)
		{
			if (!world.Has<engine::gameplay::Appearance>(change.entity))
				world.Add<engine::gameplay::Appearance>(change.entity);
			world.Get<engine::gameplay::Appearance>(change.entity)->Set(content::ModelConditionBit("JAMMED"), true);
			continue;
		}
		const auto aboard = manifest->Aboard(change.entity);
		const std::vector<ecs::Entity> riders(aboard.begin(), aboard.end());
		for (const ecs::Entity rider : riders)
			AiIdle(game, rider);
	}
}
}
