export module games.generalszh.gameplay.ai.algorithms.ai_superweapon;
import std;
import games.generalszh.gameplay.production.algorithms.build_cost;

export import games.generalszh.gameplay.ai.algorithms.ai_players;
import games.generalszh.gameplay.ai.algorithms.ai_base_building;
import games.generalszh.gameplay.construction.algorithms.build_legality;
import games.generalszh.content.powers.special_powers;
import engine.gameplay.rts.navigation.resources.waypoint_graph;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import games.generalszh.gameplay.powers.algorithms.special_power_launch;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.rts.navigation.definitions.pathfind_cell;

// A computer player aiming its special powers (ScriptActions::doSkirmishFireSpecialPowerAtMostCost,
// AIPlayer::computeSuperweaponTarget, getPlayerSuperweaponValue, calcClosestConstructionZoneLocation;
// AISkirmishPlayer::computeSuperweaponTarget's cluster mines).
export namespace generalszh::gameplay
{
namespace ai_superweapon_detail
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

// The player's teams' members, team by team, newest member first (Player::getPlayerTeams, TeamMemberList).
template<typename Visit>
void ForPlayerObjectsNewestFirst(const GameWorld &game, std::uint32_t player, Visit &&visit)
{
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		const auto &record = game.roster.TeamAt(team);
		if (record.owner != player || !record.alive)
			continue;
		for (auto it = record.members.rbegin(); it != record.members.rend(); ++it)
			if (game.world.IsAlive(*it) && !visit(*it))
				return;
	}
}
}

// getPlayerSuperweaponValue: what of the player's stands within `radius` (at least 4 cells) of `center`, each worth its
// build cost scaled from 1 at the middle to 0.5 at the edge; command centres and superweapons a tenth (a fifth times
// five for a sneak attack), flying aircraft left out; for a sneak attack (no military targets) defences and combat
// units count five times against. Whole: the sum truncated.
inline std::int64_t PlayerSuperweaponValue(GameWorld &game, Engine::Math::FixedVector2 center, std::uint32_t player, Engine::Math::Fixed radius,
	bool includeMilitaryUnits)
{
	using namespace ai_superweapon_detail;
	namespace gp = engine::gameplay;
	radius = std::max(radius, Fixed::FromInt(4 * gp::PathfindCellSize));
	const Fixed significantHeight = Fixed::FromInt(-9) * game.templates.Content().gameData.gravity;
	Fixed cash;
	const CostChanges costs = CostChangesOf(game, player); // calcCostToBuild(pPlayer)
	ForPlayerObjectsNewestFirst(game, player, [&](ecs::Entity object) {
		const auto *ref = game.world.Get<gp::DefinitionRef>(object);
		const auto *where = game.world.Get<gp::Transform>(object);
		if (ref == nullptr || where == nullptr)
			return true;
		const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
		bool negative = false;
		if (!includeMilitaryUnits)
		{
			if (kind.Is("FS_BASE_DEFENSE") || kind.Is("TECH_BASE_DEFENSE"))
				negative = true;
			else if ((kind.Is("VEHICLE") || kind.Is("INFANTRY")) && !kind.Is("DOZER") && !kind.Is("HARVESTER"))
				negative = true;
		}
		else if (kind.Is("AIRCRAFT") && where->position.z - game.ground.At(where->position.XY()) > significantHeight)
			return true; // don't target flying aircraft; ok on the airstrip
		const FixedVector2 delta = center - where->position.XY();
		if (Engine::Math::Dot(delta, delta) >= radius * radius)
			return true;
		const Fixed factor = Fixed::One() - Engine::Math::Length(delta) / (radius * Fixed::FromInt(2));
		Fixed value = Fixed::FromInt(costs.CostOf(kind));
		if (kind.Is("COMMANDCENTER"))
			value = includeMilitaryUnits ? value / Fixed::FromInt(10) : value * Fixed::FromInt(5);
		if (kind.Is("FS_SUPERWEAPON"))
			value = includeMilitaryUnits ? value / Fixed::FromInt(10) : value * Fixed::FromInt(5);
		if (negative)
			cash = cash - factor * value * Fixed::FromInt(5);
		else
			cash = cash + factor * value;
		return true;
	});
	return cash < Fixed{} ? -((Fixed{} - cash).Floor()) : cash.Floor();
}

