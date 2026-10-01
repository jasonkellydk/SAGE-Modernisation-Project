export module games.generalszh.gameplay.railroad.components.railcar;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;

// A train car (RailroadBehavior's state). Its pull (PullInfo) is where it is along its track: the point its rear sits on
// (`hitch`, the towHitchPosition), how far along (`distance`), its speed a frame and direction (1 or -1), and the last
// ping-pong point it turned at. `conductor` is the point it drives when it leads (a locomotive's front; a pulled car's
// copy of its puller's pull, kept for when it is left behind); `pull` its own, passed on to what it pulls (`trailer`).
// `anchor` is its track's anchor waypoint id (none: no track); `handle` the last track segment a locomotive's rear came
// into (0xfacade at first). It waits at a station `waitTimer` frames (held there by a script: `held`), unloading its
// passengers on stopping when `disembark`; `unpulled` counts its own updates since it was last pulled (it leads once
// that passes 2). `wings`: before the start of a track that does not loop; `endOfLine`: past its end (for good).
// `gone`: it took itself away (destroyObject); `hidden`: not drawn. Simulation state: hashed and checkpointed.
export namespace generalszh::gameplay
{
enum class ConductorState : std::uint8_t
{
	ApplyBrakes,
	WaitAtStation,
	Accelerate,
	Coast,
};

struct RailPull
{
	static constexpr std::uint32_t Unset = 0xfacade;
	Engine::Math::FixedVector3 hitch;
	Engine::Math::Fixed speed;
	Engine::Math::Fixed distance;
	std::uint32_t lastPingPong{Unset};
	std::int32_t direction{1};
};

struct Railcar
{
	static constexpr std::uint32_t NoTrack = 0xFFFFFFFFu;
	RailPull conductor;
	RailPull pull;
	ecs::Entity trailer;
	std::uint32_t anchor{NoTrack};
	std::uint32_t handle{RailPull::Unset};
	std::int32_t waitTimer{0};
	std::int32_t unpulled{0};
	std::uint8_t locomotive{0};
	std::uint8_t lead{0};
	std::uint8_t hitched{0};    // m_hasEverBeenHitched
	std::uint8_t trackLoaded{0}; // m_trackDataLoaded
	std::uint8_t wings{1};
	std::uint8_t endOfLine{0};
	std::uint8_t disembark{0};
	std::uint8_t tunnel{0};
	std::uint8_t held{0};
	std::uint8_t gone{0};
	ConductorState state{ConductorState::Coast};
	std::uint8_t hidden{0}; // its drawable hidden (setDrawableHidden: in the wings or past the end of the line)
	std::uint8_t reserved[4]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::Railcar>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railcar";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
