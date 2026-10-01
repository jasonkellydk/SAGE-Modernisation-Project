export module games.generalszh.gameplay.construction.algorithms.building;
import games.generalszh.gameplay.world.resources.map_scenery;
import games.generalszh.gameplay.world.resources.match_rules;
import engine.gameplay.rts.navigation.algorithms.clearance;
import std;
import games.generalszh.gameplay.production.algorithms.build_cost;
import games.generalszh.gameplay.powers.algorithms.special_power_state;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.construction.components.builder;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.construction_progress;
import engine.gameplay.rts.construction.resources.sales;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.common.healing.components.healing;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.construction.algorithms.build_legality;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.ecs.query.query;
import engine.gameplay.rts.navigation.resources.navigation_grid;
import engine.gameplay.common.spatial.algorithms.find_position;
import games.generalszh.gameplay.upgrades.components.command_set_override;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.sciences.resources.player_sciences;
import games.generalszh.gameplay.production.algorithms.team_building;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.rts.powers.algorithms.special_power_timing;
import games.generalszh.gameplay.orders.resources.buildable_overrides;

// Zero Hour's builders (DozerAIUpdate / WorkerAIUpdate), between ticks:
//   construct: a dozer or worker of its player's, with the money, places a
//   structure where it is told, if it may go there (build_legality), clearing
//   what may be cleared under it: made on its player's team at 0% with one
//   hit point, awaiting construction, the cost withdrawn; the dozer goes to
//   its dock beside it (the nearest point of its edge) and builds it once
//   stopped within its action tolerance (max(70, its radius + 15) from its
//   edge); the construction system raises it;
//   done: the builder is free again.
export namespace generalszh::gameplay
{
namespace building_detail
{
// ThingTemplate::getMaxSimultaneousOfType: DeterminedBySuperweaponRestriction takes the match's restriction (0: none).
inline std::uint32_t MaxSimultaneous(const GameWorld &game, const content::ObjectDefinition &what)
{
	if (!what.maxSimultaneousBySuperweaponRestriction)
		return what.maxSimultaneousOfType;
	const auto *rules = game.world.FindResource<MatchRules>();
	return rules != nullptr ? rules->superweaponRestriction : 0u;
}

// The builders: a DozerAIUpdate or WorkerAIUpdate.
inline bool IsBuilder(const content::ObjectDefinition &definition)
{
	for (const content::ModuleEntry &module : definition.modules)
		if (module.type == "DozerAIUpdate" || module.type == "WorkerAIUpdate")
			return true;
	return false;
}
}

// DozerAIUpdate::findGoodBuildOrRepairPosition: from the structure towards the builder by half its major radius (3D),
// the first spot findPositionAround finds within 100 of there (rings 5 apart from a random start angle, tryPosition:
// no more than 10 above or below it, not a cliff, impassable or water cell, clear of every object but the builder
// within 5, and one the builder can reach), or that point itself; the end dock point (DOZER_DOCK_POINT_END) is 5
// cells further out from the structure.
struct Docks
{
	Engine::Math::FixedVector2 action;
	Engine::Math::FixedVector2 leave;
};

inline Docks FindDocks(GameWorld &game, ecs::Entity dozer, ecs::Entity structure)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	auto &world = game.world;
	const content::ObjectDefinition &definition = game.templates.DefinitionAt(world.Get<gp::DefinitionRef>(structure)->index);
	const Engine::Math::FixedVector3 theirs = world.Get<gp::Transform>(structure)->position;
	const Engine::Math::FixedVector3 ours = world.Get<gp::Transform>(dozer)->position;
	Engine::Math::FixedVector3 working = theirs;
	if (Engine::Math::LengthSquared(ours - theirs) > Fixed{})
		working = theirs + Engine::Math::Normalize(ours - theirs) * (definition.geometry.majorRadius / Fixed::FromInt(2));
	gp::NavigationGrid &grid = world.Resource<gp::NavigationGrid>();
	const Fixed cell = Fixed::FromInt(gp::PathfindCellSize);
	const auto cellOf = [&](Engine::Math::FixedVector2 point) {
		return std::pair{static_cast<std::int32_t>((point.x / cell).Floor()), static_cast<std::int32_t>((point.y / cell).Floor())};
	};
	const gp::Footprint probe{gp::FootprintShape::Circle, Fixed::FromInt(5), Fixed::FromInt(5)};
	// The things around its search (FindPositionAround: 100 out), gathered once.
	const BuildScene scene = GatherBuildScene(game, working.XY(), Fixed::FromInt(110));
	const auto legal = [&](Engine::Math::FixedVector2 point) {
		if (Engine::Math::Abs(game.ground.At(point) - working.z) > Fixed::FromInt(10))
			return false;
		const auto [x, y] = cellOf(point);
		if (!grid.Contains(x, y))
			return false;
		const gp::PathfindCellType type = grid.Type(x, y);
		if (type == gp::PathfindCellType::Cliff || type == gp::PathfindCellType::Impassable || type == gp::PathfindCellType::Water)
			return false;
		bool clear = true;
		ForEachOverlapping(game, probe, point, {}, [&](ecs::Entity them, const content::ObjectDefinition &) { clear = clear && them == dozer; }, &scene);
		// clientSafeQuickDoesPathExist: the builder can get there (connected zones).
		return clear && gp::QuickPathExists(grid, gp::locomotor_surface::Ground, ours.XY(), point);
	};
	const Engine::Math::TurnAngle start{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 0xFFFFFFFFll))};
	const Engine::Math::FixedVector2 action = gp::FindPositionAround(working.XY(), Fixed{}, Fixed::FromInt(100), start, legal).value_or(working.XY());
	Engine::Math::FixedVector2 leave = action;
	if (Engine::Math::DistanceSquared(action, theirs.XY()) > Fixed{})
		leave = action + Engine::Math::Normalize(action - theirs.XY()) * Fixed::FromInt(5 * gp::PathfindCellSize);
	return {action, leave};
}

