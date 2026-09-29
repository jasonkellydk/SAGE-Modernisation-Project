export module games.generalszh.gameplay.production.algorithms.unit_queue;
import std;
import games.generalszh.gameplay.production.algorithms.build_cost;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.construction.algorithms.building;
import games.generalszh.content.production.production_content;
import games.generalszh.gameplay.aircraft.algorithms.airfields;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.aircraft.components.airfield;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;

// A player's units queued and cancelled at a factory (the control bar's UNIT_BUILD and CANCEL_UNIT_BUILD:
// MSG_QUEUE_UNIT_CREATE -> ProductionUpdate::queueCreateUnit, MSG_CANCEL_UNIT_CREATE -> cancelUnitCreate).
export namespace generalszh::gameplay
{
// Why a unit was not queued (ControlBar::processCommandUI's messages: GUI:NotEnoughMoneyToBuild,
// GUI:ProductionQueueFull, GUI:ParkingPlacesFull, GUI:UnitMaxedOut).
enum class QueueResult : std::uint8_t
{
	Queued,
	NotBuildable,
	MaxedOut,
	NoMoney,
	BuilderDisabled,
	QueueFull,
	ParkingFull,
};

// Whether `factory` may queue `unit` now (BuildAssistant::canMakeUnit, then the queue and, at an airfield, a free
// parking space: ProductionUpdate::canQueueCreateUnit, ParkingPlaceBehavior::hasAvailableSpaceFor).
inline QueueResult CanQueueUnit(GameWorld &game, ecs::Entity factory, const content::ObjectDefinition &unit)
{
	namespace gp = engine::gameplay;
	switch (CanMakeUnit(game, factory, unit))
	{
	case CanMake::Ok: break;
	case CanMake::NoPrerequisites: return QueueResult::NotBuildable;
	case CanMake::MaxedOut: return QueueResult::MaxedOut;
	case CanMake::NoMoney: return QueueResult::NoMoney;
	case CanMake::BuilderDisabled: return QueueResult::BuilderDisabled;
	}
	const auto *queue = game.world.Get<gp::ProductionQueue>(factory);
	if (queue == nullptr)
		return QueueResult::NotBuildable;
	if (queue->Full())
		return QueueResult::QueueFull;
	if (game.world.Get<gp::Airfield>(factory) != nullptr && !FreeSpace(game, factory))
		return QueueResult::ParkingFull;
	return QueueResult::Queued;
}

// Queues `unitName` at the player's `factory`, paid now, for the player's default team, with the factory's next
// production id (ProductionUpdate::requestUniqueUnitID).
inline QueueResult QueueUnit(GameWorld &game, std::uint32_t player, ecs::Entity factory, std::string_view unitName)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const content::ObjectDefinition *unit = game.templates.Content().objects.Find(unitName);
	const auto *owner = world.IsAlive(factory) ? world.Get<gp::Owner>(factory) : nullptr;
	const auto *ref = owner != nullptr ? world.Get<gp::DefinitionRef>(factory) : nullptr;
	if (unit == nullptr || ref == nullptr || owner->player != player)
		return QueueResult::NotBuildable;
	if (const QueueResult can = CanQueueUnit(game, factory, *unit); can != QueueResult::Queued)
		return can;
	const std::int64_t cost = CostToBuild(game, player, *unit);
	if (!world.Resource<gp::PlayerMoney>().Withdraw(player, cost))
		return QueueResult::NoMoney;
	const content::ObjectDefinition &producer = game.templates.DefinitionAt(ref->index);
	const auto production = content::ReadObjectProduction(producer, game.step);
	const std::uint32_t quantity = production ? production->QuantityOf(unit->name) : 1u;
	const std::uint64_t ticks = std::max<std::int64_t>(1, (unit->buildTimeSeconds * Engine::Math::Fixed::FromInt(game.step.TicksPerSecond())).Ceil());
	const std::uint32_t team = game.roster.DefaultTeam(player).value_or(0);
	auto &queue = *world.Get<gp::ProductionQueue>(factory);
	gp::ProductionEntry entry{game.templates.Definition(*unit), team, quantity, queue.nextId++, {}, ticks};
	entry.paid = cost;
	queue.Push(entry);
	return QueueResult::Queued;
}

// Cancels the unit with production id `id` at the player's `factory`, refunding what it cost.
inline bool CancelUnit(GameWorld &game, std::uint32_t player, ecs::Entity factory, std::uint32_t id)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const auto *owner = world.IsAlive(factory) ? world.Get<gp::Owner>(factory) : nullptr;
	auto *queue = owner != nullptr && owner->player == player ? world.Get<gp::ProductionQueue>(factory) : nullptr;
	if (queue == nullptr)
		return false;
	for (std::uint32_t index = 0; index < queue->count; ++index)
	{
		const gp::ProductionEntry &entry = queue->entries[index];
		if (entry.kind != gp::ProductionKind::Unit || entry.productionId != id)
			continue;
		world.Resource<gp::PlayerMoney>().Deposit(player, entry.paid);
		queue->RemoveAt(index);
		return true;
	}
	return false;
}
}
