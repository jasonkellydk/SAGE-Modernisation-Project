export module games.generalszh.gameplay.upgrades.algorithms.grant_upgrade;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.upgrades.components.upgradable;
import engine.gameplay.rts.upgrades.resources.player_upgrades;
import engine.gameplay.rts.upgrades.resources.upgrade_triggers;
import engine.gameplay.rts.upgrades.systems.upgrade_system;
import engine.gameplay.common.identity.components.definition_ref;

// Granting upgrades, the same whether production finished them or a script
// gave them: a player's (Player::addUpgrade COMPLETE: every object of the
// player looks at its triggers again) or an object's own (Object::giveUpgrade:
// the object looks again). Unknown names and the wrong kind do nothing.
export namespace generalszh::gameplay
{
inline bool GrantPlayerUpgrade(GameWorld &game, std::uint32_t player, std::string_view name)
{
	const auto upgrade = game.templates.Content().upgrades.Find(name);
	if (!upgrade)
		return false;
	game.world.Resource<engine::gameplay::PlayerUpgrades>().Grant(player, *upgrade);
	return true;
}

inline bool GiveObjectUpgrade(GameWorld &game, ecs::Entity entity, std::string_view name)
{
	const auto upgrade = game.templates.Content().upgrades.Find(name);
	if (!upgrade || !game.world.IsAlive(entity))
		return false;
	auto *upgradable = game.world.Get<engine::gameplay::Upgradable>(entity);
	if (upgradable == nullptr)
	{
		game.world.Add<engine::gameplay::Upgradable>(entity);
		upgradable = game.world.Get<engine::gameplay::Upgradable>(entity);
	}
	upgradable->completed.Set(*upgrade);
	upgradable->stale = 1;
	return true;
}

// Object::removeUpgrade of an upgrade the object has (UpgradeDie: its producer's; none had: nothing, the original's
// debug complaint aside). Returns whether it had it.
inline bool RemoveObjectUpgrade(GameWorld &game, ecs::Entity entity, std::string_view name)
{
	namespace gp = engine::gameplay;
	const auto upgrade = game.templates.Content().upgrades.Find(name);
	gp::Upgradable *upgradable = upgrade && game.world.IsAlive(entity) ? game.world.Get<gp::Upgradable>(entity) : nullptr;
	if (upgradable == nullptr || !upgradable->completed.Has(*upgrade))
		return false;
	const gp::DefinitionRef *definition = game.world.Get<gp::DefinitionRef>(entity);
	const std::vector<gp::UpgradeTrigger> *triggers = definition != nullptr ? game.world.Resource<gp::UpgradeTriggers>().Of(definition->index) : nullptr;
	gp::RemoveObjectUpgrades(*upgradable, triggers != nullptr ? *triggers : std::vector<gp::UpgradeTrigger>{}, gp::UpgradeMask::Of(*upgrade));
	return true;
}
}
