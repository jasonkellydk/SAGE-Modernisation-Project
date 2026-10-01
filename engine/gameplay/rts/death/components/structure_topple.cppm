export module engine.gameplay.rts.death.components.structure_topple;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.TurnAngle;

// A dead structure toppling over (the original's StructureToppleUpdate): waiting until `toppleTick` (bursts of
// effects every so often at `burstAt`), then tipping over along `direction`, its lean `angle` growing by `velocity` a
// tick (both radians, Q32: its first nudges are far below a Q16 unit), held back by its structural `integrity`; flat,
// it stands again turned to `toppleAngle` (its post-collapse look). `topple` indexes its definition's topples;
// `effects`: the killing blow's damage type plays its effects. Simulation state: hashed and checkpointed.
export namespace engine::gameplay
{
enum class StructureToppleState : std::uint8_t
{
	Waiting,
	Toppling,
	Done,
};

struct StructureTopple
{
	static constexpr std::int64_t One = std::int64_t{1} << 32;
	static constexpr std::int64_t Nudge = 4294967; // 0.001 rad: "a little nudge in the right direction"

	std::uint64_t toppleTick{0};
	std::uint64_t burstTick{0};
	std::int64_t angle{Nudge};
	std::int64_t velocity{0};
	std::int64_t integrity{0};
	Engine::Math::Fixed lastCrushed; // how far from its base it has crushed so far
	Engine::Math::FixedVector3 burstAt;
	Engine::Math::FixedVector2 direction; // unit
	Engine::Math::TurnAngle toppleAngle;
	std::uint32_t topple{0};
	StructureToppleState state{StructureToppleState::Waiting};
	std::uint8_t effects{1};
	std::uint8_t reserved[6]{};

	// How far over it is drawn while it falls (the rotation its transform took: every velocity applied), now and a tick
	// before; upright otherwise (flat, it stands again the same tick: setOrientation).
	constexpr std::int64_t Lean() const noexcept { return state == StructureToppleState::Toppling ? angle - Nudge : 0; }
	constexpr std::int64_t PreviousLean() const noexcept
	{
		return state == StructureToppleState::Toppling ? std::max<std::int64_t>(angle - Nudge - velocity, 0) : 0;
	}
};

// A script's direction for a structure's fall should it topple (ScriptEngine::setToppleDirection on a named thing).
struct ScriptedTopple
{
	Engine::Math::FixedVector2 direction;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::StructureTopple>
{
	static constexpr std::string_view StableName = "engine.gameplay.structure_topple";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
template<>
struct ComponentTraits<engine::gameplay::ScriptedTopple>
{
	static constexpr std::string_view StableName = "engine.gameplay.scripted_topple";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