// A builder sent to work on `structure`: to its dock (FindDocks), counting as there when within its action tolerance of
// it measured from its bounding sphere (max(MIN_ACTION_TOLERANCE 70, its bounding sphere + SLOP 15)); building it, or
// repairing it.
inline void SendToWork(GameWorld &game, ecs::Entity dozer, ecs::Entity structure, bool repair, Engine::Math::Fixed repairShare)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	auto &world = game.world;
	const Docks docks = FindDocks(game, dozer, structure);
	const Fixed own = content::BoundingSphereRadius(game.templates.DefinitionAt(world.Get<gp::DefinitionRef>(dozer)->index).geometry);
	if (!world.Has<gp::Builder>(dozer))
		world.Add<gp::Builder>(dozer);
	gp::Builder task{structure, docks.action, std::max(Fixed::FromInt(70), own + Fixed::FromInt(15)) + own};
	task.repair = repair ? 1 : 0;
	task.repairShare = repairShare;
	task.leave = docks.leave;
	*world.Get<gp::Builder>(dozer) = task;
	// DozerAIUpdate::newTask(DOZER_TASK_BUILD): even thinking about building it makes it the structure's builder.
	if (!repair)
		if (auto *construction = world.Get<gp::UnderConstruction>(structure))
			construction->builder = dozer;
	OrderMove(game, dozer, docks.action, false, false);
}

// DozerAIUpdateModuleData::m_repairHealthPercentPerSecond as a tick's share (over LOGICFRAMES_PER_SECOND).
inline Engine::Math::Fixed RepairShare(GameWorld &game, const content::ObjectDefinition &dozer)
{
	for (const content::ModuleEntry &module : dozer.modules)
		if (module.block != nullptr && (module.type == "DozerAIUpdate" || module.type == "WorkerAIUpdate"))
			if (const auto *rate = module.block->Find("RepairHealthPercentPerSecond"))
			{
				std::string_view number = rate->Value();
				if (!number.empty() && number.back() == '%')
					number.remove_suffix(1);
				if (const auto percent = engine::config::values::ParseFixed(number))
					return *percent / Engine::Math::Fixed::FromInt(100) / Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(game.step.TicksPerSecond()));
			}
	return {};
}

