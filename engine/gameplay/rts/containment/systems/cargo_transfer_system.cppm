export module engine.gameplay.rts.containment.systems.cargo_transfer_system;
import Engine.Core.Math.FixedRandom;
import engine.gameplay.common.random.resources.random_seed;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.common.physics.algorithms.forces;
import std;

export import engine.gameplay.rts.containment.components.drop_homing;
export import engine.ecs.system.system;
export import engine.gameplay.rts.containment.resources.drop_settings;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.containment.components.cargo_size;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.rts.movement.components.descent;
export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.rts.combat.components.aggression;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.rts.blocking.components.blocked_state;

// After the step: puts boarding units aboard (while there is room) and lets
// unloading passengers out, in the requests' deterministic order, through
// the tick's command buffer. Out of a landed transport a passenger steps
// clear of it; out of one in the air it comes down under a parachute.
// A networked transport (a tunnel) shares its room with its whole network,
// counts heads rather than slots and takes no aircraft
// (TunnelTracker::isValidContainerFor).
export namespace engine::gameplay
{
struct CargoTransferSystem
{
	using Query = ecs::Query<ecs::Read<Transport>>;
	using Lookup = ecs::Lookup<ecs::Read<Transport>, ecs::Read<Transform>, ecs::Read<CargoSize>, ecs::Read<Passenger>, ecs::Read<MoveOrder>,
		ecs::Read<AttackTarget>, ecs::Read<Targetable>, ecs::Read<PhysicsBody>, ecs::Read<Aggression>, ecs::Read<Owner>, ecs::Read<ExitIntent>,
		ecs::Read<DropHoming>, ecs::Read<Garrison>, ecs::Read<Attitude>, ecs::Read<BlockedState>>;
	using Resources = ecs::Resources<ecs::Read<DropSettings>, ecs::Read<BoardRequests>, ecs::Read<ExitRequests>, ecs::Read<RiderExits>, ecs::Read<DropExits>,
		ecs::Read<PlacedExits>, ecs::Read<IntentExits>, ecs::Write<CargoManifest>, ecs::Read<GroundHeight>, ecs::Read<RandomSeed>>;

	// How fast a parachute comes down (units per tick).

