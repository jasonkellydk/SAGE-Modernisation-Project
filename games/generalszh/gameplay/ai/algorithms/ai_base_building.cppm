export module games.generalszh.gameplay.ai.algorithms.ai_base_building;
import std;
import games.generalszh.gameplay.production.algorithms.build_cost;

export import games.generalszh.gameplay.ai.algorithms.ai_players;
import games.generalszh.gameplay.construction.algorithms.rebuild_holes;
import games.generalszh.gameplay.construction.algorithms.building;
import games.generalszh.gameplay.construction.algorithms.build_legality;
import games.generalszh.gameplay.production.algorithms.team_building;
import games.generalszh.gameplay.production.algorithms.unit_queue;
import games.generalszh.content.production.production_content;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.construction.components.builder;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.harvesting.components.harvester;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.economy.resources.player_energy;
import engine.gameplay.rts.stealth.components.stealth;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.navigation.algorithms.route_search;
import engine.gameplay.rts.harvesting.components.resource_store;
import engine.gameplay.common.spatial.components.off_map;
import engine.ecs.query.query;
import Engine.Core.Math.FixedAngle;

// A computer player putting up its base (AISkirmishPlayer::doBaseBuilding / processBaseBuilding,
// AIPlayer::buildStructureWithDozer, findDozer, queueDozer, isLocationSafe, buildSpecificAIBuilding), once a tick
// after the tick's objects (TheAI->update).
export namespace generalszh::gameplay
{
namespace ai_base_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;

inline constexpr std::int32_t UnlimitedRebuilds = -1;
inline constexpr std::array<std::string_view, 15> FactionStructureKinds{"FS_POWER", "FS_FACTORY", "FS_BASE_DEFENSE", "FS_TECHNOLOGY",
	"FS_SUPPLY_DROPZONE", "FS_SUPERWEAPON", "FS_BLACK_MARKET", "FS_SUPPLY_CENTER", "FS_STRATEGY_CENTER", "FS_FAKE", "FS_INTERNET_CENTER",
	"FS_ADVANCED_TECH", "FS_BARRACKS", "FS_WARFACTORY", "FS_AIRFIELD"};

inline bool IsBuildable(const AiBuildSlot &slot) { return slot.rebuilds > 0 || slot.rebuilds == UnlimitedRebuilds; }
inline void DecrementRebuilds(AiBuildSlot &slot)
{
	if (slot.rebuilds > 0)
		--slot.rebuilds;
}

inline const content::ObjectDefinition *DefinitionOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
}

inline std::optional<std::uint32_t> OwnerOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *owner = game.world.IsAlive(entity) ? game.world.Get<gp::Owner>(entity) : nullptr;
	return owner != nullptr ? std::optional(owner->player) : std::nullopt;
}

// The build list's object for this slot, still standing (findObjectByID: what is gone is none).
inline ecs::Entity Standing(const GameWorld &game, const AiBuildSlot &slot) { return game.world.IsAlive(slot.built) ? slot.built : ecs::Entity{}; }
}

// isLocationSafe: no enemy (not allied or neutral, alive, not hidden by stealth, not an insignificant building, not a
// harvester or dozer) within SupplyCenterSafeRadius plus the structure's bounding circle, from bounding circles.
inline bool IsLocationSafe(const GameWorld &game, std::uint32_t player, Engine::Math::FixedVector2 at, const content::ObjectDefinition &structure)
{
	namespace gp = engine::gameplay;
	using namespace ai_base_detail;
	const Fixed radius = game.templates.Content().aiData.supplyCenterSafeRadius + content::BoundingSphereRadius(structure.geometry);
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	if (relationships == nullptr)
		return true;
	bool safe = true;
	for (std::uint32_t team = 0; team < game.roster.TeamCount() && safe; ++team)
	{
		const auto &record = game.roster.TeamAt(team);
		if (!relationships->Enemies(player, record.owner))
			continue;
		for (const ecs::Entity them : record.members)
		{
			const content::ObjectDefinition *definition = DefinitionOf(game, them);
			const auto *where = definition != nullptr ? game.world.Get<gp::Transform>(them) : nullptr;
			if (where == nullptr || game.world.Get<gp::Dying>(them) != nullptr)
				continue;
			if (const auto *stealth = game.world.Get<gp::Stealth>(them); stealth != nullptr && stealth->Hidden())
				continue;
			if (definition->Is("HARVESTER") || definition->Is("DOZER"))
				continue;
			// PartitionFilterInsignificantBuildings(allow non-buildings, not the insignificant): a structure of no faction
			// that holds nobody (a container not a garrison, or empty) is no threat.
			if (definition->Is("STRUCTURE") &&
				std::none_of(FactionStructureKinds.begin(), FactionStructureKinds.end(), [&](std::string_view kind) { return definition->Is(kind); }))
			{
				const bool container = game.world.Get<gp::Garrison>(them) != nullptr || game.world.Get<gp::Transport>(them) != nullptr;
				const bool garrisoned = game.world.Get<gp::Garrison>(them) != nullptr && game.manifest.Count(them) > 0;
				if (container && !garrisoned)
					continue;
			}
			const Fixed reach = radius + content::BoundingSphereRadius(definition->geometry);
			if (ai_detail::Squared(where->position.x - at.x, where->position.y - at.y) <= ai_detail::Squared(reach, Fixed{}))
			{
				safe = false;
				break;
			}
		}
	}
	return safe;
}