// ActionManager::canRepairObject: a DOZER builder, not dying nor inside anything, may repair a STRUCTURE not an enemy's,
// alive, not a bridge or rebuild hole, not under construction, hurt.
inline bool MayRepair(GameWorld &game, ecs::Entity dozer, ecs::Entity structure)
{
	namespace gp = engine::gameplay;
	using namespace building_detail;
	auto &world = game.world;
	if (dozer == structure || !world.IsAlive(dozer) || !world.IsAlive(structure) || world.Has<gp::Dying>(dozer) || world.Has<gp::OffMap>(dozer))
		return false;
	const auto *ref = world.Get<gp::DefinitionRef>(dozer);
	const auto *targetRef = world.Get<gp::DefinitionRef>(structure);
	if (ref == nullptr || targetRef == nullptr || world.Get<gp::Owner>(dozer) == nullptr || world.Get<gp::Owner>(structure) == nullptr ||
		world.Get<gp::Transform>(structure) == nullptr)
		return false;
	const content::ObjectDefinition &builder = game.templates.DefinitionAt(ref->index);
	const content::ObjectDefinition &target = game.templates.DefinitionAt(targetRef->index);
	if (!IsBuilder(builder) || !builder.Is("DOZER") || !target.Is("STRUCTURE") || world.Has<gp::UnderConstruction>(structure))
		return false;
	const auto *health = world.Get<gp::Health>(structure);
	return RelationOf(game, dozer, structure) != gp::Relationship::Enemies && health != nullptr && !gp::IsDead(*health) &&
		!world.Has<gp::Dying>(structure) && !target.Is("BRIDGE") && !target.Is("BRIDGE_TOWER") && !target.Is("REBUILD_HOLE") &&
		health->current < health->maximum;
}

// A builder sent to a structure (its player's right-click): to go on building it (canResumeConstructionOf: its
// player's, still under construction, no other builder at it) or to repair it (canRepairObject: a DOZER, not an
// enemy's, alive, a STRUCTURE, not a bridge or rebuild hole, not under construction, hurt; the builder not inside
// anything). False when neither may be.
inline bool OrderWork(GameWorld &game, ecs::Entity dozer, ecs::Entity structure)
{
	namespace gp = engine::gameplay;
	using namespace building_detail;
	auto &world = game.world;
	if (dozer == structure || !world.IsAlive(dozer) || !world.IsAlive(structure) || world.Has<gp::Dying>(dozer) || world.Has<gp::OffMap>(dozer))
		return false;
	const auto *ref = world.Get<gp::DefinitionRef>(dozer);
	const auto *targetRef = world.Get<gp::DefinitionRef>(structure);
	const auto *mine = world.Get<gp::Owner>(dozer);
	const auto *theirs = world.Get<gp::Owner>(structure);
	if (ref == nullptr || targetRef == nullptr || mine == nullptr || theirs == nullptr || world.Get<gp::Transform>(structure) == nullptr)
		return false;
	const content::ObjectDefinition &builder = game.templates.DefinitionAt(ref->index);
	const content::ObjectDefinition &target = game.templates.DefinitionAt(targetRef->index);
	if (!IsBuilder(builder) || !builder.Is("DOZER") || !target.Is("STRUCTURE"))
		return false;
	if (world.Has<gp::UnderConstruction>(structure))
	{
		if (mine->player != theirs->player)
			return false;
		// ActionManager::canResumeConstructionOf: not while its builder (the one it names; this dozer too) has its build
		// task on it: a resume of work already under way does nothing (the AI's processBaseBuilding asks every pass;
		// its dozer keeps the spot it picked).
		const ecs::Entity current = world.Get<gp::UnderConstruction>(structure)->builder;
		if (world.IsAlive(current))
			if (const auto *task = world.Get<gp::Builder>(current); task != nullptr && task->target == structure && task->repair == 0)
				return false;
		SendToWork(game, dozer, structure, false, {});
		return true;
	}
	if (!MayRepair(game, dozer, structure))
		return false;
	SendToWork(game, dozer, structure, true, RepairShare(game, builder));
	return true;
}

enum class CanMake : std::uint8_t
{
	Ok,
	NoPrerequisites,  // not in its builder's command set, not buildable, or what it needs not owned
	MaxedOut,         // MaxSimultaneousOfType reached
	NoMoney,
	BuilderDisabled,  // the builder disabled or unpowered by a script
};

