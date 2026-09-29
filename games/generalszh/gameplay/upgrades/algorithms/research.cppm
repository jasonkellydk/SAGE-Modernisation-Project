export module games.generalszh.gameplay.upgrades.algorithms.research;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;
import games.generalszh.gameplay.upgrades.algorithms.grant_upgrade;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.rts.production.systems.production_system;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.upgrades.components.upgradable;
import engine.gameplay.rts.upgrades.resources.player_upgrades;

// Researching upgrades in a building's production queue (the original's
// ProductionUpdate::queueUpgrade, cancelUpgrade and its PRODUCTION_UPGRADE
// completion), the same for a player's command and an AI's script.
export namespace generalszh::gameplay
{
namespace research_detail
{
inline const content::UpgradeContent *Researchable(GameWorld &game, ecs::Entity building, std::string_view name, std::uint32_t &bit)
{
	const auto found = game.templates.Content().upgrades.Find(name);
	const auto *ref = game.world.IsAlive(building) ? game.world.Get<engine::gameplay::DefinitionRef>(building) : nullptr;
	if (!found || ref == nullptr)
		return nullptr;
	// Object::canProduceUpgrade: a button of its command set researches it.
	const auto &lists = game.templates.Content().researchLists;
	const auto list = lists.find(game.templates.DefinitionAt(ref->index).commandSet);
	if (list == lists.end() || std::find(list->second.begin(), list->second.end(), name) == list->second.end())
		return nullptr;
	bit = *found;
	return &game.templates.Content().upgrades.upgrades[*found];
}
}

// Queues `name` at `building` for its owner, paying for it now; false when it cannot (not its to research,
// a player upgrade the player has or is researching, an object upgrade it has or is researching, a full queue,
// too little money).
inline bool QueueResearch(GameWorld &game, ecs::Entity building, std::string_view name)
{
	namespace gp = engine::gameplay;
	std::uint32_t bit = 0;
	const content::UpgradeContent *upgrade = research_detail::Researchable(game, building, name, bit);
	gp::ProductionQueue *queue = upgrade != nullptr ? game.world.Get<gp::ProductionQueue>(building) : nullptr;
	const gp::Owner *owner = game.world.Get<gp::Owner>(building);
	if (queue == nullptr || owner == nullptr || queue->Full())
		return false;
	auto &players = game.world.Resource<gp::PlayerUpgrades>();
	if (upgrade->player && (players.Completed(owner->player).Has(bit) || players.InProduction(owner->player, bit)))
		return false;
	if (!upgrade->player)
	{
		if (const auto *own = game.world.Get<gp::Upgradable>(building); own != nullptr && own->completed.Has(bit))
			return false;
		for (std::uint32_t index = 0; index < queue->count; ++index)
			if (queue->entries[index].kind == gp::ProductionKind::Upgrade && queue->entries[index].definition == bit)
				return false;
	}
	if (!game.world.Resource<gp::PlayerMoney>().Withdraw(owner->player, upgrade->cost))
		return false;
	gp::ProductionEntry entry;
	entry.definition = bit;
	entry.kind = gp::ProductionKind::Upgrade;
	entry.ticksTotal = std::max<std::uint64_t>(upgrade->buildTicks, 1);
	entry.paid = upgrade->cost;
	queue->Push(entry);
	if (upgrade->player)
		players.StartProduction(owner->player, bit);
	return true;
}

// Takes `name` out of `building`'s queue and gives its owner the money back; false when it is not queued there.
inline bool CancelResearch(GameWorld &game, ecs::Entity building, std::string_view name)
{
	namespace gp = engine::gameplay;
	const auto found = game.templates.Content().upgrades.Find(name);
	gp::ProductionQueue *queue = found && game.world.IsAlive(building) ? game.world.Get<gp::ProductionQueue>(building) : nullptr;
	const gp::Owner *owner = queue != nullptr ? game.world.Get<gp::Owner>(building) : nullptr;
	if (owner == nullptr)
		return false;
	for (std::uint32_t index = 0; index < queue->count; ++index)
		if (queue->entries[index].kind == gp::ProductionKind::Upgrade && queue->entries[index].definition == *found)
		{
			const content::UpgradeContent &upgrade = game.templates.Content().upgrades.upgrades[*found];
			game.world.Resource<gp::PlayerMoney>().Deposit(owner->player, upgrade.cost);
			queue->RemoveAt(index);
			if (upgrade.player)
				game.world.Resource<gp::PlayerUpgrades>().CancelProduction(owner->player, *found);
			return true;
		}
	return false;
}

// AIPlayer::buildUpgrade (the AI_PLAYER_BUILD_UPGRADE script action): a player upgrade the player has
// neither nor is researching, and can afford, queued at the first of its buildings (its teams' members, in
// order) whose command set researches it and whose queue takes it. False when nothing queued it.
inline bool PlayerBuildUpgrade(GameWorld &game, std::uint32_t player, std::string_view name, std::span<const ecs::Entity> factories)
{
	namespace gp = engine::gameplay;
	const auto bit = game.templates.Content().upgrades.Find(name);
	if (!bit)
		return false;
	const content::UpgradeContent &upgrade = game.templates.Content().upgrades.upgrades[*bit];
	const auto &players = game.world.Resource<gp::PlayerUpgrades>();
	if (!upgrade.player || players.InProduction(player, *bit) || players.Completed(player).Has(*bit) ||
		game.world.Resource<gp::PlayerMoney>().Balance(player) < upgrade.cost)
		return false;
	// The build list's structures in order: standing (not being built, not sold), producing, with the button.
	for (const ecs::Entity factory : factories)
	{
		if (!game.world.IsAlive(factory) || game.world.Has<gp::UnderConstruction>(factory) || game.world.Has<gp::Sale>(factory) ||
			!game.world.Has<gp::ProductionQueue>(factory))
			continue;
		if (QueueResearch(game, factory, name))
			return true;
	}
	return false;
}
}
