export module games.generalszh.content.lifetime.lifetime_content;
import std;

export import games.generalszh.content.objects.object_definition;

// An object's limited life, from its LifetimeUpdate module (it is killed)
// or DeletionUpdate module (it is simply removed): between MinLifetime and
// MaxLifetime (milliseconds, as ticks).
export namespace generalszh::content
{
struct LifetimeRange
{
	std::uint64_t minimum{0};
	std::uint64_t maximum{0};
	bool deletes{false}; // DeletionUpdate: simply goes (no death)
};

std::optional<LifetimeRange> ReadObjectLifetime(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || (module.type != "LifetimeUpdate" && module.type != "DeletionUpdate"))
			continue;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		LifetimeRange range;
		if (const auto *node = module.block->Find("MinLifetime"))
			range.minimum = engine::config::ReadDurationTicks(*node, bind).value_or(0);
		if (const auto *node = module.block->Find("MaxLifetime"))
			range.maximum = engine::config::ReadDurationTicks(*node, bind).value_or(0);
		range.maximum = std::max(range.maximum, range.minimum);
		range.deletes = module.type == "DeletionUpdate";
		return range;
	}
	return std::nullopt;
}
}