// Player::canBuild: the player may build `what`: allowed to (allowedToBuild: a structure while it may build its base, a
// unit while it may build units); Buildable (a script's status first) not No; Ignore_Prerequisites is enough; Only_By_AI
// only for a computer; its prerequisite objects (one of each group) and sciences owned; not past MaxSimultaneousOfType
// (canBuildMoreOfType: counting, alive, what is it or shares its MaxSimultaneousLinkKey).
// The kinds of thing a player has (alive, not dying), each definition once: OwnedObjects for one player.
inline std::vector<std::string_view> OwnedKinds(GameWorld &game, std::uint32_t player)
{
	namespace gp = engine::gameplay;
	std::vector<std::uint8_t> seen(game.templates.DefinitionCount(), 0);
	std::vector<std::string_view> kinds;
	ecs::Query<ecs::Read<gp::Owner>, ecs::Read<gp::DefinitionRef>, ecs::Exclude<gp::Dying>> things(game.world);
	things.ForEachChunk([&](auto chunk) {
		const auto owners = chunk.template Get<gp::Owner>();
		const auto definitions = chunk.template Get<gp::DefinitionRef>();
		for (std::size_t row = 0; row < owners.size(); ++row)
		{
			const std::uint32_t index = definitions[row].index;
			if (owners[row].player != player || index >= seen.size() || seen[index] != 0)
				continue;
			seen[index] = 1;
			kinds.push_back(game.templates.DefinitionAt(index).name);
		}
	});
	return kinds;
}
// PrerequisitesMet over those kinds: one of each object group.
inline bool HasPrerequisiteObjects(const std::vector<std::string_view> &kinds, const content::ObjectDefinition &unit)
{
	return std::all_of(unit.prerequisiteObjects.begin(), unit.prerequisiteObjects.end(), [&](const std::vector<std::string> &group) {
		return std::any_of(group.begin(), group.end(), [&](const std::string &name) { return std::find(kinds.begin(), kinds.end(), name) != kinds.end(); });
	});
}
// PrerequisitesMet(OwnedObjects(game), player, unit), gathering only that player's things and only when it has
// prerequisite objects at all.
inline bool PlayerHasPrerequisiteObjects(GameWorld &game, std::uint32_t player, const content::ObjectDefinition &unit)
{
	return unit.prerequisiteObjects.empty() || HasPrerequisiteObjects(OwnedKinds(game, player), unit);
}

inline bool PlayerCanBuild(GameWorld &game, std::uint32_t player, const content::ObjectDefinition &what)
{
	namespace gp = engine::gameplay;
	using Buildable = content::ObjectDefinition::Buildable;
	auto &world = game.world;
	if (player >= game.roster.PlayerCount())
		return false;
	const auto &record = game.roster.PlayerAt(player);
	const bool structure = what.Is("STRUCTURE");
	if ((structure && !record.canBuildBase) || (!structure && !record.unitConstructionEnabled))
		return false;
	const Buildable buildable = BuildableOf(world.FindResource<BuildableOverrides>(), what);
	if (buildable == Buildable::No)
		return false;
	if (buildable == Buildable::IgnorePrerequisites)
		return true;
	if (buildable == Buildable::OnlyByAI && !world.Resource<gp::HarvestCatalog>().Computer(player))
		return false;
	if (!PlayerHasPrerequisiteObjects(game, player, what))
		return false;
	for (const std::string &name : what.prerequisiteSciences)
	{
		const auto science = game.templates.Content().Science(name);
		if (!science || !world.Resource<gp::PlayerSciences>().Has(player, *science))
			return false;
	}
	if (const std::uint32_t most = building_detail::MaxSimultaneous(game, what); most > 0)
	{
		std::uint32_t count = 0;
		ecs::Query<ecs::Read<gp::Owner>, ecs::Read<gp::DefinitionRef>, ecs::Exclude<gp::Dying>> things(world);
		things.ForEachChunk([&](auto chunk) {
			const auto owners = chunk.template Get<gp::Owner>();
			const auto definitions = chunk.template Get<gp::DefinitionRef>();
			for (std::size_t row = 0; row < owners.size(); ++row)
			{
				if (owners[row].player != player)
					continue;
				const content::ObjectDefinition &other = game.templates.DefinitionAt(definitions[row].index);
				if (other.name == what.name || (!what.maxSimultaneousLinkKey.empty() && other.maxSimultaneousLinkKey == what.maxSimultaneousLinkKey))
					++count;
			}
		});
		if (count >= most)
			return false;
	}
	return true;
}

