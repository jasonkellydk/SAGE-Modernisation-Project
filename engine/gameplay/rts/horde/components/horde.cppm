export module engine.gameplay.rts.horde.components.horde;
import std;

export import engine.ecs.core.component_registry;

// A unit that hordes (the original's HordeUpdate): when it next looks, and
// whether it is in a horde (its bonus) and a true member (the horde's thick
// part, which others close by rub off on).
export namespace engine::gameplay
{
struct Horde
{
	std::uint64_t nextTick{0};
	bool inHorde{false};
	bool trueMember{false};
	std::uint8_t reserved[6]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Horde>
{
	static constexpr std::string_view StableName = "engine.gameplay.horde";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Horde &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.nextTick);
		hasher.AppendU64((value.inHorde ? 1u : 0u) | (value.trueMember ? 2u : 0u));
	}
};
}