// computeSuperweaponTarget: over the enemy's structure bounds (none: the map), shrunk by the radius in x, a grid of at
// most 10 x 10 walked from a random corner looks for the most valuable spot (twice the radius); then 11 x 11 looks a
// tenth of the radius apart around it (the original steps both axes with x: a diagonal) average the best of those.
inline std::optional<Engine::Math::FixedVector2> ComputeSuperweaponTarget(GameWorld &game, bool sneakAttack, std::uint32_t enemy, Engine::Math::Fixed radius)
{
	using namespace ai_superweapon_detail;
	auto bounds = PlayerStructureBounds(game, enemy);
	if (bounds[0].x == Fixed{} && bounds[0].y == Fixed{} && bounds[1].x == Fixed{} && bounds[1].y == Fixed{})
	{
		const auto &extents = game.level.terrain.playableExtents;
		bounds[0] = {};
		bounds[1] = extents.empty() ? FixedVector2{} : FixedVector2{Fixed::FromInt(extents.front()[0] * 10), Fixed::FromInt(extents.front()[1] * 10)};
	}
	radius = std::max(radius, Fixed::One());
	bounds[0].x = bounds[0].x + radius;
	bounds[1].x = bounds[1].x - radius;
	if (bounds[1].x < bounds[0].x)
		bounds[0].x = bounds[1].x = (bounds[0].x + bounds[1].x) / Fixed::FromInt(2);
	if (bounds[1].y < bounds[0].y)
		bounds[0].y = bounds[1].y = (bounds[0].y + bounds[1].y) / Fixed::FromInt(2);
	const Fixed width = bounds[1].x - bounds[0].x, height = bounds[1].y - bounds[0].y;
	const std::int64_t xCount = std::min<std::int64_t>((width / radius).Ceil() + 1, 10);
	const std::int64_t yCount = std::min<std::int64_t>((height / radius).Ceil() + 1, 10);
	std::int64_t xDelta = 1, yDelta = 1, xStart = 0, yStart = 0;
	switch (Engine::Math::UniformInt(game.random, 1, 4))
	{
	case 1: break;
	case 2: xDelta = -1, xStart = xCount; break;
	case 3: yDelta = -1, yStart = yCount; break;
	default: xDelta = -1, yDelta = -1, xStart = xCount, yStart = yCount; break;
	}
	std::int64_t cash = -1;
	std::optional<FixedVector2> best;
	std::int64_t xIndex = xStart;
	for (std::int64_t x = 0; x < xCount; ++x, xIndex += xDelta)
	{
		std::int64_t yIndex = yStart;
		for (std::int64_t y = 0; y < yCount; ++y, yIndex += yDelta)
		{
			const FixedVector2 at{bounds[0].x + width * Fixed::FromInt(xIndex) / Fixed::FromInt(xCount), bounds[0].y + height * Fixed::FromInt(yIndex) / Fixed::FromInt(yCount)};
			const std::int64_t value = PlayerSuperweaponValue(game, at, enemy, radius * Fixed::FromInt(2), !sneakAttack);
			if (value > cash)
			{
				cash = value;
				best = at;
			}
		}
	}
	if (!best)
		return std::nullopt;
	cash = -1;
	FixedVector2 veryBest;
	std::int64_t count = 0;
	for (std::int64_t x = 0; x < 11; ++x)
		for (std::int64_t y = 0; y < 11; ++y)
		{
			const Fixed step = radius / Fixed::FromInt(10) * Fixed::FromInt(x - 5);
			const FixedVector2 at{best->x + step, best->y + step};
			const std::int64_t value = PlayerSuperweaponValue(game, at, enemy, radius, !sneakAttack);
			if (value > cash)
			{
				cash = value;
				veryBest = at;
				count = 1;
			}
			else if (value == cash)
			{
				veryBest = veryBest + at;
				++count;
			}
		}
	if (count > 1)
		veryBest = {veryBest.x / Fixed::FromInt(count), veryBest.y / Fixed::FromInt(count)};
	if (cash <= -1)
		return std::nullopt;
	return veryBest;
}

// AIPlayer::calcClosestConstructionZoneLocation: where the power's final product (its ReferenceObject, facing its
// PlacementViewAngle) may stand at or nearest `location`: the spot itself when nothing is in the way, else the AI's
// wiggle (rings out to 40 cells: a clear path, legal terrain, nothing in the way); none: nowhere (the original zeroes the
// spot). (The retail code never marked the spot itself valid, so a spot clear at once was zeroed and the power not fired;
// fixed: it is taken, as the check intends.)
inline std::optional<Engine::Math::FixedVector2> ClosestConstructionZoneLocation(GameWorld &game, const content::ObjectDefinition &plan,
	Engine::Math::FixedVector2 location)
{
	const Engine::Math::TurnAngle angle = Engine::Math::TurnFromDegrees(plan.placementViewAngleDegrees);
	if (CheckBuildLocation(game, plan, location, angle, {}, build_check::NoObjectOverlap) == LegalBuild::Ok)
		return location;
	return WiggleForLegalSpot(game, plan, location, angle);
}

