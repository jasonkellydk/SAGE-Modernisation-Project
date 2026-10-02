export module engine.gameplay.rts.combat.components.assisted_targeting;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
import engine.ecs.system.system;

// Answering others of its kind (the original's AssistedTargetingUpdate): when one of its player's own kind fires a
// weapon that asks for help (RequestAssistRange) close enough, it joins in with its assisting slot (AssistingWeaponSlot)
// for so many shots (AssistingClipSize). While it helps (Assisting) that slot is locked for the attack only
// (LOCKED_TEMPORARILY), the lock it had before is put back after.
export namespace engine::gameplay
{
struct AssistedTargeting
{
	std::uint8_t slot{1};
	std::uint8_t reserved{0};
	std::uint16_t reserved2{0};
	std::uint32_t clip{0};
};

struct Assisting
{
	ecs::Entity victim;
	std::uint32_t shotsLeft{0};
	std::uint8_t slot{0};
	std::uint8_t previousLock{0xFF};
	std::uint16_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::AssistedTargeting>
{
	static constexpr std::string_view StableName = "engine.gameplay.assisted_targeting";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
template<>
struct ComponentTraits<engine::gameplay::Assisting>
{
	static constexpr std::string_view StableName = "engine.gameplay.assisting";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Assisting &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.victim.index);
		hasher.AppendU64(value.victim.generation);
		hasher.AppendU64(value.shotsLeft);
		hasher.AppendU64(value.slot | (static_cast<std::uint64_t>(value.previousLock) << 8));
	}
};
}
