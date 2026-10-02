export module games.generalszh.gameplay.construction.algorithms.selling;
import std;
import games.generalszh.gameplay.production.algorithms.build_cost;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.construction.components.construction_progress;
import engine.gameplay.rts.construction.resources.sales;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.upgrades.resources.player_upgrades;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.health.components.health;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.common.healing.components.healing;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.aircraft.algorithms.airfields;
import games.generalszh.gameplay.world.resources.deselections;
import engine.gameplay.rts.mines.components.minefield;
import engine.gameplay.common.identity.components.producer;
import engine.ecs.query.query;

// Selling structures (the original's BuildAssistant), between ticks:
//   sellObject: a standing structure of its owner's, not already for sale,
//   goes on sale this tick (its construction just under 100%: the sale
//   system brings it down); whatever it was producing is refunded
//   (cancelAndRefundAllProduction); its occupants leave (onSelling: a
//   garrison, or a player's last tunnel, puts everyone out at once; another
//   tunnel hands them to the network; a transport unloads); it stops what
//   it was doing (aiIdle); it leaves every player's selection (deselectObject:
//   OBJECT_STATUS_UNSELECTABLE from now on); an airfield's jets parked or
//   taking off or landing are killed (killAllParkedUnits); the mines it made
//   are destroyed at once (no death: destroyObject);
//   update: a sale that is over pays its RefundValue, or SellPercentage of
//   its cost, to its owner and removes it.
export namespace generalszh::gameplay
{
namespace selling_detail
{
// ProductionUpdate::cancelAndRefundAllProduction.
inline void RefundProduction(GameWorld &game, ecs::Entity building)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	gp::ProductionQueue *queue = world.Get<gp::ProductionQueue>(building);
	const gp::Owner *owner = world.Get<gp::Owner>(building);
	if (queue == nullptr || owner == nullptr || queue->count == 0)
		return;
	const auto &upgrades = game.templates.Content().upgrades.upgrades;
	for (std::uint32_t index = 0; index < queue->count; ++index)
	{
		const gp::ProductionEntry &entry = queue->entries[index];
		world.Resource<gp::PlayerMoney>().Deposit(owner->player, entry.paid);
		if (entry.kind == gp::ProductionKind::Upgrade && entry.definition < upgrades.size() && upgrades[entry.definition].player)
			world.Resource<gp::PlayerUpgrades>().CancelProduction(owner->player, entry.definition);
	}
	gp::ProductionQueue emptied;
	emptied.capacity = queue->capacity;
	*queue = emptied;
}

// The mines `building` made (KINDOF_MINE whose producer it is): a minefield's maker, or any mine's producer.
inline std::vector<ecs::Entity> MinesMadeBy(GameWorld &game, ecs::Entity building)
{
	namespace gp = engine::gameplay;
	std::vector<ecs::Entity> mines;
	ecs::Query<ecs::Read<gp::DefinitionRef>, ecs::Optional<gp::Minefield>, ecs::Optional<gp::Producer>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto fields = chunk.template Get<gp::Minefield>();
		const auto producers = chunk.template Get<gp::Producer>();
		if (fields.empty() && producers.empty())
			return;
		const auto refs = chunk.template Get<gp::DefinitionRef>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < refs.size(); ++row)
		{
			const bool made = (!fields.empty() && fields[row].producer == building) || (!producers.empty() && producers[row].entity == building);
			if (made && game.templates.DefinitionAt(refs[row].index).Is("MINE"))
				mines.push_back(entities[row]);
		}
	});
	std::sort(mines.begin(), mines.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
	return mines;
}

// OpenContain::removeAllContained: everyone aboard out at once, where the container stands.
inline void PutOut(GameWorld &game, ecs::Entity container, std::vector<ecs::Entity> riders)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const gp::Transform *at = world.Get<gp::Transform>(container);
	for (const ecs::Entity rider : riders)
	{
		if (!world.IsAlive(rider))
			continue;
		world.Remove<gp::Passenger>(rider);
		world.Remove<gp::OffMap>(rider);
		if (at != nullptr)
			if (auto *where = world.Get<gp::Transform>(rider))
				where->position = at->position;
	}
	if (auto *transport = world.Get<gp::Transport>(container))
		transport->occupied = 0;
}
}

