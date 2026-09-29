export module engine.gameplay.common.areas.resources.area_activity;
import std;

export import engine.core.serialization.byte_stream;
export import engine.ecs.system.chunk_outputs;
import engine.ecs.system.system;

// When objects last changed which trigger areas hold what (GameLogic::m_frameObjectsChangedTriggerAreas: an object
// entering or leaving an area, or one that is no projectile or inert made or destroyed); what looks inside areas only
// just after a change reads it (AIGuardMachine::lookForInnerTarget). AreaChanges: the tick's entries and exits, per
// chunk of the area presence system, folded in after it. Simulation state: checkpointed.
export namespace engine::gameplay
{
struct AreaActivity
{
	std::uint64_t lastChange{0};

	void Save(engine::core::serialization::ByteWriter &writer) const { writer.U64(lastChange); }
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto value = reader.U64();
		if (!value)
			return false;
		lastChange = *value;
		return true;
	}
};

struct AreaChanges : ecs::ChunkOutputs<std::uint8_t>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::AreaActivity>
{
	static constexpr std::string_view StableName = "engine.gameplay.area_activity";
};

template<>
struct ResourceTraits<engine::gameplay::AreaChanges>
{
	static constexpr std::string_view StableName = "engine.gameplay.area_changes";
};
}
