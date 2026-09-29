export module engine.gameplay.rts.containment.systems.passenger_ride_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.common.spatial.components.transform;

// After the step: passengers ride where their transport is (the original's
// OpenContain::redeployOccupants on the container's transform change, with no
// fire points in the art: at the container's position, raised by its rider height: a Helix's 8). In a container with
// fire points (a garrison's FIREPOINT bones), one with a victim stands at the
// free point of the container's damage state's set nearest it in 3D
// (GarrisonContain::findClosestFreeGarrisonPointIndex over all 40 places,
// those past the bones in its middle; in entity order), so it shoots from
// there. In a transport with fire points (its FIREPOINT bones: OpenContain::redeployOccupants), its riders stand at
// them in turn from the first in (wrapping round), turned with the transport (and with its turret when they ride in it).
// Passengers keep their own facing
// (a passenger allowed to fire turns to its victim itself).
export import engine.gameplay.rts.containment.components.garrison_points;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.combat.components.turret;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export namespace engine::gameplay
{
struct PassengerRideSystem
{
	using Query = ecs::Query<ecs::Read<Passenger>>;
	using Lookup = ecs::Lookup<ecs::Read<Transform>, ecs::Read<GarrisonPoints>, ecs::Read<AttackTarget>, ecs::Read<Transport>, ecs::Read<TransportFirePoints>,
		ecs::Read<Turret>>;
	using Resources = ecs::Resources<ecs::Read<CargoManifest>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		const CargoManifest &manifest = context.Read<CargoManifest>();
		auto &commands = context.Commands();
		// Fire points taken this pass, by container.
		std::vector<std::pair<ecs::Entity, std::uint64_t>> taken;
		const auto takenBy = [&](ecs::Entity carrier) -> std::uint64_t & {
			for (auto &[entity, mask] : taken)
				if (entity == carrier)
					return mask;
			return taken.emplace_back(carrier, 0u).second;
		};
		query.ForEachChunk([&](auto chunk) {
			const auto passengers = chunk.Get<Passenger>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < passengers.size(); ++row)
			{
				const Transform *carrier = lookup.Get<Transform>(passengers[row].transport);
				const Transform *self = lookup.Get<Transform>(entities[row]);
				if (carrier == nullptr || self == nullptr)
					continue;
				Engine::Math::FixedVector3 at = carrier->position;
				if (const Transport *transport = lookup.Get<Transport>(passengers[row].transport))
					at.z += transport->definition.riderHeight;
				// putObjAtNextFirePoint: its turn among the riders (the first in takes the first point).
				if (const TransportFirePoints *fire = lookup.Get<TransportFirePoints>(passengers[row].transport); fire != nullptr && fire->count > 0)
				{
					const auto aboard = manifest.Aboard(passengers[row].transport);
					const auto found = std::find(aboard.begin(), aboard.end(), entities[row]);
					const std::size_t turn = static_cast<std::size_t>(found - aboard.begin()) % fire->count;
					Engine::Math::FixedVector3 local = fire->points[turn];
					if (fire->inTurret != 0)
						if (const Turret *turret = lookup.Get<Turret>(passengers[row].transport))
						{
							const Engine::Math::Fixed tc = Engine::Math::Cos(turret->angle), ts = Engine::Math::Sin(turret->angle);
							const Engine::Math::Fixed dx = local.x - fire->turretPivot.x, dy = local.y - fire->turretPivot.y;
							local.x = fire->turretPivot.x + dx * tc - dy * ts;
							local.y = fire->turretPivot.y + dx * ts + dy * tc;
						}
					const Engine::Math::Fixed c = Engine::Math::Cos(carrier->facing), s = Engine::Math::Sin(carrier->facing);
					at = {carrier->position.x + local.x * c - local.y * s, carrier->position.y + local.x * s + local.y * c, carrier->position.z + local.z};
				}
				const GarrisonPoints *points = lookup.Get<GarrisonPoints>(passengers[row].transport);
				const AttackTarget *attack = lookup.Get<AttackTarget>(entities[row]);
				const Transform *victim = attack != nullptr && attack->target.IsValid() ? lookup.Get<Transform>(attack->target) : nullptr;
				if (points != nullptr && victim != nullptr)
				{
					std::uint64_t &used = takenBy(passengers[row].transport);
					const Engine::Math::Fixed c = Engine::Math::Cos(carrier->facing), s = Engine::Math::Sin(carrier->facing);
					std::optional<std::size_t> best;
					Engine::Math::Fixed nearest;
					Engine::Math::FixedVector3 bestAt;
					const std::uint32_t set = std::min(points->condition, garrison_condition::Count - 1);
					for (std::size_t index = 0; index < GarrisonPoints::Max; ++index)
					{
						if ((used >> index) & 1u)
							continue;
						const Engine::Math::FixedVector3 local = index < points->counts[set] ? points->points[set][index] : Engine::Math::FixedVector3{};
						const Engine::Math::FixedVector3 point{carrier->position.x + local.x * c - local.y * s, carrier->position.y + local.x * s + local.y * c,
							carrier->position.z + local.z};
						const Engine::Math::FixedVector3 delta = point - victim->position;
						const Engine::Math::Fixed distance = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
						if (!best || distance < nearest)
						{
							best = index;
							nearest = distance;
							bestAt = point;
						}
					}
					if (best)
					{
						used |= std::uint64_t{1} << *best;
						at = bestAt;
					}
				}
				if (at == self->position)
					continue;
				commands.Set<Transform>(entities[row], Transform{at, self->facing});
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::PassengerRideSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.passenger_ride";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
