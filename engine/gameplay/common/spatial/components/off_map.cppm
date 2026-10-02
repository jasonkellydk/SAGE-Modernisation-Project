export module engine.gameplay.common.spatial.components.off_map;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

// The entity exists but is not in the world's space right now (carried in a
// transport, held off the map): it is not drawn, found, moved or targeted.
// Systems that work on the map exclude it. One carried where its carrier lets
// it fire (the original's isPassengerAllowedToFire) still picks and fires at
// victims from where it rides, and its shots never run into that carrier.
export namespace engine::gameplay
{
// Why it is off the map. A Stationed rider is the exception: held by a container that does not enclose it
// (isEnclosingContainerFor false: a fire base), it stays in the world (OpenContain::addToContain leaves it in the
// partition and drawn) at its station, so it is found and targeted there.
namespace off_map_reason
{
inline constexpr std::uint8_t Contained = 1;
inline constexpr std::uint8_t Mounted = 2;
inline constexpr std::uint8_t Hijacking = 3;
inline constexpr std::uint8_t Stationed = 4;
}

struct OffMap
{
	std::uint8_t reason{0};
	bool armed{false};
	std::uint8_t reserved[2]{}; // no padding: checkpoints hold its bytes
	ecs::Entity holder{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::OffMap>
{
	static constexpr std::string_view StableName = "engine.gameplay.off_map";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::OffMap &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.reason | (value.armed ? 0x100u : 0u));
		hasher.AppendU64(value.holder.index);
		hasher.AppendU64(value.holder.generation);
	}
};
}
