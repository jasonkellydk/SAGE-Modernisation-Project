export module games.generalszh.gameplay.ai.algorithms.ai_players;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.ai.resources.ai_players;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import engine.gameplay.common.health.components.inactive_body;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.rts.death.components.dying;
import Engine.Core.Math.FixedAngle;
import engine.gameplay.common.identity.components.object_id;
import engine.gameplay.rts.production.components.production_queue;
import games.generalszh.gameplay.world.resources.solo_play;
import games.generalszh.gameplay.score.algorithms.scoring;

// The computer players' start (AISkirmishPlayer::newMap, adjustBuildList, AIPlayer::computeCenterAndRadiusOfBase,
// buildStructureNow) and the enemy each picks (AISkirmishPlayer::acquireEnemy / getAiEnemy,
// ScriptEngine::getSkirmishEnemyPlayer).
export namespace generalszh::gameplay
{
namespace ai_detail
{
// Each living object of the player's (its teams' members: Player::iterateObjects), in team order.
template<typename Visit>
void ForPlayerObjects(const GameWorld &game, std::uint32_t player, Visit &&visit)
{
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		const auto &record = game.roster.TeamAt(team);
		if (record.owner != player)
			continue;
		for (const ecs::Entity member : record.members)
			if (game.world.IsAlive(member))
				visit(member);
	}
}

inline const content::ObjectDefinition *DefinitionOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.Get<engine::gameplay::DefinitionRef>(entity);
	return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
}

// isEffectivelyDead: dying, or an InactiveBody (effectively dead from the start).
inline bool EffectivelyDead(const GameWorld &game, ecs::Entity entity)
{
	return game.world.Get<engine::gameplay::Dying>(entity) != nullptr || game.world.Get<engine::gameplay::InactiveBody>(entity) != nullptr;
}

// Squared lengths in 1/256ths of a unit, whole: the original's float distances squared (up to HUGE_DIST squared) fit.
inline std::int64_t Squared(Engine::Math::Fixed dx, Engine::Math::Fixed dy)
{
	const std::int64_t x = dx.Raw() >> 8, y = dy.Raw() >> 8;
	return x * x + y * y;
}
inline std::int64_t SquaredUnits(std::int64_t units) { return units * units * 256 * 256; }
}

// Player::hasAnyObjects / hasAnyUnits / hasAnyBuildFacility (effectively dead ones do not count for the first two).
inline bool HasAnyObjects(const GameWorld &game, std::uint32_t player)
{
	bool any = false;
	ai_detail::ForPlayerObjects(game, player, [&](ecs::Entity entity) { any = any || !ai_detail::EffectivelyDead(game, entity); });
	return any;
}
inline bool HasAnyUnits(const GameWorld &game, std::uint32_t player)
{
	bool any = false;
	ai_detail::ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		const auto *definition = ai_detail::DefinitionOf(game, entity);
		any = any || (!ai_detail::EffectivelyDead(game, entity) && definition != nullptr && !definition->Is("STRUCTURE"));
	});
	return any;
}
inline bool HasAnyBuildFacility(const GameWorld &game, std::uint32_t player)
{
	bool any = false;
	ai_detail::ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		const auto *definition = ai_detail::DefinitionOf(game, entity);
		any = any || (definition != nullptr && definition->buildFacility);
	});
	return any;
}

// Player::countBuildings (its members that are structures) / countObjects(MP_COUNT_FOR_VICTORY and STRUCTURE): dead
// ones still on their teams count.
inline std::int64_t CountBuildings(const GameWorld &game, std::uint32_t player, bool factionOnly)
{
	std::int64_t count = 0;
	ai_detail::ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		const auto *definition = ai_detail::DefinitionOf(game, entity);
		if (definition != nullptr && definition->Is("STRUCTURE") && (!factionOnly || definition->Is("MP_COUNT_FOR_VICTORY")))
			++count;
	});
	return count;
}

// AIPlayer::getPlayerStructureBounds: the box around the player's structures (none: all zero).
inline std::array<Engine::Math::FixedVector2, 2> PlayerStructureBounds(const GameWorld &game, std::uint32_t player)
{
	std::array<Engine::Math::FixedVector2, 2> bounds{};
	bool first = true;
	ai_detail::ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		const auto *definition = ai_detail::DefinitionOf(game, entity);
		const auto *at = game.world.Get<engine::gameplay::Transform>(entity);
		if (definition == nullptr || at == nullptr || !definition->Is("STRUCTURE"))
			return;
		const auto position = at->position.XY();
		if (first)
		{
			bounds = {position, position};
			first = false;
			return;
		}
		bounds[0] = {std::min(bounds[0].x, position.x), std::min(bounds[0].y, position.y)};
		bounds[1] = {std::max(bounds[1].x, position.x), std::max(bounds[1].y, position.y)};
	});
	return bounds;
}