// AISkirmishPlayer::computeSuperweaponTarget for cluster mines (SPECIAL_CLUSTER_MINES, NUKE_SPECIAL_CLUSTER_MINES): the
// entrances to its own base mined: at random (0..2) its "Center", "Flank" or "Backdoor" path (by its start position,
// 1-based), the waypoint of it closest to the base's middle (none: the middle of its enemy's structures, the enemy being
// its current one or else the first human player), and the spot the base's reach out from the middle towards it.
inline Engine::Math::FixedVector2 ClusterMinesTarget(GameWorld &game, const AiPlayer &ai, std::int64_t startIndex)
{
	using Engine::Math::Fixed;
	const std::int64_t mode = Engine::Math::UniformInt(game.random, 0, 2);
	const std::string label = (mode == 1 ? std::string("Flank") : mode == 2 ? std::string("Backdoor") : std::string("Center")) + std::to_string(startIndex + 1);
	Engine::Math::FixedVector2 goal = ai.baseCenter;
	if (const std::uint32_t way = game.waypoints.ClosestOnPath(ai.baseCenter, label); way != engine::gameplay::WaypointGraph::None)
		goal = game.waypoints.Position(way).XY();
	else
	{
		std::optional<std::uint32_t> enemy = ai.enemy;
		for (std::uint32_t player = 0; !enemy && player < game.roster.PlayerCount(); ++player)
			if (game.roster.PlayerAt(player).human)
				enemy = player;
		const auto bounds = enemy ? PlayerStructureBounds(game, *enemy) : std::array<Engine::Math::FixedVector2, 2>{};
		goal = {bounds[0].x + (bounds[1].x - bounds[0].x) / Fixed::FromInt(2), bounds[0].y + (bounds[1].y - bounds[0].y) / Fixed::FromInt(2)};
	}
	return ai.baseCenter + Engine::Math::Normalize(goal - ai.baseCenter) * ai.baseRadius;
}

// Player::computeSuperweaponTarget for the named player: only a computer player aims (none: nothing); a skirmish one
// mines its own base's entrances with cluster mines, else the most valuable spot of the enemy's.
inline std::optional<Engine::Math::FixedVector2> AiSuperweaponTarget(GameWorld &game, const AiPlayer *ai, const content::SpecialPowerTemplate &power,
	std::uint32_t enemy, Engine::Math::Fixed radius, std::int64_t startIndex)
{
	if (ai == nullptr)
		return std::nullopt;
	if (ai->skirmish && (power.type == "SPECIAL_CLUSTER_MINES" || power.type == "NUKE_SPECIAL_CLUSTER_MINES"))
		return ClusterMinesTarget(game, *ai, startIndex);
	return ComputeSuperweaponTarget(game, power.type == "SPECIAL_SNEAK_ATTACK", enemy, radius);
}

// doSkirmishFireSpecialPowerAtMostCost: against the script's player's skirmish enemy, each of the named player's objects
// with the power ready fires it (as a script) at the spot its player aims at within max(50, its RadiusCursorRadius) (the
// original breaks out of a team's members after one, not out of its teams); a sneak attack (SPECIAL_SNEAK_ATTACK) goes
// where its ReferenceObject may stand nearest that spot (calcClosestConstructionZoneLocation), and is not fired when
// there is no such place. `startIndex`: the named player's start position (0-based, getMpStartIndex).
inline void SkirmishFireSpecialPowerAtMostCost(GameWorld &game, AiPlayers &ais, std::uint32_t scriptPlayer, std::uint32_t player, const std::string &name,
	std::int64_t startIndex = 0)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	const auto enemy = SkirmishEnemy(game, ais, scriptPlayer);
	if (!enemy)
		return;
	const auto power = game.templates.Content().powers.Template(name);
	if (!power)
		return;
	const content::SpecialPowerTemplate &info = game.templates.Content().powers.templates[*power];
	const Fixed radius = std::max(Fixed::FromInt(50), info.radiusCursorRadius);
	const bool sneakAttack = info.type == "SPECIAL_SNEAK_ATTACK";
	std::vector<std::uint32_t> teams;
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
		if (game.roster.TeamAt(team).owner == player && game.roster.TeamAt(team).alive)
			teams.push_back(team);
	for (const std::uint32_t team : teams)
	{
		const std::vector<ecs::Entity> members = game.roster.TeamAt(team).members;
		for (auto it = members.rbegin(); it != members.rend(); ++it)
		{
			auto *timers = game.world.IsAlive(*it) ? game.world.Get<gp::SpecialPowerTimers>(*it) : nullptr;
			gp::SpecialPowerTimer *timer = timers != nullptr ? timers->Find(*power) : nullptr;
			if (timer == nullptr || !gp::IsReady(*timer, ClockFor(game, player)))
				continue;
			auto location = AiSuperweaponTarget(game, ais.Of(player), info, *enemy, radius, startIndex);
			if (location && sneakAttack)
			{
				// mod->getReferenceThingTemplate(): the OCLSpecialPower module's ReferenceObject (none: the spot as aimed).
				const auto *ref = game.world.Get<gp::DefinitionRef>(*it);
				const auto ocl = ref != nullptr ? content::FindOclPower(game.templates.DefinitionAt(ref->index), name) : std::nullopt;
				if (const content::ObjectDefinition *product = ocl ? game.templates.Content().objects.Find(ocl->referenceObject) : nullptr)
					location = ClosestConstructionZoneLocation(game, *product, *location);
			}
			// location.lengthSqr() > 0 (its z the ground height there).
			const bool somewhere = location && (location->x != Fixed{} || location->y != Fixed{} || game.ground.At(*location) != Fixed{});
			if (somewhere)
				FireSpecialPower(game, *it, name, *location, true);
			break;
		}
	}
}
}
