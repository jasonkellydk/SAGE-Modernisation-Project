export module engine.gameplay.rts.death.resources.death_events;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.rts.death.definitions.death_definition;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.TurnAngle;
import engine.ecs.system.system;

// What dying entities do this tick, in deterministic order: effects to play,
// objects to create and weapons to fire where they are (the game carries
// them out after the step).
export namespace engine::gameplay
{
inline constexpr std::uint32_t NoTeam = 0xFFFFFFFFu;

struct DeathEvent
{
	ecs::Entity entity;
	ecs::Entity killer;
	DeathEffectKind kind{DeathEffectKind::Effect};
	std::uint32_t id{0};
	Engine::Math::FixedVector3 position;
	Engine::Math::TurnAngle facing;
	// The team it was on (what it creates is its player's); NoTeam when none.
	std::uint32_t team{NoTeam};
	// Its veterancy level as it died (what inherits its veterancy takes it: an ejected pilot).
	std::uint32_t veterancy{0};
	// An effect: played facing `facing` on the entity; else unrotated at `position` (FXListDie OrientToObject No).
	bool orient{true};
	// Objects made taking over its damage (its maximum less its health before the killing blow, from its last
	// attacker) and its attackers (CreateObjectDie TransferPreviousHealth).
	bool transfer{false};
	Engine::Math::Fixed transferDamage;
	Engine::Math::Fixed transferSubdual; // its subdual damage (getCurrentSubdualDamageAmount), passed on first
	ecs::Entity transferSource;
	// A notice: whom it credits (its DeathCredit; none: no one). A release or a weapon: its producer (none: nothing made it).
	ecs::Entity credit;
	// What it was (its definition; none: 0xFFFFFFFF) and whether it was still being built.
	std::uint32_t definition{0xFFFFFFFFu};
	bool underConstruction{false};
};

// From the moment of death (the death system).
struct DeathEvents
{
	std::vector<DeathEvent> events;
	// Died this tick and removed at once.
	std::vector<ecs::Entity> removed;
};

// From slow deaths as they play out (per chunk, merged in chunk order).
using DyingEvents = ecs::ChunkOutputs<DeathEvent>;

// From toppling structures as they fall (per chunk, merged in chunk order).
struct StructureToppleEvents : ecs::ChunkOutputs<DeathEvent>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::DeathEvents>
{
	static constexpr std::string_view StableName = "engine.gameplay.death_events";
};
template<>
struct ResourceTraits<engine::gameplay::DyingEvents>
{
	static constexpr std::string_view StableName = "engine.gameplay.dying_events";
};
template<>
struct ResourceTraits<engine::gameplay::StructureToppleEvents>
{
	static constexpr std::string_view StableName = "engine.gameplay.structure_topple_events";
};
}
