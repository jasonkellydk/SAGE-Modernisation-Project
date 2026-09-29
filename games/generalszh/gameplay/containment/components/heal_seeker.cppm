export module games.generalszh.gameplay.containment.components.heal_seeker;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// A unit that goes to be healed on its own (AutoFindHealingUpdate): how often and how far it looks for a heal pad,
// how hurt it must be (at most `neverHeal` of its maximum), and the ticks left until it looks again (m_nextScanFrames).
export namespace generalszh::gameplay
{
struct HealSeeker
{
	std::uint64_t scanTicks{0};
	std::uint64_t countdown{0};
	Engine::Math::Fixed range;
	Engine::Math::Fixed neverHeal;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::HealSeeker>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.heal_seeker";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