// BuildAssistant::canMakeUnit / isPossibleToMakeUnit / Player::canBuild: whether `builder`'s player may have it make
// `what`: its builder not disabled by a script; not past MaxSimultaneousOfType (counting, alive, what is it or shares
// its MaxSimultaneousLinkKey); a UNIT_BUILD or DOZER_CONSTRUCT button of the builder's command set makes it; it is
// Buildable (Ignore_Prerequisites skips what follows; Only_By_AI is not a player's); its prerequisite objects (one of
// each group) and sciences owned; the money there.
inline CanMake CanMakeUnit(GameWorld &game, ecs::Entity builder, const content::ObjectDefinition &what)
{
	namespace gp = engine::gameplay;
	using Buildable = content::ObjectDefinition::Buildable;
	auto &world = game.world;
	const auto *ref = world.IsAlive(builder) ? world.Get<gp::DefinitionRef>(builder) : nullptr;
	const auto *owner = ref != nullptr ? world.Get<gp::Owner>(builder) : nullptr;
	if (owner == nullptr)
		return CanMake::NoPrerequisites;
	const std::uint32_t player = owner->player;
	if (const auto *off = world.Get<gp::Disabled>(builder);
		off != nullptr && (off->mask & (gp::disabled_type::ScriptDisabled | gp::disabled_type::ScriptUnderpowered)) != 0)
		return CanMake::BuilderDisabled;
	if (const std::uint32_t most = building_detail::MaxSimultaneous(game, what); most > 0)
	{
		std::uint32_t count = 0;
		ecs::Query<ecs::Read<gp::Owner>, ecs::Read<gp::DefinitionRef>, ecs::Exclude<gp::Dying>> things(world);
		things.ForEachChunk([&](auto chunk) {
			const auto owners = chunk.template Get<gp::Owner>();
			const auto definitions = chunk.template Get<gp::DefinitionRef>();
			for (std::size_t row = 0; row < owners.size(); ++row)
			{
				if (owners[row].player != player)
					continue;
				const content::ObjectDefinition &other = game.templates.DefinitionAt(definitions[row].index);
				if (other.name == what.name || (!what.maxSimultaneousLinkKey.empty() && other.maxSimultaneousLinkKey == what.maxSimultaneousLinkKey))
					++count;
			}
		});
		if (count >= most)
			return CanMake::MaxedOut;
	}
	const content::ObjectDefinition &maker = game.templates.DefinitionAt(ref->index);
	const auto &lists = game.templates.Content().buildLists;
	// Its command set, or the one an upgrade gave it (getCommandSetString).
	const auto *swapped = world.Get<CommandSetOverride>(builder);
	const auto list = lists.find(swapped != nullptr ? game.templates.CommandSetName(swapped->id) : std::string_view(maker.commandSet));
	if (list == lists.end() || std::find(list->second.begin(), list->second.end(), what.name) == list->second.end())
		return CanMake::NoPrerequisites;
	// Player::canBuild's allowedToBuild: a structure while the player may build its base, a unit while it may build units.
	if (const auto &record = game.roster.PlayerAt(player);
		(what.Is("STRUCTURE") && !record.canBuildBase) || (!what.Is("STRUCTURE") && !record.unitConstructionEnabled))
		return CanMake::NoPrerequisites;
	// Player::canBuild with ThingTemplate::getBuildable (a script's status first): Only_By_AI is a computer's alone.
	const Buildable buildable = BuildableOf(world.FindResource<BuildableOverrides>(), what);
	if (buildable == Buildable::No || (buildable == Buildable::OnlyByAI && !world.Resource<gp::HarvestCatalog>().Computer(player)))
		return CanMake::NoPrerequisites;
	if (buildable != Buildable::IgnorePrerequisites)
	{
		if (!PlayerHasPrerequisiteObjects(game, player, what))
			return CanMake::NoPrerequisites;
		for (const std::string &name : what.prerequisiteSciences)
		{
			const auto science = game.templates.Content().Science(name);
			if (!science || !world.Resource<gp::PlayerSciences>().Has(player, *science))
				return CanMake::NoPrerequisites;
		}
	}
	if (world.Resource<gp::PlayerMoney>().Balance(player) < CostToBuild(game, player, what))
		return CanMake::NoMoney;
	return CanMake::Ok;
}