inline bool BeginSale(GameWorld &game, ecs::Entity building)
{
	namespace gp = engine::gameplay;
	using namespace selling_detail;
	auto &world = game.world;
	if (!world.IsAlive(building) || world.Has<gp::Sale>(building) || world.Has<gp::Dying>(building))
		return false;
	const auto *ref = world.Get<gp::DefinitionRef>(building);
	const auto *health = world.Get<gp::Health>(building);
	if (ref == nullptr || !game.templates.DefinitionAt(ref->index).Is("STRUCTURE") || (health != nullptr && gp::IsDead(*health)))
		return false;
	if (!world.Has<gp::ConstructionProgress>(building))
		world.Add<gp::ConstructionProgress>(building);
	world.Get<gp::ConstructionProgress>(building)->percent = Engine::Math::Fixed::FromRatio(999, 10);
	world.Add<gp::Sale>(building);
	world.Get<gp::Sale>(building)->since = game.tick;
	// BaseRegenerateUpdate stops for good.
	if (auto *regen = world.Get<gp::SelfHealing>(building); regen != nullptr && regen->WaitsWhileNotStanding())
		regen->waiting = 1;
	RefundProduction(game, building);
	// onSelling: a player's last tunnel (TakeAll hands a networked one's riders to the rest of its network) or a
	// garrison puts everyone out; another container unloads them.
	if (world.Has<gp::Transport>(building))
	{
		const bool networked = game.manifest.NetworkOf(building).has_value();
		if (networked || world.Has<gp::Garrison>(building))
			PutOut(game, building, game.manifest.TakeAll(building));
		else
			world.Get<gp::Transport>(building)->state = gp::TransportState::Unloading;
	}
	OrderStop(game, building, false, false);
	// GameLogic::deselectObject(PLAYERMASK_ALL): out of every selection (the presentation's).
	if (auto *deselections = world.FindResource<Deselections>())
		deselections->list.push_back(building);
	// ParkingPlaceBehavior::killAllParkedUnits.
	if (game.templates.Content().parking.contains(game.templates.DefinitionAt(ref->index).name))
	{
		const std::array<ecs::Entity, 1> field{building};
		KillJetsParkedAt(game, field);
	}
	// Its mines destroyed, now.
	if (std::vector<ecs::Entity> mines = MinesMadeBy(game, building); !mines.empty())
		RetireNow(game, std::move(mines));
	return true;
}

// Player::sellEverythingUnderTheSun (PLAYER_SELL_EVERYTHING): every faction structure (an FS_ kind), command centre and
// power plant of the player goes on sale (BuildAssistant::sellObject; one already for sale stays as it is).
inline void SellEverything(GameWorld &game, std::uint32_t player)
{
	namespace gp = engine::gameplay;
	static constexpr std::array<std::string_view, 16> kinds{"FS_FACTORY", "FS_BASE_DEFENSE", "FS_TECHNOLOGY", "FS_SUPPLY_DROPZONE", "FS_SUPERWEAPON",
		"FS_BLACK_MARKET", "FS_SUPPLY_CENTER", "FS_STRATEGY_CENTER", "FS_FAKE", "FS_INTERNET_CENTER", "FS_ADVANCED_TECH", "FS_BARRACKS", "FS_WARFACTORY",
		"FS_AIRFIELD", "COMMANDCENTER", "FS_POWER"};
	std::vector<ecs::Entity> sold;
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		if (game.roster.TeamAt(team).owner != player)
			continue;
		for (const ecs::Entity member : game.roster.TeamAt(team).members)
			if (const auto *ref = game.world.IsAlive(member) ? game.world.Get<gp::DefinitionRef>(member) : nullptr)
			{
				const auto &definition = game.templates.DefinitionAt(ref->index);
				if (std::ranges::any_of(kinds, [&](std::string_view kind) { return definition.Is(kind); }))
					sold.push_back(member);
			}
	}
	for (const ecs::Entity building : sold)
		BeginSale(game, building);
}

// The sales that are over this tick: paid for and removed.
inline void FinishSales(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using namespace selling_detail;
	auto &world = game.world;
	const std::vector<ecs::Entity> done = world.Resource<gp::SalesDone>().entities;
	world.Resource<gp::SalesDone>().entities.clear();
	std::vector<ecs::Entity> gone;
	for (const ecs::Entity building : done)
	{
		if (!world.IsAlive(building))
			continue;
		const auto *ref = world.Get<gp::DefinitionRef>(building);
		const auto *owner = world.Get<gp::Owner>(building);
		if (ref != nullptr && owner != nullptr)
		{
			const content::ObjectDefinition &definition = game.templates.DefinitionAt(ref->index);
			const std::int64_t value = definition.refundValue != 0 ? definition.refundValue
				: (Engine::Math::Fixed::FromInt(CostToBuild(game, owner->player, definition)) * game.templates.Content().gameData.sellPercentage).Floor();
			world.Resource<gp::PlayerMoney>().Deposit(owner->player, value);
		}
		RefundProduction(game, building);
		gone.push_back(building);
	}
	if (!gone.empty())
		RetireNow(game, std::move(gone));
}
}