// computeCenterAndRadiusOfBase: the middle of the plan's structures (those that exist), and the farthest reach of one
// (its offset from the middle, each axis grown by 0.4 of its bounding circle).
inline void ComputeBase(const GameWorld &game, AiPlayer &ai)
{
	using Engine::Math::Fixed;
	Engine::Math::FixedVector2 total;
	std::int64_t count = 0;
	const auto &objects = game.templates.Content().objects;
	for (const AiBuildSlot &slot : ai.buildList)
		if (objects.Find(slot.structure) != nullptr)
		{
			total = total + slot.location;
			++count;
		}
	if (count > 0)
		total = {total.x / Fixed::FromInt(count), total.y / Fixed::FromInt(count)};
	ai.baseCenterSet = count > 0;
	ai.baseCenter = total;
	std::int64_t farthest = 0;
	for (const AiBuildSlot &slot : ai.buildList)
		if (const auto *plan = objects.Find(slot.structure))
		{
			const Fixed grow = content::BoundingSphereRadius(plan->geometry) * Fixed::FromRatio(4, 10);
			const Fixed dx = Engine::Math::Abs(slot.location.x - ai.baseCenter.x) + grow;
			const Fixed dy = Engine::Math::Abs(slot.location.y - ai.baseCenter.y) + grow;
			farthest = std::max(farthest, ai_detail::Squared(dx, dy));
		}
	ai.baseRadius = Engine::Math::Sqrt(Fixed::FromRaw(farthest)); // (256 v)^2 as raw is v^2
}

// checkForSupplyCenter: a structure with a SupplyCenterDockUpdate makes its entry a supply building wanting its side's
// ResourceGatherers for the player's difficulty, plus one (the free one that comes with it, still due: -1 had).
inline void CheckForSupplyCenter(const GameWorld &game, const AiPlayer &ai, AiBuildSlot &slot, ecs::Entity structure)
{
	const content::ObjectDefinition *definition = ai_detail::DefinitionOf(game, structure);
	if (definition == nullptr ||
		std::none_of(definition->modules.begin(), definition->modules.end(), [](const content::ModuleEntry &module) { return module.type == "SupplyCenterDockUpdate"; }))
		return;
	std::int32_t desired = 0;
	if (const content::AiSideInfo *info = game.templates.Content().aiData.Side(ai.side))
		desired = info->resourceGatherers[std::min<std::size_t>(ai.difficulty, 2)];
	slot.supplyBuilding = true;
	slot.currentGatherers = -1;
	slot.desiredGatherers = desired + 1;
}

