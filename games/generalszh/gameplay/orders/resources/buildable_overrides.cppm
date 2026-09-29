export module games.generalszh.gameplay.orders.resources.buildable_overrides;
import std;

export import engine.core.serialization.byte_stream;
export import games.generalszh.content.objects.object_definition;
import engine.ecs.system.system;

// What scripts made of object types' buildability (GameLogic::m_thingTemplateBuildableOverrides,
// TECHTREE_MODIFY_BUILDABILITY_OBJECT): by object type, its BuildableStatus now. Every reading of a type's buildability
// goes through them (ThingTemplate::getBuildable). Simulation state: checkpointed and hashed.
export namespace generalszh::gameplay
{
struct BuildableOverrides
{
	std::map<std::string, content::ObjectDefinition::Buildable, std::less<>> types;

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(types.size()));
		for (const auto &[type, status] : types)
		{
			writer.Text(type);
			writer.U8(static_cast<std::uint8_t>(status));
		}
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto count = reader.U32();
		if (!count)
			return false;
		std::map<std::string, content::ObjectDefinition::Buildable, std::less<>> loaded;
		for (std::uint32_t entry = 0; entry < *count; ++entry)
		{
			auto type = reader.Text();
			const auto status = reader.U8();
			if (!type || !status || *status > static_cast<std::uint8_t>(content::ObjectDefinition::Buildable::OnlyByAI))
				return false;
			loaded[std::move(*type)] = static_cast<content::ObjectDefinition::Buildable>(*status);
		}
		types = std::move(loaded);
		return true;
	}
};

// ThingTemplate::getBuildable: a script's status for the type, else its own.
inline content::ObjectDefinition::Buildable BuildableOf(const BuildableOverrides *overrides, const content::ObjectDefinition &what)
{
	if (overrides != nullptr)
		if (const auto found = overrides->types.find(what.name); found != overrides->types.end())
			return found->second;
	return what.buildable;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::BuildableOverrides>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.buildable_overrides";
};
}