// findFactory: a factory of the build list's, the player's own, standing, not under construction or being sold, that
// can make it (isPossibleToMakeUnit: its command set builds it and its prerequisites are there): the first idle one,
// else (busyOK) the last busy one.
inline ecs::Entity FindFactory(GameWorld &game, AiPlayer &ai, const content::ObjectDefinition &thing, bool busyOK)
{
	namespace gp = engine::gameplay;
	using namespace ai_base_detail;
	const auto owned = OwnedObjects(game);
	ecs::Entity busy;
	for (AiBuildSlot &slot : ai.buildList)
	{
		const ecs::Entity factory = Standing(game, slot);
		if (factory == ecs::Entity{})
			continue;
		if (OwnerOf(game, factory) != ai.player)
		{
			slot.built = {};
			continue;
		}
		if (game.world.Get<gp::UnderConstruction>(factory) != nullptr || game.world.Get<gp::Sale>(factory) != nullptr)
			continue;
		const auto *queue = game.world.Get<gp::ProductionQueue>(factory);
		if (queue == nullptr)
			continue;
		const content::ObjectDefinition *definition = DefinitionOf(game, factory);
		const auto list = game.templates.Content().buildLists.find(definition->commandSet);
		if (list == game.templates.Content().buildLists.end() || std::find(list->second.begin(), list->second.end(), thing.name) == list->second.end() ||
			!PrerequisitesMet(owned, ai.player, thing))
			continue;
		if (queue->count == 0)
			return factory;
		if (busyOK)
			busy = factory;
	}
	return busyOK ? busy : ecs::Entity{};
}

// startTraining: a factory for it (findFactory; a busy one will do when busyOK) queues one (queueCreateUnit, paid now);
// the order notes the factory.
inline bool StartTraining(GameWorld &game, AiPlayer &ai, AiWorkOrder &order, bool busyOK)
{
	const content::ObjectDefinition &thing = game.templates.DefinitionAt(order.definition);
	const ecs::Entity factory = FindFactory(game, ai, thing, busyOK);
	if (factory == ecs::Entity{} || QueueUnit(game, ai.player, factory, thing.name) != QueueResult::Queued)
		return false;
	order.factory = factory;
	return true;
}

// dozerInQueue: a team being built orders a dozer that is not a supply gatherer.
inline bool DozerInQueue(const GameWorld &game, const AiPlayer &ai)
{
	for (const AiTeamInQueue &entry : ai.buildQueue)
		for (const AiWorkOrder &order : entry.orders)
			if (game.templates.DefinitionAt(order.definition).Is("DOZER") && !order.resourceGatherer)
				return true;
	return false;
}

// queueDozer: unless one is being built, the first dozer type one of its factories can make (a busy one will do) is
// ordered for the default team, first in the build queue, and trained at once.
inline void QueueDozer(GameWorld &game, AiPlayer &ai)
{
	if (DozerInQueue(game, ai))
		return;
	// ThingFactory order is its own; here the catalog's (by name): only a player able to make two dozer types could tell.
	for (const auto &[name, definition] : game.templates.Content().objects)
	{
		if (!definition.Is("DOZER") || FindFactory(game, ai, definition, true) == ecs::Entity{})
			continue;
		AiTeamInQueue entry;
		entry.priorityBuild = true;
		entry.orders.push_back({game.templates.Definition(definition), {}, 0, 1, true, false});
		entry.started = game.tick;
		entry.team = game.roster.DefaultTeam(ai.player).value_or(engine::gameplay::Team::Own);
		ai.buildQueue.insert(ai.buildQueue.begin(), std::move(entry));
		ai.teamDelay = 0;
		StartTraining(game, ai, ai.buildQueue.front().orders.front(), true);
		break;
	}
}

// findDozer: a dozer of its own that is not ferrying supplies (a worker told to, or at it, is left alone) and not
// building already; an idle one nearest `at` before a busy one; none at all: one is queued.
inline ecs::Entity FindDozer(GameWorld &game, AiPlayer &ai, Engine::Math::FixedVector2 at)
{
	namespace gp = engine::gameplay;
	using namespace ai_base_detail;
	ecs::Entity dozer, closest;
	std::int64_t closestDistance = 0;
	bool need = true;
	ai_detail::ForPlayerObjects(game, ai.player, [&](ecs::Entity entity) {
		const content::ObjectDefinition *definition = DefinitionOf(game, entity);
		if (definition == nullptr || !definition->Is("DOZER") || !building_detail::IsBuilder(*definition))
			return;
		const auto *task = game.world.Get<gp::Builder>(entity);
		const auto *harvester = game.world.Get<gp::Harvester>(entity);
		if (task == nullptr && harvester != nullptr &&
			(harvester->state == gp::HarvesterState::Wanting || harvester->state == gp::HarvesterState::Docking || harvester->forceWanting))
			return;
		if (entity == ai.repairDozer)
			return; // don't steal the repair dozer
		need = false;
		if (task != nullptr && task->repair == 0)
			return; // already building
		if (task == nullptr)
			dozer = entity;
		if (dozer == ecs::Entity{})
			dozer = entity;
		if (dozer != ecs::Entity{} && task == nullptr)
		{
			const auto *where = game.world.Get<gp::Transform>(dozer);
			const std::int64_t distance = where != nullptr ? ai_detail::Squared(at.x - where->position.x, at.y - where->position.y) : 0;
			if (closest == ecs::Entity{} || distance < closestDistance)
			{
				closest = dozer;
				closestDistance = distance;
			}
		}
	});
	if (need)
		QueueDozer(game, ai);
	return closest != ecs::Entity{} ? closest : dozer;
}