// DozerAIUpdate::construct: the structure placed, or none (not a builder, not a structure, not enough money). A rebuild
// (a rebuild hole's worker: isRebuild) skips the checks and costs nothing.
inline ecs::Entity BeginConstruction(GameWorld &game, ecs::Entity dozer, const std::string &structure, Engine::Math::FixedVector2 at,
	Engine::Math::TurnAngle facing, bool rebuild = false)
{
	namespace gp = engine::gameplay;
	using namespace building_detail;
	using Engine::Math::Fixed;
	auto &world = game.world;
	if (!world.IsAlive(dozer) || world.Has<gp::Dying>(dozer))
		return {};
	const auto *ref = world.Get<gp::DefinitionRef>(dozer);
	const auto *owner = world.Get<gp::Owner>(dozer);
	const content::ObjectDefinition *what = game.templates.Content().objects.Find(structure);
	if (ref == nullptr || owner == nullptr || what == nullptr || !IsBuilder(game.templates.DefinitionAt(ref->index)) || !what->Is("STRUCTURE"))
		return {};
	const std::uint32_t player = owner->player;
	if (player >= game.roster.PlayerCount())
		return {};
	const auto team = game.roster.DefaultTeam(player);
	// Whether it may make it (canMakeUnit), and where (isLocationLegalToBuild), before any money changes hands; a computer
	// player's dozer just builds ("The ai will validate, or cheat").
	if (!team)
		return {};
	if (!rebuild && game.roster.PlayerAt(player).human &&
		(CanMakeUnit(game, dozer, *what) != CanMake::Ok || CheckBuildLocation(game, *what, at, facing, dozer) != LegalBuild::Ok))
		return {};
	// buildObjectNow: clearRemovableForConstruction (shrubbery and the like under it go), then moveObjectsForConstruction
	// (its player's, allies' and neutrals' units under it move out; anything else there refuses a human's build), then
	// the money (DozerAIUpdate::construct). A rebuild (RebuildHoleBehavior's worker: construct directly) moves no one.
	if (auto removable = RemovableUnder(game, *what, at, facing); !removable.empty())
		RetireNow(game, std::move(removable));
	// removeTreesAndPropsForConstruction: the client's trees and props under it go too.
	world.Resource<SceneryClearings>().list.push_back({at, facing, FootprintOf(*what)});
	if (!rebuild)
	{
		const ConstructionMoves out = MoveObjectsForConstruction(game, *what, at, facing, player);
		for (const auto &[unit, to] : out.moves)
			OrderMove(game, unit, to, false, false); // CMD_FROM_AI, even if sleeping
		if (!out.clear && game.roster.PlayerAt(player).human)
			return {};
	}
	if (!rebuild)
		world.Resource<gp::PlayerMoney>().Withdraw(player, CostToBuild(game, player, *what));
	const ecs::Entity placed = SpawnObject(game, structure, at, facing, *team, "", true, true);
	if (!world.IsAlive(placed))
		return {};
	// Newly constructed objects start at 0% and one hit point.
	if (!world.Has<gp::ConstructionProgress>(placed))
		world.Add<gp::ConstructionProgress>(placed);
	world.Get<gp::ConstructionProgress>(placed)->percent = Fixed{};
	world.Get<gp::ConstructionProgress>(placed)->rebuild = rebuild ? 1u : 0u;
	if (auto *health = world.Get<gp::Health>(placed))
		health->current = Fixed::One();
	world.Add<gp::UnderConstruction>(placed);
	world.Get<gp::UnderConstruction>(placed)->builder = dozer; // setBuilder
	// Made under construction (ThingFactory::newObject with OBJECT_STATUS_UNDER_CONSTRUCTION): its power modules' countdowns
	// do not start yet (SpecialPowerModule's constructor).
	if (auto *timers = world.Get<gp::SpecialPowerTimers>(placed))
	{
		const gp::PowerClock clock{world.Resource<gp::SpecialPowerRules>(), world.Resource<gp::SharedPowerTimers>(), player, game.tick};
		for (std::uint32_t index = 0; index < timers->count; ++index)
		{
			gp::SpecialPowerTimer &timer = timers->timers[index];
			timer = {timer.power, 0, 0, 0, 0, timer.flags, 0};
			gp::StartSpecialPower(timer, true, clock);
		}
	}
	// BaseRegenerateUpdate waits until it stands.
	if (auto *regen = world.Get<gp::SelfHealing>(placed); regen != nullptr && regen->WaitsWhileNotStanding())
		regen->waiting = 1;
	// calcTimeToBuild: BuildTime * LOGICFRAMES_PER_SECOND, as an Int.
	world.Get<gp::UnderConstruction>(placed)->buildTicks = static_cast<std::uint64_t>(std::max<std::int64_t>(
		(what->buildTimeSeconds * Fixed::FromInt(static_cast<std::int64_t>(game.step.TicksPerSecond()))).Floor(), 1));
	SendToWork(game, dozer, placed, false, {});
	return placed;
}
}
