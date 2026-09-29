export module engine.gameplay.rts.death.components.collapse;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// A dead structure coming down (the original's StructureCollapseUpdate):
// shuddering until `collapseTick`, then falling into the ground (its drawn
// height, `height`, dropping by `velocity` a tick, sped up by gravity), with
// bursts of effects every so often, until it is under the ground; then it
// shows its post-collapse look. `collapse` indexes its definition's collapses.
export namespace engine::gameplay
{
enum class CollapseState : std::uint8_t
{
	Waiting,
	Collapsing,
	Done,
};

struct Collapse
{
	std::uint64_t collapseTick{0};
	std::uint64_t burstTick{0};
	Engine::Math::Fixed height;
	Engine::Math::Fixed velocity;
	std::uint32_t collapse{0};
	CollapseState state{CollapseState::Waiting};
	std::uint8_t reserved[3]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Collapse>
{
	static constexpr std::string_view StableName = "engine.gameplay.collapse";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
