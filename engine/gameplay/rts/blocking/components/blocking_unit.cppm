export module engine.gameplay.rts.blocking.components.blocking_unit;
import std;

export import engine.ecs.core.component_registry;

// A ground unit whose AI weighs in on what it runs into (AIUpdateInterface::processCollision): what it is for the path
// priority between two units blocking each other (hasHigherPathPriority: a dozer before anything else, a vehicle before
// infantry) and for infantry crossing infantry (blockedBy). Fixed at creation.
export namespace engine::gameplay
{
namespace blocking_kind
{
inline constexpr std::uint8_t Infantry = 1u << 0;
inline constexpr std::uint8_t Vehicle = 1u << 1;
inline constexpr std::uint8_t Dozer = 1u << 2;
inline constexpr std::uint8_t Harvester = 1u << 3; // KINDOF_HARVESTER: it wants a clear path (moveAllies)
inline constexpr std::uint8_t NoCollide = 1u << 4; // KINDOF_NO_COLLIDE: it asks nobody to make way
}

struct BlockingUnit
{
	std::uint8_t kinds{0};
	std::uint8_t reserved[3]{}; // no padding: checkpoints hold its bytes

	bool Is(std::uint8_t kind) const noexcept { return (kinds & kind) != 0; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::BlockingUnit>
{
	static constexpr std::string_view StableName = "engine.gameplay.blocking_unit";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::BlockingUnit &value, StateHasher &hasher) noexcept { hasher.AppendU64(value.kinds); }
};
}
