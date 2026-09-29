export module engine.gameplay.rts.containment.systems.cargo_transfer_system;
import std;

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
		ecs::Read<AttackTarget>, ecs::Read<Targetable>, ecs::Read<PhysicsBody>, ecs::Read<Aggression>, ecs::Read<Owner>, ecs::Read<ExitIntent>>;
	using Resources = ecs::Resources<ecs::Read<DropSettings>, ecs::Read<BoardRequests>, ecs::Read<ExitRequests>, ecs::Read<RiderExits>, ecs::Read<DropExits>,
		ecs::Read<IntentExits>, ecs::Write<CargoManifest>, ecs::Read<GroundHeight>>;

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
			if (used + slots > transport->definition.slots)
				return;
			used += slots;
			commands.Add<Passenger>(request.passenger, Passenger{request.transport, slots, 0, tick});
			// Inside, it may fire only where its carrier allows it (isPassengerAllowedToFire).
			const bool infantry = kind != nullptr && (kind->classes & target_class::Infantry) != 0;
			const bool armed = transport->definition.passengersFire && (!transport->definition.infantryOnly || infantry);
			commands.Add<OffMap>(request.passenger, OffMap{1, armed, {}, request.transport});
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

		std::uint32_t door = 0;
		std::vector<ecs::Entity> doorsOpened; // carriers someone got out of this tick (exitObjectViaDoor)
		// The next passenger out of an unloading transport, then those let out by name (healed).
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
						commands.Set<MoveOrder>(passenger, MoveToPoint(placement->moveTo));
				}
				commands.Set<Transform>(passenger, placed);
				const PhysicsBody *body = lookup.Get<PhysicsBody>(passenger);
				if (body == nullptr || body->Has(physics_flag::Locomotive))
					commands.Add<Descent>(passenger, Descent{drop.fallRate});
				return;
			}
			// Out beside the transport, stepping clear of it.
			const auto side = Engine::Math::Direction(from->facing + Engine::Math::TurnAngle{0x40000000u * (++door % 4)});
			const auto out = from->position.XY() + side * Engine::Math::Fixed::FromInt(15);
			commands.Set<Transform>(passenger, Transform{{out.x, out.y, ground.At(out)}, from->facing});
			if (lookup.Get<MoveOrder>(passenger) != nullptr)
				commands.Set<MoveOrder>(passenger, MoveToPoint(out + side * Engine::Math::Fixed::FromInt(20)));
		};
		exits.ForEach([&](const ExitRequest &request) { exit(request.transport, manifest.TakeNext(request.transport)); });
		riderExits.ForEach([&](const RiderExit &request) { exit(request.transport, manifest.Take(request.transport, request.rider)); });
		context.Read<IntentExits>().ForEach([&](const RiderExit &request) { exit(request.transport, manifest.Take(request.transport, request.rider)); });
		context.Read<DropExits>().ForEach([&](const DropExit &dropped) { exit(dropped.transport, manifest.TakeNext(dropped.transport), &dropped); });

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
