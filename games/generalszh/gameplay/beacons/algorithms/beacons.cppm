export module games.generalszh.gameplay.beacons.algorithms.beacons;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.beacons.resources.beacons;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.lifetime.components.lifetime;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.rts.match.resources.match_outcome;
import engine.ecs.query.query;

export namespace generalszh::gameplay
{
namespace beacon_detail
{
inline std::int32_t BeaconsOf(GameWorld &game, std::uint32_t player, std::uint32_t beacon)
{
	namespace gp = engine::gameplay;
	std::int32_t count = 0;
	ecs::Query<ecs::Read<gp::DefinitionRef>, ecs::Read<gp::Owner>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto definitions = chunk.template Get<gp::DefinitionRef>();
		const auto owners = chunk.template Get<gp::Owner>();
		for (std::size_t row = 0; row < definitions.size(); ++row)
			count += definitions[row].index == beacon && owners[row].player == player ? 1 : 0;
	});
	return count;
}
}

// GameLogic::onPlaceBeacon: at the spot (off the map: the nearest point on its edge) the player's side's beacon, on its
// default team, unless its side has none or it has been defeated (Failed, for everyone) or it already has
// MaxBeaconsPerPlayer of them up (countObjectsByThingTemplate: TooMany, for it). Placed: the presentation tells its allies.
inline void PlaceBeacon(GameWorld &game, std::uint32_t player, Engine::Math::FixedVector2 at)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	auto *cues = world.FindResource<BeaconCues>();
	const auto *rules = world.FindResource<BeaconRules>();
	if (cues == nullptr || rules == nullptr || player >= game.roster.PlayerCount())
		return;
	const auto [low, high] = game.ground.Extent();
	Engine::Math::FixedVector3 position{at.x, at.y, game.ground.At(at)};
	if (at.x < low.x || at.y < low.y || at.x > high.x || at.y > high.y)
		position = game.ground.ClosestEdgePoint(at);
	const auto *object = rules->Of(player).empty() ? nullptr : game.templates.Content().objects.Find(rules->Of(player));
	const auto *outcome = world.FindResource<gp::MatchOutcome>();
	const gp::MatchStanding *standing = outcome != nullptr ? outcome->Of(player) : nullptr;
	if (object == nullptr || (standing != nullptr && standing->defeated))
	{
		cues->list.push_back({BeaconCue::Kind::Failed, player, {}, position, {}});
		return;
	}
	const std::uint32_t beacon = game.templates.Definition(*object);
	if (beacon_detail::BeaconsOf(game, player, beacon) >= rules->maxPerPlayer)
	{
		cues->list.push_back({BeaconCue::Kind::TooMany, player, {}, position, {}});
		return;
	}
	const auto found = game.roster.DefaultTeam(player);
	const ecs::Entity made = SpawnObject(game, game.templates.DefinitionAt(beacon).name, position.XY(), {}, found.value_or(0u), {});
	if (!world.IsAlive(made))
		return;
	world.Get<gp::Transform>(made)->position = position;
	cues->list.push_back({BeaconCue::Kind::Placed, player, made, position, {}});
}

// GameLogic::onRemoveBeacon over the player's selection: its own beacons go (destroyObject); another's it hides from
// itself only (hideBeacon, for the presentation). Only what is its controller's side's beacon counts.
inline void RemoveBeacons(GameWorld &game, std::uint32_t player, std::span<const ecs::Entity> selection)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	auto *cues = world.FindResource<BeaconCues>();
	const auto *rules = world.FindResource<BeaconRules>();
	if (cues == nullptr || rules == nullptr)
		return;
	for (const ecs::Entity beacon : selection)
	{
		const auto *owner = world.IsAlive(beacon) ? world.Get<gp::Owner>(beacon) : nullptr;
		const auto *reference = owner != nullptr ? world.Get<gp::DefinitionRef>(beacon) : nullptr;
		if (reference == nullptr || rules->Of(owner->player).empty() || game.templates.DefinitionAt(reference->index).name != rules->Of(owner->player))
			continue;
		if (owner->player == player)
		{
			if (!world.Has<gp::Lifetime>(beacon))
				world.Add<gp::Lifetime>(beacon);
			*world.Get<gp::Lifetime>(beacon) = gp::Lifetime{game.tick, 1, 0};
		}
		else
			cues->list.push_back({BeaconCue::Kind::Hidden, player, beacon, world.Get<gp::Transform>(beacon)->position, {}});
	}
}

// GameLogic::onSetBeaconText: the caption of each of the selection (empty: none), for the presentation.
inline void SetBeaconText(GameWorld &game, std::uint32_t player, std::span<const ecs::Entity> selection, const std::string &text)
{
	auto &world = game.world;
	auto *cues = world.FindResource<BeaconCues>();
	if (cues == nullptr)
		return;
	for (const ecs::Entity beacon : selection)
		if (world.IsAlive(beacon))
			cues->list.push_back({BeaconCue::Kind::Text, player, beacon, {}, text});
}
}
