export module engine.ecs.system.chunk_outputs;
import std;

import engine.ecs.system.system;

export namespace ecs
{
// Deterministic per-chunk output for parallel systems: events, reductions,
// candidate lists. Each chunk job appends only to its own slot, so there is
// no contention, and readers see the results in chunk order, so the outcome
// never depends on thread count or completion timing.
//
// Typical use inside one system:
//   BeforeChunks: outputs.Reset(query.PreparedChunkCount());
//   Execute:      outputs.Slot(context).push_back(...);
//   AfterChunks:  outputs.ForEach(...) to reduce or publish.
// As an event stream between systems it is a resource: the owner is injected
// into the producer (declared ecs::Write) and the consumers (ecs::Read), so the
// scheduler runs consumers after the producer's wave.
template<typename T>
class ChunkOutputs
{
public:
	// Clears every slot but keeps allocations for the next tick.
	void Reset(std::size_t chunkCount)
	{
		if (m_slots.size() < chunkCount)
			m_slots.resize(chunkCount);
		for (std::vector<T> &slot : m_slots)
			slot.clear();
		m_active = chunkCount;
	}

	std::vector<T> &Slot(const SystemContext &context)
	{
		const std::size_t chunk = context.ChunkOrder();
		if (chunk >= m_active)
			throw std::out_of_range("ChunkOutputs slot outside the prepared chunk range; call Reset in BeforeChunks");
		return m_slots[chunk];
	}

	// A slot by index, for producers that are not chunk jobs (batch systems,
	// tests): Reset(n) first, then fill slots 0..n-1 in order.
	std::vector<T> &SlotAt(std::size_t chunk)
	{
		if (chunk >= m_active)
			throw std::out_of_range("ChunkOutputs slot outside the prepared chunk range; call Reset first");
		return m_slots[chunk];
	}

	template<typename Function>
	void ForEach(Function &&function) const
	{
		for (std::size_t chunk = 0; chunk < m_active; ++chunk)
			for (const T &value : m_slots[chunk])
				function(value);
	}

	void AppendTo(std::vector<T> &out) const
	{
		ForEach([&](const T &value) { out.push_back(value); });
	}

	// How many slots the last Reset prepared.
	std::size_t SlotCount() const noexcept { return m_active; }

	std::size_t Size() const noexcept
	{
		std::size_t total = 0;
		for (std::size_t chunk = 0; chunk < m_active; ++chunk)
			total += m_slots[chunk].size();
		return total;
	}

private:
	std::vector<std::vector<T>> m_slots;
	std::size_t m_active{0};
};
}