// buildStructureWithDozer: a dozer (none: nothing), the money for it, no enemy in the way, then the spot itself or the
// nearest the search around it finds (a skirmish player looks 120 cells out in steps of 4); a dozer that cannot get
// there is put there; then it builds (as a computer player's: no further checks). The scaffold, or none.
inline ecs::Entity BuildStructureWithDozer(GameWorld &game, AiPlayer &ai, const content::ObjectDefinition &plan, AiBuildSlot &slot)
{
	namespace gp = engine::gameplay;
	using namespace ai_base_detail;
	const ecs::Entity dozer = FindDozer(game, ai, slot.location);
	if (dozer == ecs::Entity{})
		return {};
	if (game.world.Resource<gp::PlayerMoney>().Balance(ai.player) < CostToBuild(game, ai.player, plan))
		return {};
	const Engine::Math::TurnAngle angle = Engine::Math::TurnFromDegrees(slot.angleDegrees);
	Engine::Math::FixedVector2 at = slot.location;
	// The things around the whole search, gathered once (the search reaches 120 cells out).
	const BuildScene scene = GatherBuildScene(game, at, Fixed::FromInt(gp::PathfindCellSize * 122) + FootprintRadius(FootprintOf(plan)));
	const auto legal = [&](Engine::Math::FixedVector2 where, std::uint32_t options) {
		return CheckBuildLocation(game, plan, where, angle, dozer, options, &scene) == LegalBuild::Ok;
	};
	if (!legal(at, build_check::NoEnemyObjectOverlap))
		return {};
	constexpr std::uint32_t full = build_check::ClearPath | build_check::TerrainRestrictions | build_check::NoObjectOverlap;
	if (!legal(at, full))
	{
		// Wiggle it a little: square rings around the spot, bottom and top rows then left and right columns.
		const Fixed cell = Fixed::FromInt(gp::PathfindCellSize);
		const Fixed limit = cell * Fixed::FromInt(120);
		bool valid = false;
		Engine::Math::FixedVector2 found = at;
		for (Fixed offsetStep{}; offsetStep < limit && !valid; offsetStep = offsetStep + cell * Fixed::FromInt(2))
		{
			offsetStep = offsetStep + cell * Fixed::FromInt(2);
			const Fixed offset = offsetStep / Fixed::FromInt(2);
			const Fixed y = at.y - offset;
			for (Fixed x = at.x - offset; x <= at.x + offset && !valid; x = x + cell)
			{
				x = x + cell;
				if (legal({x, y}, full))
				{
					found = {x, y};
					valid = true;
				}
				else if (legal({x, y + offsetStep}, full))
				{
					found = {x, y + offsetStep};
					valid = true;
				}
			}
			if (valid)
				break;
			const Fixed x = at.x - offset;
			for (Fixed yy = at.y - offset; yy <= at.y + offset && !valid; yy = yy + cell)
			{
				yy = yy + cell;
				if (legal({x, yy}, full))
				{
					found = {x, yy};
					valid = true;
				}
				else if (legal({x + offsetStep, yy}, full))
				{
					found = {x + offsetStep, yy};
					valid = true;
				}
			}
		}
		if (valid)
			at = found;
		else if (!legal(at, build_check::NoEnemyObjectOverlap))
			return {};
	}
	// clientSafeQuickDoesPathExist: a dozer that cannot get there is teleported.
	if (!legal(at, build_check::ClearPath))
		if (auto *where = game.world.Get<gp::Transform>(dozer))
			where->position = {at.x, at.y, game.ground.At(at)};
	const ecs::Entity built = BeginConstruction(game, dozer, plan.name, at, angle);
	if (game.world.IsAlive(built))
	{
		slot.built = built;
		slot.builtTick = game.tick + 1;
		slot.underConstruction = true;
	}
	return built;
}

