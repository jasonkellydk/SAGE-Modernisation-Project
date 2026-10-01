export module games.generalszh.gameplay.world.resources.water_changes;
import std;

export import Engine.Core.Math.Fixed;
export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// The water tables rising or falling (TerrainLogic's m_waterToUpdate: at most MAX_DYNAMIC_WATER, 64): which water area,
// its change a frame, its target, its damage to what ends up underwater and its height kept exactly (the area's own
// height is whole units). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct WaterChange
{
	std::uint32_t area{0};
	std::uint32_t reserved{0};
	Engine::Math::Fixed changePerFrame;
	Engine::Math::Fixed target;
	Engine::Math::Fixed damage;
	Engine::Math::Fixed current;
};

struct WaterChanges
{
	static constexpr std::size_t Max = 64;
	std::vector<WaterChange> list;
	// A water table set at once this tick (setWaterHeight with forcePathfindUpdate): the pathfinding map is remade.
	std::uint32_t refresh{0};

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(refresh);
		writer.U32(static_cast<std::uint32_t>(list.size()));
		for (const WaterChange &change : list)
		{
			writer.U32(change.area);
			for (const Engine::Math::Fixed value : {change.changePerFrame, change.target, change.damage, change.current})
				writer.I64(value.Raw());
		}
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		list.clear();
		const auto flag = reader.U32();
		if (!flag)
			return false;
		refresh = *flag;
		const auto count = reader.U32();
		if (!count)
			return false;
		for (std::uint32_t index = 0; index < *count; ++index)
		{
			WaterChange change;
			const auto area = reader.U32();
			if (!area)
				return false;
			change.area = *area;
			for (Engine::Math::Fixed *value : {&change.changePerFrame, &change.target, &change.damage, &change.current})
			{
				const auto raw = reader.I64();
				if (!raw)
					return false;
				*value = Engine::Math::Fixed::FromRaw(*raw);
			}
			list.push_back(change);
		}
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::WaterChanges>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.water_changes";
};
}
