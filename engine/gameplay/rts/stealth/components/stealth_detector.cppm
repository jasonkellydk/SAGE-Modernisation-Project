export module engine.gameplay.rts.stealth.components.stealth_detector;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.core.component_registry;

// Seeing through stealth (the original's StealthDetectorUpdate): every
// `rate` ticks a detector reveals the stealthed enemies and neutrals within
// its range whose target classes it may detect, until just past its next
// scan. Detectors scan at random phases, so they do not all scan on one tick.
export namespace engine::gameplay
{
namespace stealth_detector_flag
{
inline constexpr std::uint32_t Enabled = 1u << 0;
inline constexpr std::uint32_t WhileGarrisoned = 1u << 1; // still detects inside a building it can fire out of
inline constexpr std::uint32_t WhileContained = 1u << 2;  // still detects in a transport or tunnel
}

struct StealthDetector
{
	std::uint64_t rate{1};
	std::uint64_t nextScan{0};
	Engine::Math::Fixed range;
	std::uint32_t requiredClasses{0};  // any of these (none: all)
	std::uint32_t forbiddenClasses{0}; // none of these
	std::uint32_t flags{stealth_detector_flag::Enabled};
	std::uint32_t reserved{0};

	bool Has(std::uint32_t flag) const noexcept { return (flags & flag) != 0; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::StealthDetector>
{
	static constexpr std::string_view StableName = "engine.gameplay.stealth_detector";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