// processBaseBuilding (a skirmish player's): over the build list, what was lost is noted (rebuilt only after
// RebuildDelayTimeSeconds); what stands under construction gets its dozer back to work (another found if it is gone
// or no longer its); of what is missing and safe to build at, the first marked for priority is taken and the first
// power plant (not a cash generator) while short of power or built unasked; nothing else is (the original checks
// canMakeUnit against the choice so far, which is none until a priority build is taken). A power plant goes first
// unless one is under construction. It is built with a dozer; then the structure timer starts again
// (StructureSeconds, faster when poor or wealthy).
inline void ProcessBaseBuilding(GameWorld &game, AiPlayer &ai)
{
	namespace gp = engine::gameplay;
	using namespace ai_base_detail;
	if (!ai.readyToBuildStructure)
		return;
	const auto &content = game.templates.Content();
	const auto &energy = game.world.Resource<gp::PlayerEnergy>();
	const bool underPowered = energy.Production(ai.player) < energy.Consumption(ai.player);
	bool powerUnderConstruction = false;
	AiBuildSlot *choice = nullptr;
	const content::ObjectDefinition *choicePlan = nullptr;
	AiBuildSlot *power = nullptr;
	const content::ObjectDefinition *powerPlan = nullptr;
	bool priority = false;
	for (AiBuildSlot &slot : ai.buildList)
	{
		const content::ObjectDefinition *plan = content.objects.Find(slot.structure);
		if (plan == nullptr)
			continue;
		ecs::Entity standing = Standing(game, slot);
		if (slot.built != ecs::Entity{})
		{
			if (standing == ecs::Entity{})
			{
				// Destroyed: a GLA rebuild hole it left takes its place in the plan.
				const ecs::Entity prior = slot.built;
				slot.built = {};
				slot.builtTick = game.tick + 1;
				if (const ecs::Entity hole = HoleOf(game, prior); hole != ecs::Entity{})
					slot.built = hole;
			}
			else if (OwnerOf(game, standing) == ai.player)
			{
				if (const auto *building = game.world.Get<gp::UnderConstruction>(standing))
				{
					if (plan->Is("FS_POWER") && !plan->Is("CASH_GENERATOR"))
						powerUnderConstruction = true;
					ecs::Entity dozer = building->builder;
					if (dozer != ecs::Entity{} && (!game.world.IsAlive(dozer) || OwnerOf(game, dozer) != ai.player ||
													  (game.world.Get<gp::Disabled>(dozer) != nullptr && game.world.Get<gp::Disabled>(dozer)->mask != 0)))
						dozer = {};
					if (dozer == ecs::Entity{})
					{
						QueueDozer(game, ai);
						dozer = FindDozer(game, ai, slot.location);
						if (dozer == ecs::Entity{})
							continue;
					}
					// aiResumeConstruction.
					OrderWork(game, dozer, standing);
				}
			}
			else
			{
				slot.built = {}; // captured
				slot.builtTick = game.tick + 1;
				standing = {};
			}
		}
		if (slot.built == ecs::Entity{} && slot.builtTick > 0)
		{
			const std::uint64_t delay = static_cast<std::uint64_t>(
				(content.aiData.rebuildDelaySeconds * Fixed::FromInt(game.step.TicksPerSecond())).Floor());
			if (slot.builtTick + delay > game.tick)
				continue;
			slot.builtTick = 0; // ready to build
		}
		if (standing != ecs::Entity{})
			continue;
		if (!IsLocationSafe(game, ai.player, slot.location, *plan))
			continue;
		if (slot.priorityBuild && !priority)
		{
			choice = &slot;
			choicePlan = plan;
			priority = true;
		}
		if (plan->Is("FS_POWER") && power == nullptr && !plan->Is("CASH_GENERATOR") && (underPowered || slot.automaticallyBuild))
		{
			power = &slot;
			powerPlan = plan;
		}
		if (!slot.automaticallyBuild)
			continue;
		const ecs::Entity dozer = FindDozer(game, ai, slot.location);
		if (dozer == ecs::Entity{})
		{
			if (underPowered)
				QueueDozer(game, ai);
			continue;
		}
		if (choicePlan == nullptr || CanMakeUnit(game, dozer, *choicePlan) != CanMake::Ok)
			continue;
		if (IsBuildable(slot) && choice == nullptr)
		{
			choice = &slot;
			choicePlan = plan;
		}
	}
	if (power != nullptr && (choicePlan == nullptr || powerPlan->name != choicePlan->name) && !powerUnderConstruction)
	{
		choice = power;
		choicePlan = powerPlan;
	}
	if (choice == nullptr)
		return;
	if (!game.world.IsAlive(BuildStructureWithDozer(game, ai, *choicePlan, *choice)))
		return;
	DecrementRebuilds(*choice);
	ai.readyToBuildStructure = false;
	std::int64_t timer = (content.aiData.structureSeconds * Fixed::FromInt(game.step.TicksPerSecond())).Floor();
	const std::int64_t money = game.world.Resource<gp::PlayerMoney>().Balance(ai.player);
	if (money < content.aiData.poor)
		timer = (Fixed::FromInt(timer) / content.aiData.structuresPoorRate).Floor();
	else if (money > content.aiData.wealthy)
		timer = (Fixed::FromInt(timer) / content.aiData.structuresWealthyRate).Floor();
	ai.structureTimer = timer;
	ai.lastBuildingTick = game.tick;
}