// AISkirmishPlayer::newMap for a computer player: its side's skirmish build list (AIData), moved to its start
// (adjustBuildList: its starting command centre goes and stands again as the plan's first, initially built entry;
// the plan turned by 135 degrees about its command centre and put where that stood; only when the plan's first entry
// is a command centre, as the original checks it for every entry), the base worked out, then what is initially built
// put up at once (buildStructureNow) and every other entry given one more rebuild (its first build uses one).
inline void SetUpSkirmishAi(GameWorld &game, AiPlayers &ais, std::uint32_t player, std::uint8_t difficulty, std::string side, std::uint32_t defaultTeam)
{
	using Engine::Math::Fixed;
	AiPlayer ai;
	ai.player = player;
	ai.difficulty = difficulty;
	ai.side = std::move(side);
	ai.teamSeconds = game.templates.Content().aiData.teamSeconds.Floor();
	const auto &content = game.templates.Content();
	if (const content::AiBuildList *plan = content.aiData.BuildList(ai.side))
		for (const content::AiBuildListEntry &entry : plan->structures)
			ai.buildList.push_back({entry.structure, entry.location, entry.angleDegrees, entry.rebuilds, entry.initiallyBuilt, entry.automaticallyBuild});

	// adjustBuildList.
	std::optional<Engine::Math::FixedVector2> start;
	ecs::Entity startingCenter;
	ai_detail::ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		const auto *definition = ai_detail::DefinitionOf(game, entity);
		const auto *at = game.world.Get<engine::gameplay::Transform>(entity);
		if (!start && definition != nullptr && at != nullptr && definition->Is("COMMANDCENTER"))
		{
			start = at->position.XY();
			startingCenter = entity;
		}
	});
	if (start)
	{
		ScoreObjectUnbuilt(game, player, startingCenter); // Player::onStructureUndone
		RetireNow(game, {startingCenter}, engine::gameplay::Departure::Removed);
		Engine::Math::FixedVector2 buildAt;
		for (AiBuildSlot &slot : ai.buildList)
			if (const auto *plan = content.objects.Find(slot.structure); plan != nullptr && plan->Is("COMMANDCENTER"))
			{
				buildAt = slot.location;
				slot.initiallyBuilt = true;
			}
		// RotateSkirmishBases would turn it further by the start's ninth of the map; the shipped data does not.
		const Engine::Math::FixedVector2 turn = Engine::Math::Direction(Engine::Math::TurnFromDegrees(135));
		const auto *first = ai.buildList.empty() ? nullptr : content.objects.Find(ai.buildList.front().structure);
		if (first != nullptr && first->Is("COMMANDCENTER"))
			for (AiBuildSlot &slot : ai.buildList)
			{
				const Engine::Math::FixedVector2 offset = slot.location - buildAt;
				slot.location = Engine::Math::FixedVector2{offset.x * turn.x - offset.y * turn.y, offset.y * turn.x + offset.x * turn.y} + *start;
			}
	}
	ComputeBase(game, ai);

	for (AiBuildSlot &slot : ai.buildList)
	{
		if (content.objects.Find(slot.structure) == nullptr)
			continue;
		if (!slot.initiallyBuilt)
		{
			++slot.rebuilds;
			continue;
		}
		const ecs::Entity built = SpawnObject(game, slot.structure, slot.location, Engine::Math::TurnFromDegrees(slot.angleDegrees), defaultTeam, "");
		if (game.world.IsAlive(built))
		{
			slot.built = built;
			slot.builtTick = game.tick + 1;
			OnBuildComplete(game, built); // BuildAssistant::buildObjectNow
			ScoreStructureComplete(game, built, false);
			CheckForSupplyCenter(game, ai, slot, built);
		}
	}
	// AISkirmishPlayer: unit building on from the start.
	game.roster.PlayerAt(player).unitConstructionEnabled = true;
	ais.players.push_back(std::move(ai));
}

// AIPlayer::newMap for a computer player of the map's own (not a skirmish seat): its build list is the map's plan for
// it with the factories it was given put in front (Player::addToBuildList, walking its objects newest first: the oldest
// ends up first; they cannot be rebuilt); its base's middle and reach; the plan's initially built structures put up
// now on its default team (buildStructureNow, named as planned), the rest given one more rebuild. Unit building starts
// off (a script turns it on).
inline void SetUpAi(GameWorld &game, AiPlayers &ais, std::uint32_t player, std::uint8_t difficulty,
	const std::vector<engine::level::PlannedPlacement> &plan, std::uint32_t defaultTeam)
{
	AiPlayer ai;
	ai.player = player;
	ai.difficulty = difficulty;
	ai.skirmish = false;
	ai.teamSeconds = game.templates.Content().aiData.teamSeconds.Floor();
	for (const engine::level::PlannedPlacement &planned : plan)
	{
		AiBuildSlot slot;
		slot.structure = planned.type;
		slot.location = planned.position.XY();
		slot.angleDegrees = Engine::Math::Degrees(planned.orientation);
		slot.rebuilds = planned.rebuilds;
		slot.initiallyBuilt = planned.initiallyPlaced;
		ai.buildList.push_back(std::move(slot));
	}
	std::vector<std::pair<std::uint32_t, ecs::Entity>> factories;
	ai_detail::ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		const auto *id = game.world.Get<engine::gameplay::ObjectId>(entity);
		if (id != nullptr && game.world.Get<engine::gameplay::ProductionQueue>(entity) != nullptr)
			factories.emplace_back(id->value, entity);
	});
	std::sort(factories.begin(), factories.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
	for (const auto &[id, factory] : factories)
	{
		AiBuildSlot slot;
		slot.structure = ai_detail::DefinitionOf(game, factory)->name;
		const auto &at = *game.world.Get<engine::gameplay::Transform>(factory);
		slot.location = at.position.XY();
		slot.angleDegrees = Engine::Math::Degrees(at.facing);
		slot.rebuilds = 0;
		slot.built = factory;
		ai.buildList.insert(ai.buildList.begin(), std::move(slot));
	}
	ComputeBase(game, ai);
	const std::size_t planned = plan.size();
	for (std::size_t index = ai.buildList.size() - planned; index < ai.buildList.size(); ++index)
	{
		AiBuildSlot &slot = ai.buildList[index];
		if (game.templates.Content().objects.Find(slot.structure) == nullptr)
			continue;
		if (!slot.initiallyBuilt)
		{
			++slot.rebuilds;
			continue;
		}
		const auto &source = plan[index - (ai.buildList.size() - planned)];
		const ecs::Entity built = SpawnObject(game, slot.structure, slot.location, source.orientation, defaultTeam, source.name);
		if (game.world.IsAlive(built))
		{
			slot.built = built;
			slot.builtTick = game.tick + 1;
			OnBuildComplete(game, built); // BuildAssistant::buildObjectNow
			ScoreStructureComplete(game, built, false);
			CheckForSupplyCenter(game, ai, slot, built);
		}
	}
	game.roster.PlayerAt(player).unitConstructionEnabled = false;
	ais.players.push_back(std::move(ai));
}

