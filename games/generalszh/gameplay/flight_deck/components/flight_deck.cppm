export module games.generalszh.gameplay.flight_deck.components.flight_deck;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import engine.ecs.core.world;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// An aircraft carrier's flight deck (FlightDeckBehavior), as data on the carrier (its parking itself is its Airfield):
//   built: its jets made (buildInfo: one in every space, the first update);
//   the order it was given (m_designatedCommand, its target and position: aiDoCommand takes a script's or a player's
//     guard, attack-position, attack-move, attack, force-attack and idle; any other order clears it; its own AI's are
//     ignored) and whether it is still to be passed on to the jets already off the deck (propagateOrdersToPlanes);
//   per runway the launch ramp (m_rampUp and the frames it is up by, the next launch wave, the catapult's steam and
//     the ramp's lowering) and its door condition (DOOR_2 / DOOR_3: opening while up, closing once lowered);
//   when the queue is next moved up, when its parked jets are next healed (and which spaces were healing: a new one
//     puts the heal off again, resetWakeFrame), and when it may queue its next replacement.
// DeckJets: the tick's jets of flight decks (their space, cycle and where they stand), gathered for the decks.
// FlightDeckEvents: what the decks did beyond themselves this tick (jets to make, orders to pass on, spaces swapped,
// replacements to queue, jets healed), carried out after the step.
// Simulation state: checkpointed.
export namespace generalszh::gameplay
{
enum class DeckOrder : std::uint8_t
{
	None,
	Idle,
	Guard,
	AttackPosition,
	AttackMove,
	Attack,
	ForceAttack,
};

struct FlightDeckRunway
{
	std::uint64_t rampUpTick{0};
	std::uint64_t nextLaunchWave{0};
	std::uint64_t catapultTick{0};
	std::uint64_t lowerRampTick{0};
	std::uint8_t rampUp{0};
	std::uint8_t door{0}; // 0 none, 1 opening, 2 closing
	std::uint8_t reserved[6]{};
};

struct FlightDeck
{
	static constexpr std::uint64_t Forever = 0x3FFFFFFFFFFFFFFFull;
	static constexpr std::size_t MaxRunways = 4;
	std::uint8_t built{0};
	DeckOrder command{DeckOrder::None};
	std::uint8_t propagate{0};
	std::uint8_t reserved{0};
	std::uint32_t healing{0}; // the spaces healing at the last heal
	ecs::Entity target;
	Engine::Math::FixedVector2 position;
	std::uint64_t nextCleanup{0};
	std::uint64_t nextHeal{Forever};
	std::uint64_t nextAllowedProduction{0};
	std::array<FlightDeckRunway, MaxRunways> runways{};
};

struct DeckJet
{
	ecs::Entity carrier;
	ecs::Entity jet;
	std::uint32_t space{0};
	std::uint32_t state{0}; // its JetState
	Engine::Math::FixedVector3 at;
	std::uint8_t loaded{1};
	std::uint8_t reserved[7]{};
};

struct DeckJets : ecs::ChunkOutputs<DeckJet>
{
};

struct FlightDeckEvent
{
	enum class Kind : std::uint8_t
	{
		MakeJets, // buildInfo: a jet in every space
		Order,    // pass the deck's order on to `jet` (`launch`: it is off the deck now)
		Swap,     // `jet` moves up to `space`; `other` (none: nobody) takes its old space
		Queue,    // a replacement queued
		Heal,     // `jet` healed by `amount`
	};
	Kind kind{Kind::MakeJets};
	ecs::Entity carrier;
	ecs::Entity jet;
	ecs::Entity other;
	std::uint32_t space{0};
	std::uint8_t launch{0};
	DeckOrder command{DeckOrder::None};
	ecs::Entity target;
	Engine::Math::FixedVector2 position;
	Engine::Math::Fixed amount;
};

struct FlightDeckEvents : ecs::ChunkOutputs<FlightDeckEvent>
{
};

// FlightDeckBehavior::aiDoCommand for an order the carrier was given: false when it has no flight deck (the order goes
// on as usual). Its own AI's orders are ignored; a guard, attack-position, attack-move, attack or force-attack is
// remembered with its target or position (idle forgets both) and passed on to its jets off the deck; anything else
// forgets it.
inline bool DesignateFlightDeck(ecs::World &world, ecs::Entity carrier, DeckOrder command, ecs::Entity target, Engine::Math::FixedVector2 position,
	bool fromAi)
{
	FlightDeck *deck = world.IsAlive(carrier) ? world.Get<FlightDeck>(carrier) : nullptr;
	if (deck == nullptr)
		return false;
	if (fromAi)
		return true;
	deck->command = command;
	deck->target = command == DeckOrder::Attack || command == DeckOrder::ForceAttack ? target : ecs::Entity{};
	deck->position = command == DeckOrder::Guard || command == DeckOrder::AttackPosition || command == DeckOrder::AttackMove ? position
																												   : Engine::Math::FixedVector2{};
	deck->propagate = command != DeckOrder::None ? 1 : 0;
	return true;
}
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::FlightDeck>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.flight_deck";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::FlightDeck &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(std::uint64_t{value.built} | (std::uint64_t(value.command) << 8) | (std::uint64_t{value.propagate} << 16) |
			(std::uint64_t{value.healing} << 32));
		hasher.AppendU64((std::uint64_t{value.target.index} << 32) | value.target.generation);
		hasher.AppendU64(static_cast<std::uint64_t>(value.position.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.position.y.Raw()));
		hasher.AppendU64(value.nextCleanup);
		hasher.AppendU64(value.nextHeal);
		hasher.AppendU64(value.nextAllowedProduction);
		for (const auto &runway : value.runways)
		{
			hasher.AppendU64(runway.rampUpTick);
			hasher.AppendU64(runway.nextLaunchWave);
			hasher.AppendU64(runway.catapultTick);
			hasher.AppendU64(runway.lowerRampTick);
			hasher.AppendU64(std::uint64_t{runway.rampUp} | (std::uint64_t{runway.door} << 8));
		}
	}
};
template<>
struct ResourceTraits<generalszh::gameplay::DeckJets>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.deck_jets";
};
template<>
struct ResourceTraits<generalszh::gameplay::FlightDeckEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.flight_deck_events";
};
}