// processBaseBuilding (a plain AIPlayer's: the map's own computer players): over its build list in order, what was lost
// is noted (rebuilt only after RebuildDelayTimeSeconds), what stands under construction gets a dozer back to work
// (another found when its builder is gone), what was captured is let go; the first missing entry that may still be built
// is put up with a dozer, and the structure timer starts again (StructureSeconds, faster when poor or wealthy).
inline void ProcessPlainBaseBuilding(GameWorld &game, AiPlayer &ai)
{
	namespace gp = engine::gameplay;
	using namespace ai_base_detail;
	if (!ai.readyToBuildStructure)
		return;
	const auto &content = game.templates.Content();
	for (AiBuildSlot &slot : ai.buildList)
	{
		if (slot.structure.empty())
			continue;
		const content::ObjectDefinition *plan = content.objects.Find(slot.structure);
		if (plan == nullptr)
			continue;
		if (slot.built != ecs::Entity{})
		{
			const ecs::Entity standing = Standing(game, slot);
			if (standing == ecs::Entity{})
			{
				// Destroyed: a GLA rebuild hole it left takes its place in the plan.
				const ecs::Entity prior = slot.built;
				slot.built = {};
				slot.builtTick = game.tick + 1;
				if (const ecs::Entity hole = HoleOf(game, prior); hole != ecs::Entity{})
					slot.built = hole;
			}
			else if (OwnerOf(game, standing) == ai.player)
			{
				if (const auto *building = game.world.Get<gp::UnderConstruction>(standing))
				{
					ecs::Entity dozer = game.world.IsAlive(building->builder) ? building->builder : ecs::Entity{};
					if (dozer == ecs::Entity{})
					{
						dozer = FindDozer(game, ai, slot.location);
						if (dozer == ecs::Entity{})
							continue;
					}
					OrderWork(game, dozer, standing); // aiResumeConstruction
				}
			}
			else
			{
				slot.built = {}; // captured
				slot.builtTick = game.tick + 1;
			}
		}
		if (slot.built == ecs::Entity{} && slot.builtTick > 0)
		{
			const std::uint64_t delay = static_cast<std::uint64_t>(
				(content.aiData.rebuildDelaySeconds * Fixed::FromInt(game.step.TicksPerSecond())).Floor());
			if (slot.builtTick + delay > game.tick)
				continue;
			slot.builtTick = 0; // ready to build
		}
		if (!IsBuildable(slot) || Standing(game, slot) != ecs::Entity{})
			continue;
		if (!game.world.IsAlive(BuildStructureWithDozer(game, ai, *plan, slot)))
			continue;
		DecrementRebuilds(slot);
		ai.readyToBuildStructure = false;
		std::int64_t timer = (content.aiData.structureSeconds * Fixed::FromInt(game.step.TicksPerSecond())).Floor();
		const std::int64_t money = game.world.Resource<gp::PlayerMoney>().Balance(ai.player);
		if (money < content.aiData.poor)
			timer = (Fixed::FromInt(timer) / content.aiData.structuresPoorRate).Floor();
		else if (money > content.aiData.wealthy)
			timer = (Fixed::FromInt(timer) / content.aiData.structuresWealthyRate).Floor();
		ai.structureTimer = timer;
		ai.lastBuildingTick = game.tick;
		break; // only one building per look
	}
}

// doBaseBuilding: while it may build its base, the structure timer counts down (a skirmish player's never above 3 s)
// to readiness; every 2 s (sooner when something asks) it builds what it can (the skirmish or the plain way).
inline void DoBaseBuilding(GameWorld &game, AiPlayer &ai)
{
	if (!game.roster.PlayerAt(ai.player).canBuildBase)
		return;
	const std::int64_t second = game.step.TicksPerSecond();
	if (!ai.readyToBuildStructure)
	{
		if (--ai.structureTimer <= 0)
		{
			ai.readyToBuildStructure = true;
			ai.buildDelay = 0;
		}
		if (ai.skirmish && ai.structureTimer > 3 * second)
			ai.structureTimer = 3 * second;
	}
	if (--ai.buildDelay < 1)
	{
		if (ai.readyToBuildStructure)
		{
			if (ai.skirmish)
				ProcessBaseBuilding(game, ai);
			else
				ProcessPlainBaseBuilding(game, ai);
		}
		if (ai.buildDelay < 1)
			ai.buildDelay = 2 * second;
	}
}

// buildSpecificAIBuilding (SKIRMISH_BUILD_BUILDING): the first entry of that structure not standing and not already
// marked is marked for priority, and the next look comes at once.
inline void BuildSpecificAiBuilding(GameWorld &game, AiPlayer &ai, const std::string &structure)
{
	using namespace ai_base_detail;
	bool marked = false;
	for (AiBuildSlot &slot : ai.buildList)
	{
		if (slot.structure != structure || game.templates.Content().objects.Find(structure) == nullptr)
			continue;
		if (Standing(game, slot) != ecs::Entity{} || slot.priorityBuild)
			continue;
		slot.priorityBuild = true;
		marked = true;
		break;
	}
	if (marked)
		ai.buildDelay = 0;
}

// AIPlayer::isSupplySourceAttacked: at most every ten frames (SCAN_RATE), and only within ten frames of the player last
// being attacked, the first of its things (its teams in order, each newest first) that is a cash generator, dozer or
// harvester hit within the last ten frames; found, it is remembered (m_attackedSupplyCenter). Not yet: a hit that did
// nothing (DamageInfo m_noEffect) is not told apart.
inline bool SupplySourceAttacked(GameWorld &game, AiPlayer &ai)
{
	namespace gp = engine::gameplay;
	constexpr std::uint64_t ScanRate = 10;
	const std::uint64_t now = game.tick;
	ai.attackedSupplyCenter = {};
	if (now < ai.supplyAttackCheckTick || ai.player >= game.roster.PlayerCount())
		return false;
	if (game.roster.PlayerAt(ai.player).attackedTick + ScanRate < now)
		return false;
	ai.supplyAttackCheckTick = now + ScanRate;
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		if (game.roster.TeamAt(team).owner != ai.player)
			continue;
		const auto &members = game.roster.TeamAt(team).members;
		for (auto member = members.rbegin(); member != members.rend(); ++member)
		{
			const auto *ref = game.world.IsAlive(*member) ? game.world.Get<gp::DefinitionRef>(*member) : nullptr;
			const auto *health = ref != nullptr ? game.world.Get<gp::Health>(*member) : nullptr;
			if (health == nullptr)
				continue;
			const content::ObjectDefinition &object = game.templates.DefinitionAt(ref->index);
			if (!object.Is("CASH_GENERATOR") && !object.Is("DOZER") && !object.Is("HARVESTER"))
				continue;
			if (health->lastDamageTick != 0 && health->lastDamageTick + ScanRate > now)
			{
				ai.attackedSupplyCenter = *member;
				return true;
			}
		}
	}
	return false;
}

