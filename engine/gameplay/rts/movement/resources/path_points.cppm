export module engine.gameplay.rts.movement.resources.path_points;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
export import engine.core.serialization.byte_stream;
import engine.ecs.core.entity_codec;
import engine.ecs.system.system;

// The points of the paths things follow (the original's goal path: AIStateMachine::m_goalPath, a vector as long as it
// needs to be), as structure of arrays: every path's points in two columns (x, y), each path a block of them owned by
// the thing following it (MovePath::block). A block is taken first-fit from the freed ones (lowest index first), else
// added; a path outgrowing its block moves to one twice the size. Blocks whose owner no longer follows them are freed
// once a tick (PathPointsSweepSystem). Simulation state: checkpointed and hashed.
export namespace engine::gameplay
{
class PathPoints
{
public:
	static constexpr std::uint32_t NoBlock = 0xFFFFFFFFu;

	// A block of at least `capacity` points for `owner`.
	std::uint32_t Allocate(ecs::Entity owner, std::uint32_t capacity)
	{
		capacity = (std::max)(capacity, MinimumCapacity);
		for (std::uint32_t block = 0; block < m_first.size(); ++block)
			if (m_used[block] == 0 && m_capacity[block] >= capacity)
			{
				m_used[block] = 1;
				m_owner[block] = owner;
				return block;
			}
		std::uint32_t rounded = MinimumCapacity;
		while (rounded < capacity)
			rounded *= 2;
		const auto block = static_cast<std::uint32_t>(m_first.size());
		m_first.push_back(static_cast<std::uint32_t>(m_x.size()));
		m_capacity.push_back(rounded);
		m_owner.push_back(owner);
		m_used.push_back(1);
		m_x.resize(m_x.size() + rounded);
		m_y.resize(m_y.size() + rounded);
		return block;
	}

	// The block, grown to hold `capacity` points (its first `count` kept): the same block when it already does.
	std::uint32_t Reserve(std::uint32_t block, std::uint32_t count, std::uint32_t capacity)
	{
		if (block < m_first.size() && m_capacity[block] >= capacity)
			return block;
		const ecs::Entity owner = block < m_owner.size() ? m_owner[block] : ecs::Entity{};
		const std::uint32_t grown = Allocate(owner, (std::max)(capacity, block < m_capacity.size() ? m_capacity[block] * 2 : capacity));
		for (std::uint32_t index = 0; index < count && block < m_first.size(); ++index)
			Set(grown, index, At(block, index));
		Free(block);
		return grown;
	}

	void Free(std::uint32_t block) noexcept
	{
		if (block < m_used.size())
		{
			m_used[block] = 0;
			m_owner[block] = {};
		}
	}

	Engine::Math::FixedVector2 At(std::uint32_t block, std::uint32_t index) const noexcept
	{
		const std::size_t at = m_first[block] + index;
		return {m_x[at], m_y[at]};
	}
	void Set(std::uint32_t block, std::uint32_t index, Engine::Math::FixedVector2 point) noexcept
	{
		const std::size_t at = m_first[block] + index;
		m_x[at] = point.x;
		m_y[at] = point.y;
	}

	std::uint32_t Capacity(std::uint32_t block) const noexcept { return block < m_capacity.size() ? m_capacity[block] : 0u; }
	std::uint32_t BlockCount() const noexcept { return static_cast<std::uint32_t>(m_first.size()); }
	bool Used(std::uint32_t block) const noexcept { return block < m_used.size() && m_used[block] != 0; }
	ecs::Entity Owner(std::uint32_t block) const noexcept { return block < m_owner.size() ? m_owner[block] : ecs::Entity{}; }

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_first.size()));
		for (std::size_t block = 0; block < m_first.size(); ++block)
		{
			writer.U32(m_first[block]);
			writer.U32(m_capacity[block]);
			writer.U8(m_used[block]);
			ecs::WriteEntity(writer, m_owner[block]);
		}
		writer.U32(static_cast<std::uint32_t>(m_x.size()));
		for (std::size_t index = 0; index < m_x.size(); ++index)
		{
			writer.I64(m_x[index].Raw());
			writer.I64(m_y[index].Raw());
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		PathPoints loaded;
		const auto blocks = reader.U32();
		if (!blocks)
			return false;
		for (std::uint32_t block = 0; block < *blocks && !reader.Failed(); ++block)
		{
			const auto first = reader.U32();
			const auto capacity = reader.U32();
			const auto used = reader.U8();
			const auto owner = ecs::ReadEntity(reader);
			if (!first || !capacity || !used || !owner)
				return false;
			loaded.m_first.push_back(*first);
			loaded.m_capacity.push_back(*capacity);
			loaded.m_used.push_back(*used);
			loaded.m_owner.push_back(*owner);
		}
		const auto points = reader.U32();
		if (!points)
			return false;
		for (std::uint32_t index = 0; index < *points && !reader.Failed(); ++index)
		{
			const auto x = reader.I64();
			const auto y = reader.I64();
			if (!x || !y)
				return false;
			loaded.m_x.push_back(Engine::Math::Fixed::FromRaw(*x));
			loaded.m_y.push_back(Engine::Math::Fixed::FromRaw(*y));
		}
		for (std::size_t block = 0; block < loaded.m_first.size(); ++block)
			if (static_cast<std::size_t>(loaded.m_first[block]) + loaded.m_capacity[block] > loaded.m_x.size())
				return false;
		*this = std::move(loaded);
		return true;
	}

private:
	static constexpr std::uint32_t MinimumCapacity = 4;
	std::vector<Engine::Math::Fixed> m_x;
	std::vector<Engine::Math::Fixed> m_y;
	std::vector<std::uint32_t> m_first;
	std::vector<std::uint32_t> m_capacity;
	std::vector<ecs::Entity> m_owner;
	std::vector<std::uint8_t> m_used;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::PathPoints>
{
	static constexpr std::string_view StableName = "engine.gameplay.path_points";
};
}
