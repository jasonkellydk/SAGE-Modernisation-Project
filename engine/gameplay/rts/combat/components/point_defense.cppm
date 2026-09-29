export module engine.gameplay.rts.combat.components.point_defense;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;

// A point defense laser (the original's PointDefenseLaserUpdate): its own
// weapon, fired at incoming missiles (its primary target classes, else its
// secondary ones) it finds on a slow scan and tracks between scans; when it
// next scans, when it may fire again, and what it tracks.
export namespace engine::gameplay
{
struct PointDefenseDefinition
{
	std::uint32_t weapon{0xFFFFFFFFu};
	std::uint32_t primaryClasses{0};   // PrimaryTargetTypes
	std::uint32_t secondaryClasses{0}; // SecondaryTargetTypes
	std::uint32_t scanTicks{0};        // ScanRate
	Engine::Math::Fixed scanRange;     // ScanRange
};

struct PointDefense
{
	PointDefenseDefinition definition;
	std::uint32_t scanLeft{0}; // m_nextScanFrames
	std::uint32_t shotLeft{0}; // m_nextShotAvailableInFrames
	ecs::Entity target;        // m_bestTargetID
	bool inRange{false};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::PointDefense>
{
	static constexpr std::string_view StableName = "engine.gameplay.point_defense";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::PointDefense &value, StateHasher &hasher) noexcept
	{
		const auto &d = value.definition;
		hasher.AppendU64((std::uint64_t{d.weapon} << 32) | d.scanTicks);
		hasher.AppendU64((std::uint64_t{d.primaryClasses} << 32) | d.secondaryClasses);
		hasher.AppendU64(static_cast<std::uint64_t>(d.scanRange.Raw()));
		hasher.AppendU64((std::uint64_t{value.scanLeft} << 32) | value.shotLeft);
		hasher.AppendU64((std::uint64_t{value.target.index} << 32) | value.target.generation);
		hasher.AppendU64(value.inRange ? 1u : 0u);
	}
};
}