// acquireEnemy: the current enemy stays while it still has units and something to build with; else each enemy with
// anything left is weighed by how far the middle of its structures is from this base (a crippled one: half of
// HUGE_DIST squared), plus 500 squared for each other computer player already after it and less 25 squared for each
// one after this player; the nearest (the first of equals) is taken.
inline void AcquireEnemy(const GameWorld &game, AiPlayers &ais, AiPlayer &ai)
{
	using Engine::Math::Fixed;
	if (ai.enemy && HasAnyUnits(game, *ai.enemy) && HasAnyBuildFacility(game, *ai.enemy))
		return;
	const auto *relationships = game.world.FindResource<engine::gameplay::Relationships>();
	if (relationships == nullptr)
		return;
	const std::int64_t huge = ai_detail::SquaredUnits(1000000);
	std::optional<std::uint32_t> best;
	std::int64_t bestDistance = huge;
	for (std::uint32_t other = 0; other < game.roster.PlayerCount(); ++other)
	{
		if (!relationships->Enemies(ai.player, other) || !HasAnyObjects(game, other))
			continue;
		const bool crippled = !HasAnyUnits(game, other) || !HasAnyBuildFacility(game, other);
		const auto bounds = PlayerStructureBounds(game, other);
		const Engine::Math::FixedVector2 middle{bounds[0].x + (bounds[1].x - bounds[0].x) / Fixed::FromInt(2),
			bounds[0].y + (bounds[1].y - bounds[0].y) / Fixed::FromInt(2)};
		std::int64_t distance = ai_detail::Squared(middle.x - ai.baseCenter.x, middle.y - ai.baseCenter.y);
		if (crippled)
			distance = huge / 2;
		for (std::uint32_t some = 0; some < game.roster.PlayerCount(); ++some)
		{
			if (some == other)
				continue;
			const AiPlayer *theirs = ais.Of(some);
			if (theirs != nullptr && theirs->enemy == other)
				distance += ai_detail::SquaredUnits(500);
			if (theirs != nullptr && theirs->enemy == ai.player)
				distance = std::max<std::int64_t>(distance - ai_detail::SquaredUnits(25), 0);
		}
		if (distance < bestDistance)
		{
			best = other;
			bestDistance = distance;
		}
	}
	if (best && best != ai.enemy)
		ai.enemy = best;
}

// getAiEnemy: looked at again once 5 s have passed since the last look.
inline std::optional<std::uint32_t> AiEnemy(const GameWorld &game, AiPlayers &ais, std::uint32_t player)
{
	AiPlayer *ai = ais.Of(player);
	if (ai == nullptr)
		return std::nullopt;
	if (game.tick >= ai->checkEnemyAt)
	{
		ai->checkEnemyAt = game.tick + 5 * game.step.TicksPerSecond();
		AcquireEnemy(game, ais, *ai);
	}
	return ai->enemy;
}

// getSkirmishEnemyPlayer: the player's computer enemy, else the first human player (in a Generals' Challenge not the
// placeholder "ThePlayer").
inline std::optional<std::uint32_t> SkirmishEnemy(const GameWorld &game, AiPlayers &ais, std::uint32_t player)
{
	if (const auto enemy = AiEnemy(game, ais, player))
		return enemy;
	const auto *solo = game.world.FindResource<SoloPlay>();
	const bool challenge = solo != nullptr && solo->singlePlayer && solo->challenge;
	for (std::uint32_t other = 0; other < game.roster.PlayerCount(); ++other)
		if (game.roster.PlayerAt(other).human && !(challenge && game.roster.PlayerAt(other).name == "ThePlayer"))
			return other;
	return std::nullopt;
}
}
