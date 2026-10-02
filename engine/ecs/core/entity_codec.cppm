export module engine.ecs.core.entity_codec;
import std;

export import engine.ecs.core.entity;
export import engine.core.serialization.byte_stream;

// Entity handles in checkpoints and messages: index then generation.
export namespace ecs
{
void WriteEntity(engine::core::serialization::ByteWriter &writer, Entity entity)
{
	writer.U32(entity.index);
	writer.U32(entity.generation);
}

std::optional<Entity> ReadEntity(engine::core::serialization::ByteReader &reader)
{
	const auto index = reader.U32();
	const auto generation = reader.U32();
	if (!index || !generation)
		return std::nullopt;
	return Entity{*index, *generation};
}
}
