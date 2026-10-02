export module engine.gameplay.rts.topple.components.topple;
import std;

export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.TurnAngle;
export import Engine.Core.Math.FixedVector;
import engine.ecs.core.component_registry;

// Something that falls over when a heavy crusher runs into it (the
// original's ToppleUpdate: trees, lamp posts, fences): it falls away from
// the crusher, faster the faster it was hit, bounces on the ground and is
// killed once it lies still (or as soon as it starts falling).
export namespace engine::gameplay
{
enum class ToppleState : std::uint32_t
{
	Upright,
	Falling,
	Down,
};

namespace topple_flag
{
inline constexpr std::uint32_t KillWhenStarting = 1u << 0;
inline constexpr std::uint32_t KillWhenDown = 1u << 1;
inline constexpr std::uint32_t LeftOrRightOnly = 1u << 2;
// ReorientToppledRubble: killed when down, it is moved to where its top now lies and stood upright (its rubble state).
inline constexpr std::uint32_t ReorientRubble = 1u << 3;
}

// How it was toppled (the original's ToppleOptions): not bouncing (lies still on first reaching the ground), no bounce
// effects.
namespace topple_option
{
inline constexpr std::uint32_t NoBounce = 1u << 0;
inline constexpr std::uint32_t NoFx = 1u << 1;
}

struct Topple
{
	// As authored (shares of the crusher's speed; radians per tick for the rates).
	Engine::Math::Fixed initialVelocity{Engine::Math::Fixed::FromRatio(2, 10)};
	Engine::Math::Fixed initialAcceleration{Engine::Math::Fixed::FromRatio(1, 100)};
	Engine::Math::Fixed bounceVelocity{Engine::Math::Fixed::FromRatio(3, 10)};
	std::uint32_t flags{topple_flag::KillWhenDown};
	ToppleState state{ToppleState::Upright};
	// Falling.
	Engine::Math::Fixed angularVelocity;
	Engine::Math::Fixed angularAcceleration;
	Engine::Math::Fixed fallen; // radians
	Engine::Math::TurnAngle facingStep;
	std::uint32_t facingSteps{0};
	std::int32_t fallSign{1}; // falls toward its facing (1) or away (-1)
	std::uint32_t options{0}; // topple_option
	// A script's direction for its fall (ScriptEngine::setToppleDirection, adjustToppleDirection): in place of the push's.
	Engine::Math::FixedVector2 scriptedDirection{};
	std::uint32_t scripted{0};
	std::uint32_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Topple>
{
	static constexpr std::string_view StableName = "engine.gameplay.topple";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