	void Execute(ecs::SystemContext &context)
	{
		const DropSettings &drop = context.Read<DropSettings>();
		const BoardRequests &boards = context.Read<BoardRequests>();
		const ExitRequests &exits = context.Read<ExitRequests>();
		const RiderExits &riderExits = context.Read<RiderExits>();
		CargoManifest &manifest = context.Write<CargoManifest>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		// Seats used this tick by the transports touched (several may board the
		// same one): scratch for this run only, written back to their Transport.
		std::vector<std::pair<ecs::Entity, std::uint32_t>> occupied;
		const auto load = [&](ecs::Entity carrier, const Transport &transport) -> std::uint32_t & {
			for (auto &[entity, used] : occupied)
				if (entity == carrier)
					return used;
			return occupied.emplace_back(carrier, transport.occupied).second;
		};
		// Heads in each network touched, and the networked transports touched (their room is the network's).
		std::vector<std::pair<std::uint32_t, std::uint32_t>> heads;
		std::vector<std::pair<ecs::Entity, std::uint32_t>> networked;
		const auto headsIn = [&](ecs::Entity carrier, std::uint32_t network) -> std::uint32_t & {
			if (std::find(networked.begin(), networked.end(), std::pair{carrier, network}) == networked.end())
				networked.emplace_back(carrier, network);
			for (auto &[id, count] : heads)
				if (id == network)
					return count;
			return heads.emplace_back(network, static_cast<std::uint32_t>(manifest.NetworkCount(network))).second;
		};
		const std::uint64_t tick = context.Tick();

		std::vector<std::pair<ecs::Entity, std::uint32_t>> entered; // this tick's last player in, per transport
		boards.ForEach([&](const BoardRequest &request) {
			if (!request.arrived || request.touchOnly || !lookup.IsAlive(request.passenger) || lookup.Get<Passenger>(request.passenger) != nullptr)
				return;
			commands.Remove<Boarding>(request.passenger);
			const Transport *transport = lookup.Get<Transport>(request.transport);
			if (transport == nullptr || transport->closed)
				return;
			const Targetable *kind = lookup.Get<Targetable>(request.passenger);
			const std::optional<std::uint32_t> network = manifest.NetworkOf(request.transport);
			if (network && kind != nullptr && (kind->classes & target_class::Aircraft) != 0)
				return;
			const CargoSize *size = lookup.Get<CargoSize>(request.passenger);
			const std::uint32_t slots = network ? 1u : size != nullptr ? size->slots : 1u;
			std::uint32_t &used = network ? headsIn(request.transport, *network) : load(request.transport, *transport);
			// A rider-change container (RiderChangeContain::isValidContainerFor: capacity unchecked) takes a new rider when full:
			// the game has the new one throw the old one off.
			if (used + slots > transport->definition.slots && !transport->definition.deletesRiders)
				return;
			used += slots;
			commands.Add<Passenger>(request.passenger, Passenger{request.transport, slots, 0, tick});
			// Inside, it may fire only where its carrier allows it (isPassengerAllowedToFire).
			const bool infantry = kind != nullptr && (kind->classes & target_class::Infantry) != 0;
			const bool armed = transport->definition.passengersFire && (!transport->definition.infantryOnly || infantry);
			// OpenContain::addToContain: an enclosing container takes it out of the world (addOrRemoveObjFromWorld); one that
			// does not (a fire base) leaves it there.
			commands.Add<OffMap>(request.passenger, OffMap{transport->definition.enclosesRiders != 0 ? off_map_reason::Contained : off_map_reason::Stationed,
				armed, {}, request.transport});
			if (lookup.Get<MoveOrder>(request.passenger) != nullptr)
				commands.Set<MoveOrder>(request.passenger, MoveOrder{});
			if (lookup.Get<AttackTarget>(request.passenger) != nullptr)
				commands.Set<AttackTarget>(request.passenger, AttackTarget{});
			manifest.Board(request.transport, request.passenger);
			// OpenContain::addToContain: the player who entered it, now.
			if (const Owner *rider = lookup.Get<Owner>(request.passenger))
			{
				auto found = std::find_if(entered.begin(), entered.end(), [&](const auto &entry) { return entry.first == request.transport; });
				if (found == entered.end())
					entered.emplace_back(request.transport, rider->player);
				else
					found->second = rider->player;
			}
		});

		std::vector<ecs::Entity> doorsOpened; // carriers someone got out of this tick (exitObjectViaDoor)
		std::vector<std::pair<ecs::Entity, std::uint32_t>> exitPathsTaken; // carriers' next exit path after this tick's
		// The next passenger out of an unloading transport, then those let out by name (healed).
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0x6A77u;
		// TransportContain::onRemoving: ExitBone puts it where that bone of the carrier is (turned with the carrier's
		// facing); OrientLikeContainerOnExit turns it the carrier's way, upright; KeepContainerVelocityOnExit pushes it
		// with the carrier's velocity times its mass (applyMotiveForce) and pitches it at its CenterOfMassOffset times
		// ExitPitchRate. `velocity`: the carrier's (a delivery's: how far it went this tick; else its body's). True when it
		// changed the body.
		const auto removing = [&](const Transport &transport, const Transform &from, ecs::Entity passenger, Transform &placed,
								  std::optional<PhysicsBody> &body, const Engine::Math::FixedVector3 &velocity) {
			const TransportDefinition &definition = transport.definition;
			if (definition.hasExitBone != 0)
			{
				const Engine::Math::Fixed c = Engine::Math::Cos(from.facing), s = Engine::Math::Sin(from.facing);
				const Engine::Math::FixedVector3 &bone = definition.exitBone;
				placed.position = from.position + Engine::Math::FixedVector3{bone.x * c - bone.y * s, bone.x * s + bone.y * c, bone.z};
			}
			if (definition.orientOnExit)
			{
				placed.facing = from.facing;
				if (lookup.Get<Attitude>(passenger) != nullptr)
					commands.Set<Attitude>(passenger, Attitude{});
			}
			if (!definition.keepVelocityOnExit || !body.has_value())
				return false;
			ApplyMotiveForce(*body, velocity * body->mass, context.Tick());
			body->pitchRate = static_cast<std::int32_t>(Engine::Math::TurnFromRadians(body->centerOfMassOffset * definition.exitPitchRate).units);
			return true;
		};
		const auto carrierVelocity = [&](ecs::Entity carrier) {
			const PhysicsBody *body = lookup.Get<PhysicsBody>(carrier);
			return body != nullptr ? body->velocity : Engine::Math::FixedVector3{};
		};
		const auto exit = [&](ecs::Entity carrier, ecs::Entity passenger, const DropExit *placement = nullptr) {
			const Transport *transport = lookup.Get<Transport>(carrier);
			const Transform *from = lookup.Get<Transform>(carrier);
			if (transport == nullptr || from == nullptr || !lookup.IsAlive(passenger))
				return;
			const Passenger *seat = lookup.Get<Passenger>(passenger);
			const std::optional<std::uint32_t> network = manifest.NetworkOf(carrier);
			std::uint32_t &used = network ? headsIn(carrier, *network) : load(carrier, *transport);
			used -= std::min(used, network ? 1u : seat != nullptr ? seat->slots : 1u);
			commands.Remove<Passenger>(passenger);
			commands.Remove<OffMap>(passenger);
			if (transport->definition.doorOpenTicks > 0 && std::find(doorsOpened.begin(), doorsOpened.end(), carrier) == doorsOpened.end())
				doorsOpened.push_back(carrier);
			if (lookup.Get<ExitIntent>(passenger) != nullptr)
				commands.Remove<ExitIntent>(passenger);
			// TransportContain::onRemoving: GoAggressiveOnExit makes it aggressive.
			if (const Aggression *aggression = lookup.Get<Aggression>(passenger); aggression != nullptr && transport->definition.goAggressiveOnExit)
			{
				Aggression aggressive = *aggression;
				aggressive.attitude = attitude::Aggressive;
				commands.Set<Aggression>(passenger, aggressive);
			}
			const bool aloft = from->position.z - ground.Surface(from->position.XY()) > Engine::Math::Fixed::FromInt(3);
			if ((transport->definition.unloadInAir && aloft) || placement != nullptr)
			{
				// Dropped in the air: straight below the carrier (a delivery's: moved by its drop); with a body of its own
				// (not held by a locomotor: a parachute) it falls on it, else it sinks steadily.
				Transform placed = *from;
				if (placement != nullptr)
				{
					placed.position.x += placement->offset.x;
					placed.position.y += placement->offset.y;
					placed.position.z += placement->offset.z;
					if (placement->moves && lookup.Get<MoveOrder>(passenger) != nullptr)
						commands.Set<MoveOrder>(passenger, Replanned(MoveToPoint(placement->moveTo)));
					// DeliverPayloadAIUpdate: a smart bomb is told its spot (SmartBombTargetHomingUpdate::SetTargetPosition).
					if (const DropHoming *homing = lookup.Get<DropHoming>(passenger))
					{
						DropHoming told = *homing;
						told.target = placement->moveTo;
						told.received = 1;
						commands.Set<DropHoming>(passenger, told);
					}
				}
				const PhysicsBody *body = lookup.Get<PhysicsBody>(passenger);
				std::optional<PhysicsBody> pushed;
				if (body != nullptr)
					pushed = *body;
				// DeliveringState::update: InheritTransportVelocity, the carrier's velocity applied to it as a force.
				const bool inherited = placement != nullptr && placement->inherit && pushed.has_value();
				if (inherited)
					ApplyForce(*pushed, placement->velocity);
				const bool kept = removing(*transport, *from, passenger, placed, pushed, placement != nullptr ? placement->velocity : carrierVelocity(carrier));
				commands.Set<Transform>(passenger, placed);
				if (inherited || kept)
					commands.Set<PhysicsBody>(passenger, *pushed);
				if (body == nullptr || body->Has(physics_flag::Locomotive))
					commands.Add<Descent>(passenger, Descent{drop.fallRate});
				return;
			}
			// GarrisonContain::exitObjectViaDoor with its evacuation to one side (EVAC_TO_LEFT / EVAC_TO_RIGHT): out at a random
			// spot beside it (along its length within a quarter of its half length, out from half to twice its half width),
			// walking on to a random spot ten half widths out on that side; the building's frame turns them.
			if (const Garrison *garrison = lookup.Get<Garrison>(carrier); garrison != nullptr && (garrison->evac == 1 || garrison->evac == 2))
			{
				using Engine::Math::Fixed;
				auto random = Engine::Math::Stream(seed, {context.Tick(), passenger.index, passenger.generation, 0xE7ACu});
				const Fixed sign = garrison->evac == 1 ? Fixed::One() : Fixed{} - Fixed::One();
				const Fixed length = garrison->halfLength, width = garrison->halfWidth;
				const Fixed doorX = Engine::Math::UniformFixed(random, Fixed{} - length / Fixed::FromInt(4), length / Fixed::FromInt(4));
				const Fixed doorY = Engine::Math::UniformFixed(random, width / Fixed::FromInt(2), width * Fixed::FromInt(2)) * sign;
				const Fixed walkX = Engine::Math::UniformFixed(random, Fixed{} - length, length);
				const Fixed walkY = width * Fixed::FromInt(10) * sign;
				const Fixed c = Engine::Math::Cos(from->facing), s = Engine::Math::Sin(from->facing);
				const auto place = [&](Fixed x, Fixed y) { return Engine::Math::FixedVector2{from->position.x + x * c - y * s, from->position.y + x * s + y * c}; };
				const auto start = place(doorX, doorY);
				commands.Set<Transform>(passenger, Transform{{start.x, start.y, from->position.z}, from->facing});
				if (lookup.Get<MoveOrder>(passenger) != nullptr)
					commands.Set<MoveOrder>(passenger, Replanned(MoveToPoint(place(walkX, walkY))));
				return;
			}
			// GarrisonContain::exitObjectViaDoor bursting from its centre: from its position (on the ground when it encloses
			// its riders; else where the rider stood), facing its way, walking to a free spot there (adjustToPossibleDestination).
			// (The cliff bunkers' fallback to its front or back when that ground is no good for the rider is not ported.)
			if (lookup.Get<Garrison>(carrier) != nullptr)
			{
				const Transform *stood = lookup.Get<Transform>(passenger);
				Transform out = stood != nullptr ? *stood : *from;
				if (transport->definition.enclosesRiders != 0)
					out.position = {from->position.x, from->position.y, ground.At(from->position.XY())};
				else
					out.position.z = ground.At(out.position.XY()); // onRemoving: from its station down to the ground there
				out.facing = from->facing;
				commands.Set<Transform>(passenger, out);
				if (lookup.Get<MoveOrder>(passenger) != nullptr)
					commands.Set<MoveOrder>(passenger, Replanned(MoveToPoint(out.position.XY())));
				return;
			}
			// OpenContain::exitObjectViaDoor: where it rode (onRemoving's ExitBone first); with NumberOfExitPaths, set at the
			// path's ExitStart facing the carrier's way, walking to its ExitEnd and ignoring collisions a second
			// (setIgnoreCollisionTime); the next one out takes the next path. Without: left where it is.
			// (TunnelContain::onRemoving: out of a tunnel network, at the tunnel it leaves by.)
			const Transform *rode = lookup.Get<Transform>(passenger);
			Transform placed = rode != nullptr && !network ? *rode : *from;
			std::optional<PhysicsBody> pushed;
			if (const PhysicsBody *body = lookup.Get<PhysicsBody>(passenger))
				pushed = *body;
			if (removing(*transport, *from, passenger, placed, pushed, carrierVelocity(carrier)))
				commands.Set<PhysicsBody>(passenger, *pushed);
			const TransportDefinition &definition = transport->definition;
			if (definition.exitPaths > 0)
			{
				auto taken = std::find_if(exitPathsTaken.begin(), exitPathsTaken.end(), [&](const auto &entry) { return entry.first == carrier; });
				if (taken == exitPathsTaken.end())
				{
					exitPathsTaken.emplace_back(carrier, transport->nextExitPath);
					taken = exitPathsTaken.end() - 1;
				}
				const std::uint32_t path = std::min(taken->second % definition.exitPaths, TransportDefinition::MaxExitPaths - 1);
				taken->second = (taken->second + 1) % definition.exitPaths;
				const Engine::Math::Fixed c = Engine::Math::Cos(from->facing), s = Engine::Math::Sin(from->facing);
				const auto inWorld = [&](const Engine::Math::FixedVector3 &bone) {
					return from->position + Engine::Math::FixedVector3{bone.x * c - bone.y * s, bone.x * s + bone.y * c, bone.z};
				};
				placed.position = inWorld(definition.exitStarts[path]);
				placed.facing = from->facing;
				if (lookup.Get<MoveOrder>(passenger) != nullptr)
					commands.Set<MoveOrder>(passenger, Replanned(MoveToPoint(inWorld(definition.exitEnds[path]).XY())));
				if (const BlockedState *blocked = lookup.Get<BlockedState>(passenger))
				{
					BlockedState ignoring = *blocked;
					ignoring.ignoreUntil = tick + 30; // LOGICFRAMES_PER_SECOND
					commands.Set<BlockedState>(passenger, ignoring);
				}
			}
			commands.Set<Transform>(passenger, placed);
		};
		exits.ForEach([&](const ExitRequest &request) { exit(request.transport, manifest.TakeNext(request.transport)); });
		riderExits.ForEach([&](const RiderExit &request) { exit(request.transport, manifest.Take(request.transport, request.rider)); });
		context.Read<IntentExits>().ForEach([&](const RiderExit &request) { exit(request.transport, manifest.Take(request.transport, request.rider)); });
		context.Read<DropExits>().ForEach([&](const DropExit &dropped) { exit(dropped.transport, manifest.TakeNext(dropped.transport), &dropped); });
		context.Read<PlacedExits>().ForEach([&](const PlacedExit &placed) {
			const ecs::Entity rider = manifest.Take(placed.transport, placed.rider);
			if (!lookup.IsAlive(rider))
				return;
			exit(placed.transport, rider);
			// Not stepping clear of it on foot nor sinking: where it is put, its own AI to say what next.
			commands.Set<Transform>(rider, Transform{placed.at, placed.facing});
			if (lookup.Get<MoveOrder>(rider) != nullptr)
				commands.Set<MoveOrder>(rider, MoveOrder{});
			commands.Remove<Descent>(rider);
			if (const PhysicsBody *body = lookup.Get<PhysicsBody>(rider))
			{
				PhysicsBody free = *body;
				free.velocity = {};
				free.acceleration = {};
				free.Set(physics_flag::AllowToFall, true);
				free.Set(physics_flag::PhysicsDriven, true);
				commands.Set<PhysicsBody>(rider, free);
			}
		});

		for (const auto &[entity, network] : networked)
			for (const auto &[id, count] : heads)
				if (id == network)
					occupied.emplace_back(entity, count);
		for (const auto &[entity, player] : entered)
			if (std::find_if(occupied.begin(), occupied.end(), [&](const auto &entry) { return entry.first == entity; }) == occupied.end())
				if (const Transport *transport = lookup.Get<Transport>(entity))
					occupied.emplace_back(entity, transport->occupied);
		for (const auto &[entity, used] : occupied)
		{
			const auto in = std::find_if(entered.begin(), entered.end(), [&](const auto &entry) { return entry.first == entity; });
			const Transport *transport = lookup.Get<Transport>(entity);
			const bool opened = std::find(doorsOpened.begin(), doorsOpened.end(), entity) != doorsOpened.end();
			if (transport != nullptr && (transport->occupied != used || in != entered.end() || opened))
			{
				Transport updated = *transport;
				updated.occupied = used;
				if (in != entered.end())
				{
					updated.enteredBy = in->second;
					updated.enteredTick = tick;
				}
				if (opened)
					updated.doorOpenedTick = tick;
				for (const auto &[carrier, next] : exitPathsTaken)
					if (carrier == entity)
						updated.nextExitPath = next;
				commands.Set<Transport>(entity, updated);
			}
		}
		// A door opened with nobody's count changing (the carrier not among those touched above).
		for (const ecs::Entity carrier : doorsOpened)
			if (std::find_if(occupied.begin(), occupied.end(), [&](const auto &entry) { return entry.first == carrier; }) == occupied.end())
				if (const Transport *transport = lookup.Get<Transport>(carrier))
				{
					Transport updated = *transport;
					updated.doorOpenedTick = tick;
					for (const auto &[taken, next] : exitPathsTaken)
						if (taken == carrier)
							updated.nextExitPath = next;
					commands.Set<Transport>(carrier, updated);
				}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::CargoTransferSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.cargo_transfer";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
