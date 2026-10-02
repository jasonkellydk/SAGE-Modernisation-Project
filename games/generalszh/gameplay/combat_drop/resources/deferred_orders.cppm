export module games.generalszh.gameplay.combat_drop.resources.deferred_orders;
import std;

export import engine.ecs.core.entity;
export import engine.net.lockstep.protocol;
export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Orders kept for a transport busy with a combat drop (ChinookAIUpdate::aiDoCommand while DOING_COMBAT_DROP:
// m_pendingCommand, the last one given, carried out once the drop is over; an order given it directly since
// (passItThru) forgets it). Each: the unit, the player who gave it, and the order (as it travels the bus: its units
// just that one). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct DeferredOrder
{
	ecs::Entity unit;
	std::uint32_t player{0};
	engine::net::CommandEnvelope order;
};

struct DeferredOrders
{
	std::vector<DeferredOrder> list;

	void Keep(ecs::Entity unit, std::uint32_t player, engine::net::CommandEnvelope order)
	{
		for (DeferredOrder &kept : list)
			if (kept.unit == unit)
			{
				kept.player = player;
				kept.order = std::move(order);
				return;
			}
		list.push_back({unit, player, std::move(order)});
	}

	void Forget(ecs::Entity unit)
	{
		std::erase_if(list, [&](const DeferredOrder &kept) { return kept.unit == unit; });
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(list.size()));
		for (const DeferredOrder &kept : list)
		{
			writer.U32(kept.unit.index);
			writer.U32(kept.unit.generation);
			writer.U32(kept.player);
			writer.U64(kept.order.type);
			writer.U32(kept.order.player);
			writer.Blob(kept.order.payload);
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		list.clear();
		const auto count = reader.U32();
		if (!count)
			return false;
		for (std::uint32_t index = 0; index < *count; ++index)
		{
			const auto unitIndex = reader.U32();
			const auto generation = reader.U32();
			const auto player = reader.U32();
			const auto type = reader.U64();
			const auto sender = reader.U32();
			auto payload = reader.Blob();
			if (!unitIndex || !generation || !player || !type || !sender || !payload)
				return false;
			ecs::Entity unit;
			unit.index = *unitIndex;
			unit.generation = *generation;
			list.push_back({unit, *player, engine::net::CommandEnvelope{*type, *sender, std::move(*payload)}});
		}
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::DeferredOrders>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.deferred_orders";
};
}
