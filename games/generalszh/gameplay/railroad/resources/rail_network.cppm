export module games.generalszh.gameplay.railroad.resources.rail_network;
import std;

export import Engine.Core.Math.FixedVector;
export import engine.ecs.core.entity;
import engine.ecs.system.system;

// What trains run on.
//   RailWaypoints: the level's waypoints as the original's TerrainLogic keeps them (made once when the session starts):
//     in its list order (each waypoint put at the head as the map is read: the map's order reversed), each on the ground
//     (addWaypoint snaps its z), with its id, its first link (getLink(0): the links in the order the map gives them, a
//     bidirectional waypoint's link also made the other way at that moment; addWaypointLink) and what its name ends
//     with (Tunnel, Station, Disembark, PingPong).
//   RailTracks: the tracks locomotives laid (TrainTrack, loadTrackData), each from its anchor waypoint along the first
//     links: a point per waypoint with how far along the track it is, its handle (the waypoint before it: the first two
//     share the anchor's) and what it does to a train. A track is the same for the same anchor on the same level: it is
//     derived, made again after a checkpoint when a train needs it.
//   RailroadRequests: locomotives whose carriages are to be made from their templates after the tick (createCarriages).
export namespace generalszh::gameplay
{
namespace rail_point
{
inline constexpr std::uint8_t Tunnel = 1u << 0;
inline constexpr std::uint8_t Station = 1u << 1;
inline constexpr std::uint8_t Disembark = 1u << 2;
inline constexpr std::uint8_t PingPong = 1u << 3;
}

struct RailWaypoints
{
	static constexpr std::uint32_t None = 0xFFFFFFFFu;
	std::vector<std::uint32_t> ids;
	std::vector<Engine::Math::FixedVector3> positions;
	std::vector<std::uint32_t> next; // the index of its first link (None: none)
	std::vector<std::uint8_t> kinds; // rail_point bits its name gives

	std::uint32_t IndexOf(std::uint32_t id) const noexcept
	{
		for (std::uint32_t index = 0; index < ids.size(); ++index)
			if (ids[index] == id)
				return index;
		return None;
	}
};

struct RailTrack
{
	std::uint32_t anchor{RailWaypoints::None}; // the anchor waypoint's id
	std::uint32_t first{0};
	std::uint32_t count{0};
	bool looping{false};
	Engine::Math::Fixed length;
};

struct RailTracks
{
	std::vector<RailTrack> tracks;
	std::vector<Engine::Math::FixedVector3> positions;
	std::vector<Engine::Math::Fixed> along; // distance from the first point
	std::vector<std::uint32_t> handles;
	std::vector<std::uint8_t> kinds;

	const RailTrack *Of(std::uint32_t anchor) const noexcept
	{
		for (const RailTrack &track : tracks)
			if (track.anchor == anchor)
				return &track;
		return nullptr;
	}
};

// A kind of train car (RailroadBehavior's module data, RailroadContent) with its geometry's major radius (half the
// length of track it takes up).
struct RailroadConfig
{
	bool present{false};
	bool locomotive{false};
	std::vector<std::string> carriages;
	bool firstCarriageKnown{false}; // its first carriage template exists (createCarriages hitches nothing otherwise)
	Engine::Math::Fixed radius;
	Engine::Math::Fixed runningGarrisonSpeedMax;
	Engine::Math::Fixed killSpeedMin;
	Engine::Math::Fixed speedMax;
	Engine::Math::Fixed acceleration;
	Engine::Math::Fixed braking;
	Engine::Math::Fixed friction;
	std::int32_t waitAtStationTicks{150};
	// Its sounds (RunningSound, ClicketyClackSound, WhistleSound, Big/SmallMetalBounceSound, MeatyBounceSound).
	std::string runningSound, clicketyClackSound, whistleSound, bigMetalSound, smallMetalSound, meatySound;
};

// The tick's train sounds for the presentation to play (addAudioEvent from RailroadBehavior's update and onCollide):
// a car's whistle, its clickety-clack coming into a new stretch of track (at its speed / 10), an impact on what it hit.
// `definition` is the car's; `sound` the event (an impact's already chosen). Cleared as the tick starts.
struct RailroadCue
{
	enum class Kind : std::uint8_t
	{
		Whistle,
		ClicketyClack,
		Impact,
	};
	Kind kind{Kind::Whistle};
	ecs::Entity car;
	Engine::Math::FixedVector3 at;
	Engine::Math::Fixed volume{Engine::Math::Fixed::One()};
	std::uint32_t player{0xFFFFFFFFu}; // the one it is played for (an impact: its victim's controller)
	std::string sound;
};

struct RailroadCues
{
	std::vector<RailroadCue> list;
};

// A train's push on what it hits (onCollide), put into effect after the tick.
struct RailroadImpulse
{
	ecs::Entity victim;
	Engine::Math::FixedVector3 position; // where it is put (shoved, lifted)
	Engine::Math::FixedVector3 velocity; // added
	std::int32_t pitchRate{0}, rollRate{0}, yawRate{0};
	std::uint8_t setsPosition{0}, pushes{0}, spins{0}, turns{0}, falls{0}; // falls: setAllowToFall, setAllowBouncing, setAllowAirborneFriction
};

struct RailroadImpulses
{
	std::vector<RailroadImpulse> impulses;
};

// The damage types a train deals: kill() (UNRESISTABLE, NORMAL, its maximum health) and its slow crush (CRUSH, CRUSHED).
struct RailroadDamage
{
	std::uint32_t unresistable{0};
	std::uint32_t crush{0};
	std::uint32_t normalDeath{0};
	std::uint32_t crushedDeath{0};
};

struct RailroadRequests
{
	std::vector<ecs::Entity> carriages; // locomotives to make carriages for
	// makeAWallOutOfThisTrain: locomotives whose trains become walls on the pathfinding grid (true) or stop being ones.
	std::vector<std::pair<ecs::Entity, bool>> walls;
	std::vector<ecs::Entity> disembarks; // disembark(): locomotives whose trains let their passengers out
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::RailWaypoints>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.rail_waypoints";
};
template<>
struct ResourceTraits<generalszh::gameplay::RailTracks>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.rail_tracks";
};
template<>
struct ResourceTraits<generalszh::gameplay::RailroadImpulses>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railroad_impulses";
};
template<>
struct ResourceTraits<generalszh::gameplay::RailroadDamage>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railroad_damage";
};
template<>
struct ResourceTraits<generalszh::gameplay::RailroadCues>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railroad_cues";
};
template<>
struct ResourceTraits<generalszh::gameplay::RailroadRequests>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railroad_requests";
};
}
