export module engine.gameplay.rts.aircraft.resources.jet_damage;
import std;

export import engine.gameplay.common.health.resources.incoming_damage;
import engine.ecs.system.system;

// The damage jets circling a dead airfield take this tick (JetOrHeliCirclingDeadAirfieldState: out of ammo with
// nowhere to land), for the jet damage system to add to the tick's incoming damage.
export namespace engine::gameplay
{
struct JetDamage
{
	std::vector<DamageRecord> records;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::JetDamage>
{
	static constexpr std::string_view StableName = "engine.gameplay.jet_damage";
};
}
