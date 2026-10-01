export module games.generalszh.gameplay.beacons.resources.beacons;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Multiplayer beacons (GameLogic::onPlaceBeacon / onRemoveBeacon / onSetBeaconText).
//   BeaconRules: each player's beacon (its PlayerTemplate's BeaconName; empty: its side has none; its definition is
//     interned only as one is placed, so a checkpoint's template history stays the simulation's own) and how many each
//     may have up (MultiplayerSettings MaxBeaconsPerPlayer).
//   BeaconCues: the tick's beacon feedback for the presentation (the original's client side of those handlers): placed
//     (a beacon up for `player`), too many (its player already has as many as it may), failed (no beacon or a defeated
//     player: shown to every one), hidden (a player told another's beacon to go: hidden from `player` only) and text (a
//     beacon's caption set). Cleared as the tick starts; never read by the simulation.
export namespace generalszh::gameplay
{
struct BeaconRules
{
	std::vector<std::string> beaconOf; // by player
	std::int32_t maxPerPlayer{3};

	std::string_view Of(std::uint32_t player) const noexcept { return player < beaconOf.size() ? std::string_view(beaconOf[player]) : std::string_view{}; }
};

struct BeaconCue
{
	enum class Kind : std::uint8_t
	{
		Placed,
		TooMany,
		Failed,
		Hidden,
		Text,
	};
	Kind kind{Kind::Placed};
	std::uint32_t player{0};
	ecs::Entity beacon;
	Engine::Math::FixedVector3 at;
	std::string text; // UTF-8
};

struct BeaconCues
{
	std::vector<BeaconCue> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::BeaconRules>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.beacon_rules";
};
template<>
struct ResourceTraits<generalszh::gameplay::BeaconCues>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.beacon_cues";
};
}