// findSupplyCenter: the supply warehouse (a SUPPLY_SOURCE structure with a store) nearest this base, not an enemy's,
// holding at least `minimumCash` worth (boxes x ValuePerSupplyBox), with no cash generator of its own within 20 cells of
// its edge, and not nearer the enemy's structures than 60/40; none: the same for half the cash, while above 100.
inline ecs::Entity FindSupplyCenter(GameWorld &game, AiPlayers &ais, AiPlayer &ai, std::int64_t minimumCash)
{
	namespace gp = engine::gameplay;
	using namespace ai_base_detail;
	const auto enemy = AiEnemy(game, ais, ai.player);
	Engine::Math::FixedVector2 enemyCenter;
	if (enemy)
	{
		const auto bounds = PlayerStructureBounds(game, *enemy);
		enemyCenter = {(bounds[0].x + bounds[1].x) / Fixed::FromInt(2), (bounds[0].y + bounds[1].y) / Fixed::FromInt(2)};
	}
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	const std::int64_t boxValue = game.templates.Content().gameData.valuePerSupplyBox;
	const Fixed close = Fixed::FromInt(20 * gp::PathfindCellSize);
	std::vector<std::pair<ecs::Entity, const gp::ResourceStore *>> warehouses;
	ecs::Query<ecs::Read<gp::ResourceStore>> stores(game.world);
	stores.ForEachChunk([&](auto chunk) {
		const auto entities = chunk.Entities();
		const auto boxes = chunk.template Get<gp::ResourceStore>();
		for (std::size_t row = 0; row < entities.size(); ++row)
			warehouses.push_back({entities[row], &boxes[row]});
	});
	std::sort(warehouses.begin(), warehouses.end(), [](const auto &a, const auto &b) { return a.first.index < b.first.index; });
	do
	{
		ecs::Entity best;
		Fixed bestDistance;
		for (const auto &[warehouse, store] : warehouses)
		{
			const content::ObjectDefinition *definition = DefinitionOf(game, warehouse);
			const auto *where = definition != nullptr ? game.world.Get<gp::Transform>(warehouse) : nullptr;
			if (where == nullptr || !definition->Is("STRUCTURE") || !definition->Is("SUPPLY_SOURCE"))
				continue;
			if (static_cast<std::int64_t>(store->boxes) * boxValue < minimumCash)
				continue;
			if (const auto owner = OwnerOf(game, warehouse); owner && relationships != nullptr && relationships->Enemies(ai.player, *owner))
				continue;
			const Fixed reach = close + content::BoundingSphereRadius(definition->geometry);
			bool haveOne = false;
			ai_detail::ForPlayerObjects(game, ai.player, [&](ecs::Entity mine) {
				const content::ObjectDefinition *theirs = DefinitionOf(game, mine);
				const auto *at = theirs != nullptr ? game.world.Get<gp::Transform>(mine) : nullptr;
				if (at == nullptr || !theirs->Is("CASH_GENERATOR") || game.world.Get<gp::OffMap>(mine) != nullptr)
					return;
				haveOne = haveOne || Engine::Math::Distance(at->position.XY(), where->position.XY()) <= reach + content::BoundingSphereRadius(theirs->geometry);
			});
			if (haveOne)
				continue;
			const Fixed distance = Engine::Math::DistanceSquared(where->position.XY(), ai.baseCenter);
			if (enemy && distance * Fixed::FromRatio(4, 10) > Engine::Math::DistanceSquared(where->position.XY(), enemyCenter) * Fixed::FromRatio(6, 10))
				continue;
			if (best == ecs::Entity{} || bestDistance > distance)
			{
				best = warehouse;
				bestDistance = distance;
			}
		}
		if (best != ecs::Entity{})
			return best;
		minimumCash /= 2;
	} while (minimumCash > 100);
	return {};
}

// Player::addToPriorityBuildList: at the front of the plan, marked for priority, built once.
inline void AddToPriorityBuildList(AiPlayer &ai, const std::string &structure, Engine::Math::FixedVector2 at, Engine::Math::Fixed angleDegrees)
{
	AiBuildSlot slot;
	slot.structure = structure;
	slot.location = at;
	slot.angleDegrees = angleDegrees;
	slot.priorityBuild = true;
	slot.rebuilds = 1;
	ai.buildList.insert(ai.buildList.begin(), std::move(slot));
}

