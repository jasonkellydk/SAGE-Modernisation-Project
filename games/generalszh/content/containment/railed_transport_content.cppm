export module games.generalszh.content.containment.railed_transport_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import Engine.Core.Math.Fixed;
export import engine.config.binding.schema;

// A railed transport (the ferry, CivilianVehicleFerry) as content: RailedTransportAIUpdate's PathPrefixName (its paths
// are the waypoints <prefix>Start01 / <prefix>End01 up to 32) and RailedTransportDockUpdate's PullInsideDuration /
// PushOutsideDuration (milliseconds up to whole frames, parseDurationUnsignedInt; 0 when not given) and
// ToleranceDistance (50 when not given): how far from its centre a docker may be to be pulled in. Its dock's points
// are its model's bones (DockLayout, harvest_content), its room its RailedTransportContain (a TransportContain).
export namespace generalszh::content
{
struct RailedTransportContent
{
	std::string pathPrefix;
	std::uint64_t pullInsideTicks{0};
	std::uint64_t pushOutsideTicks{0};
	Engine::Math::Fixed tolerance{Engine::Math::Fixed::FromInt(50)};
};

inline std::optional<RailedTransportContent> ReadRailedTransport(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	const auto module = [&](std::string_view type) -> const ModuleEntry * {
		for (const ModuleEntry &entry : object.modules)
			if (entry.block != nullptr && entry.type == type)
				return &entry;
		return nullptr;
	};
	const ModuleEntry *ai = module("RailedTransportAIUpdate");
	const ModuleEntry *dock = module("RailedTransportDockUpdate");
	if (ai == nullptr || dock == nullptr)
		return std::nullopt;
	RailedTransportContent out;
	if (const auto *prefix = ai->block->Find("PathPrefixName"); prefix != nullptr && !prefix->values.empty())
		out.pathPrefix = std::string(prefix->Value());
	const auto frames = [&](std::string_view key) -> std::uint64_t {
		const auto *node = dock->block->Find(key);
		if (node == nullptr || node->values.empty())
			return 0;
		const std::int64_t ms = std::max<std::int64_t>(0, engine::config::values::ParseInt(node->Value()).value_or(0));
		return static_cast<std::uint64_t>((ms * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
	};
	out.pullInsideTicks = frames("PullInsideDuration");
	out.pushOutsideTicks = frames("PushOutsideDuration");
	if (const auto *tolerance = dock->block->Find("ToleranceDistance"); tolerance != nullptr && !tolerance->values.empty())
		out.tolerance = engine::config::values::ParseFixed(tolerance->Value()).value_or(out.tolerance);
	return out;
}
}
