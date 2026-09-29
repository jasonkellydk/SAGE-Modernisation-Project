export module engine.gameplay.rts.parachute.components.parachute;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// A parachute with its rider (the original's ParachuteContain state), and the rider's side of it. The chute falls
// closed until it has dropped its opening distance from where it started, then opens and glides for its landing spot
// (a set one: ultra-accurate, else where it opened), swaying on a spring; the rider hangs from its harness, held.
// Bones are its rider's harness point (PARA_MAN) at rest, falling and hanging open.
export namespace engine::gameplay
{
namespace parachute_flag
{
inline constexpr std::uint32_t Started = 1u << 0;       // its start height is known
inline constexpr std::uint32_t Opened = 1u << 1;
inline constexpr std::uint32_t Moving = 1u << 2;        // gliding for its landing spot (not there yet)
inline constexpr std::uint32_t Override = 1u << 3;      // a set landing spot (setOverrideDestination)
inline constexpr std::uint32_t MaintainValid = 1u << 4; // it holds `maintain` (locoUpdate_maintainCurrentPosition)
}

struct Parachute
{
	std::uint32_t definition{0};
	std::uint32_t flags{0};
	ecs::Entity rider;
	Engine::Math::FixedVector3 riderBone;     // the rider's PARA_MAN falling (FREEFALL)
	Engine::Math::FixedVector3 riderOpenBone; // and hanging (PARACHUTING)
	Engine::Math::FixedVector3 landing;       // its landing spot
	Engine::Math::FixedVector3 maintain;      // where it holds once there
	Engine::Math::Fixed startZ;
	std::int32_t pitch{0}; // sway, turn units
	std::int32_t roll{0};
	std::int32_t pitchRate{0};
	std::int32_t rollRate{0};
	std::uint64_t openedTick{0};

	bool Has(std::uint32_t flag) const noexcept { return (flags & flag) != 0; }
	void Set(std::uint32_t flag, bool on) noexcept { flags = on ? (flags | flag) : (flags & ~flag); }
	const Engine::Math::FixedVector3 &RiderBone() const noexcept { return Has(parachute_flag::Opened) ? riderOpenBone : riderBone; }
};

// Hanging from a parachute: held (DISABLED_HELD), placed by it each tick, until it lands.
struct ParachuteRider
{
	ecs::Entity chute;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Parachute>
{
	static constexpr std::string_view StableName = "engine.gameplay.parachute";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Parachute &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.definition);
		hasher.AppendU64(value.flags);
		hasher.AppendU64(value.rider.index);
		hasher.AppendU64(value.rider.generation);
		for (const auto *vector : {&value.riderBone, &value.riderOpenBone, &value.landing, &value.maintain})
		{
			hasher.AppendU64(static_cast<std::uint64_t>(vector->x.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(vector->y.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(vector->z.Raw()));
		}
		hasher.AppendU64(static_cast<std::uint64_t>(value.startZ.Raw()));
		hasher.AppendU64(static_cast<std::uint32_t>(value.pitch));
		hasher.AppendU64(static_cast<std::uint32_t>(value.roll));
		hasher.AppendU64(static_cast<std::uint32_t>(value.pitchRate));
		hasher.AppendU64(static_cast<std::uint32_t>(value.rollRate));
		hasher.AppendU64(value.openedTick);
	}
};
template<>
struct ComponentTraits<engine::gameplay::ParachuteRider>
{
	static constexpr std::string_view StableName = "engine.gameplay.parachute_rider";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::ParachuteRider &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.chute.index);
		hasher.AppendU64(value.chute.generation);
	}
};
}