// The AI's wiggle for a spot (AIPlayer::buildBySupplies / buildSpecificBuildingNearestTeam): rings of growing size
// around `location` (0 to 2 x SUPPLY_CENTER_CLOSE_DIST, 20 cells, in steps of two cells), each ring's bottom and top
// rows then its left and right columns cell by cell, the first spot where it may be built with a clear path, on legal
// terrain and nothing in the way. (The original let a found top-row or right-column spot be overwritten by the next
// cell's check; the first legal spot is kept.)
inline std::optional<Engine::Math::FixedVector2> WiggleForLegalSpot(GameWorld &game, const content::ObjectDefinition &plan, Engine::Math::FixedVector2 location,
	Engine::Math::TurnAngle angle)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	const auto legal = [&](Engine::Math::FixedVector2 at, std::uint32_t options) { return CheckBuildLocation(game, plan, at, angle, {}, options) == LegalBuild::Ok; };
	constexpr std::uint32_t full = build_check::ClearPath | build_check::TerrainRestrictions | build_check::NoObjectOverlap;
	const Fixed cell = Fixed::FromInt(gp::PathfindCellSize);
	const Fixed limit = Fixed::FromInt(2 * 20 * gp::PathfindCellSize);
	for (Fixed step{}; step < limit; step = step + cell * Fixed::FromInt(2))
	{
		const Fixed half = step / Fixed::FromInt(2);
		for (Fixed x = location.x - half; x <= location.x + half; x = x + cell)
		{
			if (legal({x, location.y - half}, full))
				return Engine::Math::FixedVector2{x, location.y - half};
			if (legal({x, location.y - half + step}, full))
				return Engine::Math::FixedVector2{x, location.y - half + step};
		}
		for (Fixed y = location.y - half; y <= location.y + half; y = y + cell)
		{
			if (legal({location.x - half, y}, full))
				return Engine::Math::FixedVector2{location.x - half, y};
			if (legal({location.x - half + step, y}, full))
				return Engine::Math::FixedVector2{location.x - half + step, y};
		}
	}
	return std::nullopt;
}

// buildBySupplies (AI_PLAYER_BUILD_SUPPLY_CENTER): by the supply warehouse found (for what is not a cash generator,
// the one last built by), a spot is planned: 3 cells back towards the base, or (anything else, likely a defence) the
// warehouse's bounding circle towards the enemy; where it may not stand (objects in the way) the rings around it are
// tried out to 40 cells for a legal spot; it goes at the front of the plan, marked for priority.
inline void BuildBySupplies(GameWorld &game, AiPlayers &ais, AiPlayer &ai, std::int64_t minimumCash, const std::string &thing)
{
	namespace gp = engine::gameplay;
	using namespace ai_base_detail;
	const content::ObjectDefinition *plan = game.templates.Content().objects.Find(thing);
	if (plan == nullptr)
		return;
	ecs::Entity warehouse = FindSupplyCenter(game, ais, ai, minimumCash);
	if (!plan->Is("CASH_GENERATOR") && game.world.IsAlive(ai.currentWarehouse))
		warehouse = ai.currentWarehouse;
	const auto *where = game.world.IsAlive(warehouse) ? game.world.Get<gp::Transform>(warehouse) : nullptr;
	if (where == nullptr)
		return;
	Engine::Math::FixedVector2 location = where->position.XY();
	Engine::Math::FixedVector2 offset = Engine::Math::Normalize(location - ai.baseCenter);
	Fixed radius = Fixed::FromInt(3 * gp::PathfindCellSize);
	if (!plan->Is("CASH_GENERATOR"))
	{
		const auto enemy = SkirmishEnemy(game, ais, ai.player);
		const auto bounds = enemy ? PlayerStructureBounds(game, *enemy) : std::array<Engine::Math::FixedVector2, 2>{};
		offset = Engine::Math::Normalize(location - Engine::Math::FixedVector2{(bounds[0].x + bounds[1].x) / Fixed::FromInt(2), (bounds[0].y + bounds[1].y) / Fixed::FromInt(2)});
		if (const auto *definition = DefinitionOf(game, warehouse))
			radius = content::BoundingSphereRadius(definition->geometry);
	}
	location = location - offset * radius;
	const Engine::Math::TurnAngle angle = Engine::Math::TurnFromDegrees(plan->placementViewAngleDegrees);
	if (CheckBuildLocation(game, *plan, location, angle, {}, build_check::NoObjectOverlap) != LegalBuild::Ok)
		if (const auto found = WiggleForLegalSpot(game, *plan, location, angle))
			location = *found;
	AddToPriorityBuildList(ai, thing, location, plan->placementViewAngleDegrees);
	ai.currentWarehouse = warehouse;
}

// buildSpecificBuildingNearestTeam (AI_PLAYER_BUILD_TYPE_NEAREST_TEAM): where the team is estimated to be (its first
// member's position, getEstimateTeamPosition: the newest member, the team list being prepended), or, where it may not
// stand there (objects in the way), the first legal spot of the wiggle around it; it goes at the front of the plan,
// marked for priority. None found: nothing. (The original built nothing when the team's own spot was legal: its
// `valid` was only set by the wiggle. Here that spot is used.)
inline void BuildNearestTeam(GameWorld &game, AiPlayer &ai, const std::string &thing, std::uint32_t team)
{
	namespace gp = engine::gameplay;
	const content::ObjectDefinition *plan = game.templates.Content().objects.Find(thing);
	if (plan == nullptr || team >= game.roster.TeamCount())
		return;
	const auto &members = game.roster.TeamAt(team).members;
	const auto *first = members.empty() || !game.world.IsAlive(members.back()) ? nullptr : game.world.Get<gp::Transform>(members.back());
	if (first == nullptr)
		return;
	Engine::Math::FixedVector2 location = first->position.XY();
	const Engine::Math::TurnAngle angle = Engine::Math::TurnFromDegrees(plan->placementViewAngleDegrees);
	if (CheckBuildLocation(game, *plan, location, angle, {}, build_check::NoObjectOverlap) != LegalBuild::Ok)
	{
		const auto found = WiggleForLegalSpot(game, *plan, location, angle);
		if (!found)
			return;
		location = *found;
	}
	AddToPriorityBuildList(ai, thing, location, plan->placementViewAngleDegrees);
}

