export module engine.gameplay.common.areas.components.area_presence;
import std;

export import engine.ecs.core.component_registry;

// The trigger areas an object is in (the original's Object::m_triggerInfo, up to MAX_TRIGGER_AREA_INFOS = 5): each
// area's index and whether it is inside, entered or exited it at `changedTick` (the tick it last entered or exited
// one); `x`, `y`: its whole position when last checked (REAL_TO_INT: toward zero); `known`: checked at all yet.
// Objects that are not projectiles or inert keep one. Simulation state: checkpointed.
export namespace engine::gameplay
{
namespace area_flag
{
inline constexpr std::uint8_t Inside = 1u << 0;
inline constexpr std::uint8_t Entered = 1u << 1;
inline constexpr std::uint8_t Exited = 1u << 2;
}

struct AreaPresence
{
	static constexpr std::size_t Capacity = 5;
	std::array<std::uint32_t, Capacity> area{};
	std::array<std::uint8_t, Capacity> flags{};
	std::uint8_t count{0};
	std::uint8_t known{0};
	std::uint8_t reserved{0};
	std::int32_t x{0};
	std::int32_t y{0};
	std::uint32_t reserved2{0};
	std::uint64_t changedTick{0};

	// Object::isInside / didEnter / didExit (the last two only on the tick it changed or the one after: the scripts
	// run before the objects move).
	bool Inside(std::uint32_t which) const noexcept { return Has(which, area_flag::Inside); }
	bool Entered(std::uint32_t which, std::uint64_t now) const noexcept { return Recent(now) && Has(which, area_flag::Entered); }
	bool Exited(std::uint32_t which, std::uint64_t now) const noexcept { return Recent(now) && Has(which, area_flag::Exited); }
	bool Recent(std::uint64_t now) const noexcept { return changedTick != 0 && (changedTick == now || changedTick + 1 == now); }

private:
	bool Has(std::uint32_t which, std::uint8_t flag) const noexcept
	{
		for (std::size_t index = 0; index < count; ++index)
			if (area[index] == which && (flags[index] & flag) != 0)
				return true;
		return false;
	}
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::AreaPresence>
{
	static constexpr std::string_view StableName = "engine.gameplay.area_presence";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
