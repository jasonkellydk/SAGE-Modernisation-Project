export module games.generalszh.gameplay.waveguide.resources.wave_guide_events;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
import engine.ecs.system.system;

// What the tick's flood waves (WaveGuideSystem) leave for the session to carry out once the systems have run
// (ApplyWaveGuideEvents), in the order they happened:
//   Start: startMoving: the wave put at its path's first waypoint (on the ground there), facing its next one, and sent
//     along the path from it (`waypoint`);
//   Destroy: TheGameLogic->destroyObject on the wave (its path was bad, or it reached its end);
//   Wet: a victim made wet (OBJECT_STATUS_WET) and flooded (MODELCONDITION_FLOODED, its shadows off);
//   BridgeHit: a bridge victim at `at`: the WaterWaveBridge put there and its bridge deleted.
// And the tick's cues for the presentation (WaveGuideCues): a random splash sound on the wave, a shoreline splash
// (WaveSplashLeft01 / WaveSplashRight01) at `at`, a victim's WaveHit01 riding on the wave at `local` in its frame, the
// BridgeParticle at `at` turned by `yaw`, WaveSplash01 where the wave ends. Both cleared as each tick starts.
export namespace generalszh::gameplay
{
struct WaveGuideEvent
{
	enum class Kind : std::uint8_t
	{
		Start,
		Destroy,
		Wet,
		BridgeHit,
	};
	Kind kind{Kind::Start};
	ecs::Entity guide;
	ecs::Entity target;
	Engine::Math::FixedVector3 at;
	Engine::Math::TurnAngle facing{};
	std::uint32_t waypoint{0};
};

struct WaveGuideEvents
{
	std::vector<WaveGuideEvent> list;
};

struct WaveGuideCue
{
	enum class Kind : std::uint8_t
	{
		Splash,
		ShoreLeft,
		ShoreRight,
		Hit,
		Bridge,
		End,
	};
	Kind kind{Kind::Splash};
	ecs::Entity guide;
	std::uint32_t definition{0}; // the wave's (its sound and bridge particle are its module data's)
	Engine::Math::FixedVector3 at;
	Engine::Math::FixedVector3 local;
	Engine::Math::TurnAngle yaw{};
};

struct WaveGuideCues
{
	std::vector<WaveGuideCue> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::WaveGuideEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.wave_guide_events";
};
template<>
struct ResourceTraits<generalszh::gameplay::WaveGuideCues>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.wave_guide_cues";
};
}
