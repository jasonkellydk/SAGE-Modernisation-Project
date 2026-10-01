export module games.generalszh.gameplay.flight_deck.algorithms.flight_decks;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.flight_deck.components.flight_deck;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.objects.resources.object_templates;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.ai.algorithms.guards;
import engine.gameplay.rts.aircraft.components.jet;
import engine.gameplay.rts.aircraft.components.airfield;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.health.components.health;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.production.components.production_queue;

// The flight decks' events, after the step (FlightDeckBehavior):
//   MakeJets (buildInfo): a PayloadTemplate jet in every space in order, on the carrier's controlling player's default
//     team, made by the carrier, standing at its space (its prep bone) facing as the bone does, parked.
//   Order (propagateOrderToSpecificPlane): guard -> aiGuardPosition, attack position -> aiAttackPosition, attack or
//     force attack -> aiForceAttackObject, attack move -> aiAttackMoveToPosition, idle -> aiEnter the carrier (back to
//     land); one launched (off the deck now) takes off at once.
//   Swap (the queue moving up): the jet takes the space and taxies straight up to it (REASSIGN_PARKING: to its prep);
//     the other (off the deck, if any) takes the old one.
//   Queue: PayloadTemplate queued at the carrier (ProductionUpdate::queueCreateUnit: free), for its default team.
//   Heal (attemptHealing): up to its maximum.
export namespace generalszh::gameplay
{
inline void ApplyFlightDeckEvents(GameWorld &game, const FlightDeckEvents &events)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	events.ForEach([&](const FlightDeckEvent &event) {
		if (!world.IsAlive(event.carrier))
			return;
		const gp::Airfield *field = world.Get<gp::Airfield>(event.carrier);
		const auto *ref = world.Get<gp::DefinitionRef>(event.carrier);
		const content::FlightDeckContent *config = ref != nullptr ? game.templates.FlightDeckOf(ref->index) : nullptr;
		if (field == nullptr || config == nullptr)
			return;
		const auto *owner = world.Get<gp::Owner>(event.carrier);
		switch (event.kind)
		{
		case FlightDeckEvent::Kind::MakeJets:
		{
			const std::uint32_t team = owner != nullptr ? game.roster.DefaultTeam(owner->player).value_or(0) : 0u;
			for (std::uint32_t space = 0; space < field->spaceCount; ++space)
			{
				const gp::ParkingSpace spot = world.Get<gp::Airfield>(event.carrier)->spaces[space];
				const ecs::Entity jet = SpawnObject(game, config->payload, spot.prep.XY(), spot.parkingFacing, team, "");
				if (!world.IsAlive(jet))
					continue;
				SetProducer(game, jet, event.carrier);
				auto &transform = *world.Get<gp::Transform>(jet);
				transform.position = spot.prep;
				transform.facing = spot.parkingFacing;
				if (gp::Jet *state = world.Get<gp::Jet>(jet))
				{
					state->airfield = event.carrier;
					state->space = space;
					state->state = gp::JetState::Parked;
					state->since = game.tick;
					state->goal = spot.prep.XY();
					if (auto *motion = world.Get<gp::Locomotion>(jet))
						motion->locomotor = state->taxi;
				}
			}
			break;
		}
		case FlightDeckEvent::Kind::Order:
		{
			if (!world.IsAlive(event.jet))
				return;
			gp::Jet *jet = world.Get<gp::Jet>(event.jet);
			switch (event.command)
			{
			case DeckOrder::Guard: GuardPosition(game, event.jet, event.position); break;
			case DeckOrder::AttackPosition:
				OrderAttackPosition(game, event.jet, {event.position.x, event.position.y, game.ground.At(event.position)}, 0, false);
				break;
			case DeckOrder::Attack:
			case DeckOrder::ForceAttack: OrderAttack(game, event.jet, event.target, 0, gp::CommandSource::Player); break;
			case DeckOrder::AttackMove: OrderAttackMove(game, event.jet, event.position); break;
			case DeckOrder::Idle:
				if (jet != nullptr)
					jet->order = gp::Jet::Recall;
				break;
			default: break;
			}
			if (event.launch != 0 && jet != nullptr && event.command != DeckOrder::Idle)
				jet->order = gp::Jet::Scramble;
			break;
		}
		case FlightDeckEvent::Kind::Swap:
		{
			gp::Jet *moving = world.IsAlive(event.jet) ? world.Get<gp::Jet>(event.jet) : nullptr;
			if (moving == nullptr || event.space >= field->spaceCount)
				return;
			const std::uint32_t old = moving->space;
			moving->space = event.space;
			const gp::ParkingSpace &spot = field->spaces[event.space];
			if (moving->state == gp::JetState::Parked || moving->state == gp::JetState::Reloading)
			{
				moving->state = gp::JetState::TaxiToParking;
				moving->route = 2; // straight up to its new space
				moving->leg = 0;
				moving->since = game.tick;
				moving->goal = spot.prep.XY();
				if (auto *motion = world.Get<gp::Locomotion>(event.jet))
					motion->locomotor = moving->taxi;
			}
			if (gp::Jet *other = world.IsAlive(event.other) ? world.Get<gp::Jet>(event.other) : nullptr)
				other->space = old;
			break;
		}
		case FlightDeckEvent::Kind::Queue:
		{
			auto *queue = world.Get<gp::ProductionQueue>(event.carrier);
			const content::ObjectDefinition *unit = game.templates.Content().objects.Find(config->payload);
			if (queue == nullptr || unit == nullptr || queue->Full())
				return;
			const std::uint32_t team = owner != nullptr ? game.roster.DefaultTeam(owner->player).value_or(0) : 0u;
			const std::uint64_t ticks = std::max<std::int64_t>(1, (unit->buildTimeSeconds * Engine::Math::Fixed::FromInt(game.step.TicksPerSecond())).Ceil());
			queue->Push(gp::ProductionEntry{game.templates.Definition(*unit), team, 1, queue->nextId++, {}, ticks});
			break;
		}
		case FlightDeckEvent::Kind::Heal:
			if (auto *health = world.IsAlive(event.jet) ? world.Get<gp::Health>(event.jet) : nullptr; health != nullptr && !gp::IsDead(*health))
				health->current = std::min(health->maximum, health->current + event.amount);
			break;
		}
	});
}
}
