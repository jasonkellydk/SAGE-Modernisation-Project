export module engine.gameplay.rts.vision.definitions.dynamic_clearing;
import std;

export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// A shroud clearing range that swells and fades over the object's life (the original's
// DynamicShroudClearingRangeUpdate: a radar scan's ping, a spy satellite's): after GrowDelay it grows to its full range
// over GrowTime, holds, and ShrinkDelay after it began shrinks to FinalVision over ShrinkTime; the object's range is
// set to it only every ChangeInterval (GrowInterval while growing) ticks. Times in ticks. DynamicClearingCatalog: each
// object definition's (by definition index; none: not present).
export namespace engine::gameplay
{
struct DynamicClearingDefinition
{
	std::uint32_t changeInterval{0};
	std::uint32_t growInterval{0};
	std::uint32_t shrinkDelay{0};
	std::uint32_t shrinkTime{0};
	std::uint32_t growDelay{0};
	std::uint32_t growTime{0};
	Engine::Math::Fixed finalVision;
};

struct DynamicClearingCatalog
{
	std::vector<std::optional<DynamicClearingDefinition>> definitions;

	const DynamicClearingDefinition *Of(std::uint32_t definition) const noexcept
	{
		return definition < definitions.size() && definitions[definition] ? &*definitions[definition] : nullptr;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::DynamicClearingCatalog>
{
	static constexpr std::string_view StableName = "engine.gameplay.dynamic_clearing_catalog";
};
}
