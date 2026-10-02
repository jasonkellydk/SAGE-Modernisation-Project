export module engine.gameplay.rts.death.resources.hulk_lifetime;
import std;

export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// How long hulks may last, as a script wants it (GameLogic::m_scriptHulkMaxLifetimeOverride,
// SCRIPTING_OVERRIDE_HULK_LIFETIME): in ticks; below zero: their own. A hulk made meanwhile lives exactly that long
// (LifetimeUpdate), and one that dies slowly sinks at once (SlowDeathBehavior::beginSlowDeath). Simulation state:
// checkpointed.
export namespace engine::gameplay
{
struct HulkLifetime
{
	std::int64_t overrideTicks{-1};

	bool Overridden() const noexcept { return overrideTicks != -1; }
	void Save(engine::core::serialization::ByteWriter &writer) const { writer.I64(overrideTicks); }
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto ticks = reader.I64();
		if (!ticks)
			return false;
		overrideTicks = *ticks;
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::HulkLifetime>
{
	static constexpr std::string_view StableName = "engine.gameplay.hulk_lifetime";
};
}
