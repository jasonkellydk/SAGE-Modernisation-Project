export module engine.gameplay.rts.movement.components.floor_lift;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// The floor it stands on is `height` over the terrain (a carrier's deck: OBJECT_STATUS_DECK_HEIGHT_OFFSET, the
// physics' ground clamp at the terrain plus the deck's LandingDeckHeightOffset): on the ground its locomotor holds it
// there, not on the terrain.
export namespace engine::gameplay
{
struct FloorLift
{
	Engine::Math::Fixed height;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::FloorLift>
{
	static constexpr std::string_view StableName = "engine.gameplay.floor_lift";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::FloorLift &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.height.Raw()));
	}
};
}
