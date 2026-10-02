export module games.generalszh.gameplay.waveguide.components.wave_guide;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;

// The dam break's flood wave (WaveGuideUpdate) on its own object: its module data as made (WaveDelay in frames, the
// fraction kept; YSize, LinearWaveSpacing and WaveBendMagnitude its front's shape; PreferredHeight, the shore effects'
// distance behind the front, DamageRadius, DamageAmount, ToppleForce, RandomSplashSoundFrequency) and its state: whether
// it still has to disable itself (m_needDisable: its first update does), the tick it became active (m_activeFrame,
// 0: not yet), whether it has set off along its path (m_initialized), the tick it last tried a splash sound
// (m_splashSoundFrame), how many splash rolls it has made (its splash stream's position) and the end of its path
// (m_finalDestination). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct WaveGuide
{
	Engine::Math::Fixed delay;
	Engine::Math::Fixed ySize;
	Engine::Math::Fixed spacing;
	Engine::Math::Fixed bend;
	Engine::Math::Fixed preferredHeight;
	Engine::Math::Fixed shoreline;
	Engine::Math::Fixed damageRadius;
	Engine::Math::Fixed damageAmount;
	Engine::Math::Fixed toppleForce;
	Engine::Math::FixedVector3 finalDestination;
	std::uint64_t activeTick{0};
	std::uint64_t splashTick{0};
	std::int32_t splashFrequency{0};
	std::uint32_t splashTries{0}; // splash rolls made: the next is this many draws into its splash stream
	std::uint8_t needDisable{1};
	std::uint8_t initialized{0};
	std::uint8_t reserved[6]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::WaveGuide>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.wave_guide";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
