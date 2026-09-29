module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

module engine.ecs.core.world;

// World checkpoints: the exact storage layout (archetypes in signature order,
// their chunks in order, rows in order, which chunks take new entities next)
// plus the entity allocator, so a loaded world is indistinguishable from the
// saved one: same iteration order, same future entity IDs, same StateHash.
namespace ecs
{
namespace
{
using engine::core::serialization::ByteReader;
using engine::core::serialization::ByteWriter;

constexpr std::uint32_t CheckpointMagic = 0x314B4345u; // "ECK1"
}

extern "C++"
{
void World::SaveCheckpoint(ByteWriter &writer) const
{
	RequireComponentsFinalized();
	if (m_scheduledExecutionActive)
		throw std::logic_error("ECS checkpoints are taken between scheduled executions");
	writer.U32(CheckpointMagic);
	writer.U64(m_components.SchemaHash());
	writer.U64(m_config.chunkCapacity);

	writer.U64(m_records.size());
	for (const EntityRecord &record : m_records)
	{
		writer.U32(record.generation);
		writer.U8(static_cast<std::uint8_t>((record.alive ? 1u : 0u) | (record.retired ? 2u : 0u)));
	}
	writer.U64(m_freeIndices.size());
	for (const EntityIndex index : m_freeIndices)
		writer.U32(index);

	const std::vector<const Archetype *> archetypes = m_archetypes.GetArchetypes();
	writer.U64(archetypes.size());
	for (const Archetype *archetype : archetypes)
	{
		const auto &columns = archetype->Layout().Columns();
		for (const auto &column : columns)
		{
			const ComponentInfo &info = m_components.Get(column.component);
			if (info.persistence == PersistencePolicy::Serializable && info.saveState == nullptr && archetype->EntityCount() != 0)
				throw std::logic_error("ECS component '" + std::string(info.stableName) +
					"' is Serializable but neither trivially copyable nor provides ComponentTraits::Save/Load");
			if (info.persistence == PersistencePolicy::Serializable && info.savesPadding && archetype->EntityCount() != 0)
				throw std::logic_error("ECS component '" + std::string(info.stableName) +
					"' is saved as bytes but has padding or floats; add reserved fields or provide ComponentTraits::Save/Load");
		}
		writer.U64(archetype->GetSignature().size());
		for (const ComponentId component : archetype->GetSignature())
			writer.U64(m_components.Get(component).stableKey);
		const auto &chunks = archetype->Chunks();
		writer.U64(chunks.size());
		for (const auto &chunk : chunks)
		{
			writer.U64(chunk->Size());
			for (std::size_t row = 0; row < chunk->Size(); ++row)
			{
				writer.U32(chunk->Entities()[row].index);
				writer.U32(chunk->Entities()[row].generation);
			}
			for (std::size_t column = 0; column < columns.size(); ++column)
			{
				const ComponentInfo &info = m_components.Get(columns[column].component);
				if (info.persistence == PersistencePolicy::Serializable && chunk->Size() != 0)
					info.saveState(chunk->ComponentData(column), chunk->Size(), writer);
			}
		}
		writer.U64(archetype->m_availableChunks.size());
		for (const Chunk *available : archetype->m_availableChunks)
			for (std::size_t index = 0; index < chunks.size(); ++index)
				if (chunks[index].get() == available)
					writer.U64(index);
	}
}

bool World::LoadCheckpoint(ByteReader &reader)
{
	RequireComponentsFinalized();
	RequireStructuralMutationAllowed();
	if (!m_records.empty())
		throw std::logic_error("ECS checkpoints load into a world that has never held entities");

	// Counts come from the stream: bound them before allocating.
	const auto bounded = [](std::optional<std::uint64_t> value, std::uint64_t limit) -> std::optional<std::size_t> {
		if (!value || *value > limit)
			return std::nullopt;
		return static_cast<std::size_t>(*value);
	};
	constexpr std::uint64_t MaxCount = std::uint64_t{1} << 32;

	const auto magic = reader.U32();
	const auto schema = reader.U64();
	const auto capacity = reader.U64();
	if (magic != CheckpointMagic || schema != m_components.SchemaHash() || capacity != m_config.chunkCapacity)
		return false;

	const auto recordCount = bounded(reader.U64(), Entity::InvalidIndex);
	if (!recordCount)
		return false;
	std::vector<EntityRecord> records(*recordCount);
	std::size_t alive = 0;
	for (EntityRecord &record : records)
	{
		const auto generation = reader.U32();
		const auto flags = reader.U8();
		if (!generation || !flags || *flags > 3u)
			return false;
		record.generation = *generation;
		record.alive = (*flags & 1u) != 0;
		record.retired = (*flags & 2u) != 0;
		alive += record.alive ? 1u : 0u;
	}
	const auto freeCount = bounded(reader.U64(), *recordCount);
	if (!freeCount)
		return false;
	std::vector<EntityIndex> freeIndices(*freeCount);
	for (EntityIndex &index : freeIndices)
	{
		const auto value = reader.U32();
		if (!value || *value >= *recordCount || records[*value].alive)
			return false;
		index = *value;
	}

	m_records = std::move(records);
	m_freeIndices = std::move(freeIndices);
	m_entityCount = alive;
	std::size_t placed = 0;
	const auto fail = [&] {
		ClearForFailedLoad();
		return false;
	};

	const auto archetypeCount = bounded(reader.U64(), MaxCount);
	if (!archetypeCount)
		return fail();
	for (std::size_t archetypeIndex = 0; archetypeIndex < *archetypeCount; ++archetypeIndex)
	{
		const auto signatureSize = bounded(reader.U64(), m_components.Count());
		if (!signatureSize)
			return fail();
		Signature signature;
		for (std::size_t index = 0; index < *signatureSize; ++index)
		{
			const auto key = reader.U64();
			const ComponentId component = key ? m_components.TryGet(ComponentKey{*key}) : InvalidComponentId;
			if (component == InvalidComponentId)
				return fail();
			signature.push_back(component);
		}
		CanonicalizeSignature(signature);
		if (signature.size() != *signatureSize)
			return fail();
		Archetype &archetype = GetOrCreateArchetype(signature);
		const auto &columns = archetype.Layout().Columns();
		const auto chunkCount = bounded(reader.U64(), MaxCount);
		if (!chunkCount || !archetype.m_chunks.empty())
			return fail();
		for (std::size_t chunkIndex = 0; chunkIndex < *chunkCount; ++chunkIndex)
		{
			archetype.m_chunks.push_back(std::make_unique<Chunk>(ChunkLayout(archetype.m_layout), archetype.m_components));
			Chunk &chunk = *archetype.m_chunks.back();
			const auto size = bounded(reader.U64(), chunk.Capacity());
			if (!size)
				return fail();
			for (std::size_t row = 0; row < *size; ++row)
			{
				const auto index = reader.U32();
				const auto generation = reader.U32();
				if (!index || !generation || *index >= m_records.size())
					return fail();
				EntityRecord &record = m_records[*index];
				if (!record.alive || record.generation != *generation || record.location.archetype != nullptr)
					return fail();
				const std::size_t reserved = chunk.ReserveRow();
				for (std::size_t column = 0; column < columns.size(); ++column)
				{
					const ComponentInfo &info = m_components.Get(columns[column].component);
					if (info.constructDefault == nullptr)
					{
						chunk.CancelRow(reserved);
						return fail();
					}
				}
				for (std::size_t column = 0; column < columns.size(); ++column)
					m_components.Get(columns[column].component)
						.constructDefault(static_cast<std::byte *>(chunk.ComponentData(column)) + reserved * columns[column].size);
				chunk.PublishRow(Entity{*index, *generation}, reserved);
				++archetype.m_entityCount;
				record.location = EntityLocation{&archetype, &chunk, reserved};
				++placed;
			}
			for (std::size_t column = 0; column < columns.size(); ++column)
			{
				const ComponentInfo &info = m_components.Get(columns[column].component);
				if (info.persistence != PersistencePolicy::Serializable || *size == 0)
					continue;
				if (info.loadState == nullptr || !info.loadState(chunk.ComponentData(column), *size, reader))
					return fail();
			}
		}
		const auto availableCount = bounded(reader.U64(), *chunkCount);
		if (!availableCount)
			return fail();
		for (std::size_t index = 0; index < *availableCount; ++index)
		{
			const auto chunkIndex = bounded(reader.U64(), *chunkCount - 1);
			if (!chunkIndex || archetype.m_chunks[*chunkIndex]->IsFull())
				return fail();
			archetype.m_availableChunks.push_back(archetype.m_chunks[*chunkIndex].get());
		}
	}
	// Every live entity placed exactly once.
	if (placed != alive || reader.Failed())
		return fail();
	return true;
}

void World::ClearForFailedLoad() noexcept
{
	// Chunks destroy their rows; empty archetypes are harmless.
	for (Archetype *archetype : m_archetypes.GetArchetypes())
	{
		archetype->m_availableChunks.clear();
		archetype->m_chunks.clear();
		archetype->m_entityCount = 0;
	}
	m_records.clear();
	m_freeIndices.clear();
	m_entityCount = 0;
}
}
}
