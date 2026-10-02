export module engine.gameplay.rts.construction.components.construction_progress;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// How far a structure is built (the original's construction percent): 100 is
// complete; below that it rises while being built and falls while being sold
// (into the negatives as its scaffold sinks away).
export namespace engine::gameplay
{
struct ConstructionProgress
{
	Engine::Math::Fixed percent{Engine::Math::Fixed::FromInt(100)};
	// Put up by a rebuild (DozerAIUpdate's m_isRebuild: a rebuild hole's worker): its completion builds nothing new.
	std::uint32_t rebuild{0};
	std::uint32_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::ConstructionProgress>
{
	static constexpr std::string_view StableName = "engine.gameplay.construction_progress";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
