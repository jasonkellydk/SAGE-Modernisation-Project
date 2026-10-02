export module engine.gameplay.rts.economy.resources.overcharge_events;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
import engine.ecs.system.system;

// Overcharging's settings (the damage its drain does: the game's DAMAGE_PENALTY, DEATH_NORMAL; the ticks a second its
// drain is spread over: LOGICFRAMES_PER_SECOND) and this tick's
// overcharges that ran out (health below their floor: OverchargeBehavior::update's GUI:OverchargeExhausted), per chunk.
export namespace engine::gameplay
{
struct OverchargeSettings
{
	std::uint32_t damageType{0};
	std::uint32_t deathType{0};
	std::uint32_t ticksPerSecond{30};
};

struct OverchargeExhausted
{
	ecs::Entity entity;
};

struct OverchargeEvents : ecs::ChunkOutputs<OverchargeExhausted>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::OverchargeSettings>
{
	static constexpr std::string_view StableName = "engine.gameplay.overcharge_settings";
};
template<>
struct ResourceTraits<engine::gameplay::OverchargeEvents>
{
	static constexpr std::string_view StableName = "engine.gameplay.overcharge_events";
};
}
