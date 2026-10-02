export module engine.gameplay.rts.construction.components.builder;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;

// A dozer or worker with a structure to build or repair (DozerAIUpdate's
// build and repair tasks): it goes to its dock beside it and, once there
// (within `reach`) and stopped, builds it a step a tick until it stands, or
// repairs `repairShare` of its most health a tick until it is whole.
export namespace engine::gameplay
{
struct Builder
{
	ecs::Entity target;
	Engine::Math::FixedVector2 dock;
	Engine::Math::Fixed reach;
	std::uint8_t atWork{0};
	std::uint8_t repair{0};
	std::uint8_t reserved[6]{};
	Engine::Math::Fixed repairShare; // RepairHealthPercentPerSecond / LOGICFRAMES_PER_SECOND
	Engine::Math::FixedVector2 leave; // where it goes once its structure stands (DOZER_DOCK_POINT_END)
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Builder>
{
	static constexpr std::string_view StableName = "engine.gameplay.builder";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Builder &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64((std::uint64_t{value.target.index} << 32) | value.target.generation);
		hasher.AppendU64(static_cast<std::uint64_t>(value.dock.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.dock.y.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.reach.Raw()));
		hasher.AppendU64(value.atWork | (std::uint64_t{value.repair} << 8));
		hasher.AppendU64(static_cast<std::uint64_t>(value.repairShare.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.leave.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.leave.y.Raw()));
	}
};
}
