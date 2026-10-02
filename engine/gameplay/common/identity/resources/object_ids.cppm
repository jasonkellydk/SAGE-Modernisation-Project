export module engine.gameplay.common.identity.resources.object_ids;
import std;

export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// The next object id to hand out (GameLogic::allocateObjectID). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct ObjectIds
{
	std::uint32_t next{1};

	std::uint32_t Allocate() noexcept { return next++; }

	void Save(engine::core::serialization::ByteWriter &writer) const { writer.U32(next); }
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto value = reader.U32();
		if (!value)
			return false;
		next = *value;
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::ObjectIds>
{
	static constexpr std::string_view StableName = "engine.gameplay.object_ids";
};
}