// AISkirmishPlayer::buildAIBaseDefenseStructure (SKIRMISH_BUILD_STRUCTURE_FRONT / _FLANK): the next spot on the circle
// of the base's reach plus SkirmishBaseDefenseExtraDistance, aimed at the approach path (front: "Center<n>"; flank: in
// turn "Backdoor<n>" and "Flank<n>", n the start position; the waypoint of it closest to the base; a front with no path
// aims at the enemy's structures, a flank with none gives up). Each try takes the next angle, alternately left and right
// of the aim by four times the structure's bounding circle over that radius (radians); past 60 degrees it gives up; the
// first spot where it may stand (terrain, no objects) goes at the front of the plan, marked for priority.
inline void BuildAiBaseDefenseStructure(GameWorld &game, AiPlayers &ais, AiPlayer &ai, std::int64_t startIndex, const std::string &structure, bool flank)
{
	using namespace ai_base_detail;
	const content::ObjectDefinition *plan = game.templates.Content().objects.Find(structure);
	if (plan == nullptr)
		return;
	const Fixed sixtyDegrees = Engine::Math::Radians(Engine::Math::TurnFromDegrees(60));
	for (;;)
	{
		std::string label;
		if (flank)
			label = ((ai.flankDefenses & 1) != 0 ? "Flank" : "Backdoor") + std::to_string(startIndex + 1);
		else
			label = "Center" + std::to_string(startIndex + 1);
		Engine::Math::FixedVector2 goal = ai.baseCenter;
		if (const std::uint32_t way = game.waypoints.ClosestOnPath(ai.baseCenter, label); way != gp::WaypointGraph::None)
			goal = game.waypoints.Position(way).XY();
		else
		{
			if (flank)
				return;
			// getMyEnemyPlayerIndex: its current enemy, else the first human player.
			std::optional<std::uint32_t> enemy = ai.enemy;
			for (std::uint32_t player = 0; !enemy && player < game.roster.PlayerCount(); ++player)
				if (game.roster.PlayerAt(player).human)
					enemy = player;
			const auto bounds = enemy ? PlayerStructureBounds(game, *enemy) : std::array<Engine::Math::FixedVector2, 2>{};
			goal = {bounds[0].x + (bounds[1].x - bounds[0].x) / Fixed::FromInt(2), bounds[0].y + (bounds[1].y - bounds[0].y) / Fixed::FromInt(2)};
		}
		const Fixed distance = ai.baseRadius + game.templates.Content().aiData.skirmishBaseDefenseExtraDistance;
		const Engine::Math::FixedVector2 offset = Engine::Math::Normalize(goal - ai.baseCenter) * distance;
		// 2pi * (radius * 4 / (2pi * distance)).
		const Fixed step = distance > Fixed{} ? Fixed::FromInt(4) * content::BoundingSphereRadius(plan->geometry) / distance : Fixed{};
		Fixed angle;
		const auto take = [&](std::size_t left, std::size_t right, std::int32_t selector) {
			if ((selector & 1) != 0)
			{
				ai.defenseAngles[right] = ai.defenseAngles[right] - step;
				angle = ai.defenseAngles[right];
			}
			else
			{
				angle = ai.defenseAngles[left];
				ai.defenseAngles[left] = ai.defenseAngles[left] + step;
			}
		};
		if (flank)
		{
			const std::int32_t selector = ai.flankDefenses >> 1;
			if ((ai.flankDefenses & 1) != 0)
				take(2, 3, selector);
			else
				take(4, 5, selector);
		}
		else
			take(0, 1, ai.frontDefenses);
		if (angle > sixtyDegrees)
			return;
		const Engine::Math::TurnAngle turn = angle < Fixed{} ? Engine::Math::TurnAngle{0u - Engine::Math::TurnFromRadians(Fixed{} - angle).units}
															  : Engine::Math::TurnFromRadians(angle);
		const Fixed s = Engine::Math::Sin(turn), c = Engine::Math::Cos(turn);
		const Engine::Math::FixedVector2 at{ai.baseCenter.x + offset.x * c - offset.y * s, ai.baseCenter.y + offset.y * c + offset.x * s};
		const bool canBuild = CheckBuildLocation(game, *plan, at, Engine::Math::TurnFromDegrees(plan->placementViewAngleDegrees), {},
								  build_check::TerrainRestrictions | build_check::NoObjectOverlap) == LegalBuild::Ok;
		if (flank)
			++ai.flankDefenses;
		else
			++ai.frontDefenses;
		if (canBuild)
		{
			AddToPriorityBuildList(ai, structure, at, plan->placementViewAngleDegrees);
			return;
		}
	}
}

// AISkirmishPlayer::buildAIBaseDefense (SKIRMISH_BUILD_BASE_DEFENSE_FRONT / _FLANK): its side's BaseDefenseStructure1.
inline void BuildAiBaseDefense(GameWorld &game, AiPlayers &ais, AiPlayer &ai, std::int64_t startIndex, bool flank)
{
	if (const content::AiSideInfo *info = game.templates.Content().aiData.Side(ai.side))
		BuildAiBaseDefenseStructure(game, ais, ai, startIndex, info->baseDefenseStructure, flank);
}
}
