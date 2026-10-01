export module engine.gameplay.rts.docking.components.dock_look;
import std;

export import engine.ecs.core.component_registry;

// The docking model conditions a dock itself shows (DockUpdate: onEnterReached sets DOCKING and DOCKING_BEGINNING on
// it, clearing DOCKING_ENDING; onDockReached turns BEGINNING into DOCKING_ACTIVE; onExitReached leaves only
// DOCKING_ENDING, kept until the next mover enters; cancelDock for its active mover clears them all). Its movers'
// own docking looks follow their Docking phase. Simulation state: checkpointed.
export namespace engine::gameplay
{
namespace dock_look
{
inline constexpr std::uint8_t Docking = 1u << 0;
inline constexpr std::uint8_t Beginning = 1u << 1;
inline constexpr std::uint8_t Active = 1u << 2;
inline constexpr std::uint8_t Ending = 1u << 3;
}

struct DockLook
{
	std::uint8_t flags{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DockLook>
{
	static constexpr std::string_view StableName = "engine.gameplay.dock_look";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::DockLook &value, StateHasher &hasher) noexcept { hasher.AppendU64(value.flags); }
};
}
