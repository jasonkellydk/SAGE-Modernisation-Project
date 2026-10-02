export module games.generalszh.gameplay.powers.resources.spy_vision_catalog;
import std;

import engine.ecs.system.system;

// Each kind of object's SpyVisionUpdate modules (definition data, indexed by DefinitionRef): whether an upgrade turns
// it on, whether it runs on its own timers, and those timers (ticks). Immutable.
export namespace generalszh::gameplay
{
struct SpyVisionConfig
{
	bool needsUpgrade{false};
	bool selfPowered{false};
	std::uint64_t durationTicks{0};
	std::uint64_t intervalTicks{0};
};

struct SpyVisionCatalog
{
	std::vector<std::vector<SpyVisionConfig>> byDefinition;

	const SpyVisionConfig *Of(std::uint32_t definition, std::size_t module) const noexcept
	{
		return definition < byDefinition.size() && module < byDefinition[definition].size() ? &byDefinition[definition][module] : nullptr;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::SpyVisionCatalog>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.spy_vision_catalog";
};
}
