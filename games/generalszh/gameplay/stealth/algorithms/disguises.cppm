export module games.generalszh.gameplay.stealth.algorithms.disguises;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import engine.gameplay.rts.stealth.components.stealth;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;

// StealthUpdate::disguiseAsObject, as a disguiser's SpecialAbilityUpdate triggers SPECIAL_DISGUISE_AS_VEHICLE on its target
// (triggerAbilityEffect): a target with a controlling player lends its look and player (a disguised target its own
// disguise's), which the unit takes over its DisguiseTransitionTime from the next tick (its update woken); no target, a
// disguised unit starts losing its disguise.
export namespace generalszh::gameplay
{
inline void DisguiseAsObject(GameWorld &game, ecs::Entity unit, ecs::Entity target)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	gp::Stealth *stealth = world.IsAlive(unit) ? world.Get<gp::Stealth>(unit) : nullptr;
	if (stealth == nullptr)
		return;
	const gp::Owner *owner = target != ecs::Entity{} && world.IsAlive(target) ? world.Get<gp::Owner>(target) : nullptr;
	const gp::DefinitionRef *ref = owner != nullptr ? world.Get<gp::DefinitionRef>(target) : nullptr;
	if (owner == nullptr || ref == nullptr)
	{
		gp::DropDisguise(*stealth);
		return;
	}
	std::uint32_t definition = ref->index;
	std::int32_t player = static_cast<std::int32_t>(owner->player);
	if (const gp::Stealth *theirs = world.Get<gp::Stealth>(target); theirs != nullptr && theirs->IsDisguised())
	{
		definition = theirs->disguiseAs;
		player = theirs->disguisePlayer;
	}
	const std::uint32_t team = player >= 0 ? game.roster.DefaultTeam(static_cast<std::uint32_t>(player)).value_or(gp::Stealth::NoTeam) : gp::Stealth::NoTeam;
	gp::DisguiseAs(*stealth, definition, player, team);
}
}
