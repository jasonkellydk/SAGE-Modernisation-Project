export module games.generalszh.gameplay.upgrades.algorithms.upgrade_affects;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.rts.upgrades.components.upgradable;
import engine.gameplay.rts.upgrades.resources.upgrade_triggers;
import engine.gameplay.rts.upgrades.resources.player_upgrades;

// Whether an upgrade would do anything for an object (the control bar's and the scripts' question).
export namespace generalszh::gameplay
{
// Object::affectedByUpgrade: one of its upgrade modules would go (wouldUpgrade) for its player's (`player`) completed
// upgrades, its own and this one together.
inline bool AffectedByUpgrade(const GameWorld &game, ecs::Entity unit, std::uint32_t player, std::uint32_t upgrade)
{
	namespace gp = engine::gameplay;
	const auto *ref = game.world.Get<gp::DefinitionRef>(unit);
	const auto *triggers = ref != nullptr ? game.world.Resource<gp::UpgradeTriggers>().Of(ref->index) : nullptr;
	if (triggers == nullptr)
		return false;
	const auto *own = game.world.Get<gp::Upgradable>(unit);
	gp::UpgradeMask key = game.world.Resource<gp::PlayerUpgrades>().Completed(player);
	if (own != nullptr)
		key.Add(own->completed);
	key.Set(upgrade);
	for (std::size_t index = 0; index < triggers->size(); ++index)
	{
		const bool executed = own != nullptr && index < 32 && ((own->executed >> index) & 1u) != 0;
		if (gp::WouldUpgrade((*triggers)[index], key, executed))
			return true;
	}
	return false;
}
}
