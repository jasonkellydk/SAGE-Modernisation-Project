export module games.generalszh.gameplay.combat.components.cleanup_hazard;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// A unit that cleans up hazards on its own (CleanupHazardUpdate: the Ambulance's toxin clean-up), as data: how often and
// how far it looks (ScanRate, ScanRange), the ticks left until it looks again (m_nextScanFrames), the hazard it last
// picked (m_bestTargetID) and whether it was within reach of it (m_inRange); the area a CleanupAreaPower sent it to
// (m_pos) and how far round it it cleans (m_moveRange; 0: none); and the weapon slot it cleans with (WeaponSlot: PRIMARY 0,
// SECONDARY 1, TERTIARY 2). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct CleanupHazard
{
	Engine::Math::FixedVector2 position;
	ecs::Entity best;
	Engine::Math::Fixed scanRange;
	Engine::Math::Fixed moveRange;
	std::uint64_t scanTicks{0};
	std::uint64_t nextScan{0};
	std::uint8_t inRange{0};
	std::uint8_t slot{0};
	std::uint8_t reserved[6]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::CleanupHazard>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.cleanup_hazard";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::CleanupHazard &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.position.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.position.y.Raw()));
		hasher.AppendU64((std::uint64_t{value.best.index} << 32) | value.best.generation);
		hasher.AppendU64(static_cast<std::uint64_t>(value.moveRange.Raw()));
		hasher.AppendU64(value.nextScan);
		hasher.AppendU64((std::uint64_t{value.slot} << 8) | value.inRange);
	}
};
}
