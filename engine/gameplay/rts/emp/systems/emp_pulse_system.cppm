export module engine.gameplay.rts.emp.systems.emp_pulse_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.emp.components.emp_pulse;
export import engine.gameplay.common.status.components.disabled_until;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.lifecycle.resources.kill_requests;
export import engine.gameplay.rts.emp.resources.emp_strikes;

// EMPUpdate::update and doDisableAttack (GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/EMPUpdate.cpp), see
// EmpPulse: on its fade tick it asks for the disables (DisableRequests) and kills (KillRequests) its pulse deals, in
// the spatial index's order, and lists the victims its sphere disabled (EmpStrikes: their sparks); on its die tick it asks
// for its own death.
export namespace engine::gameplay
{
struct EmpPulseSystem
{
	using Query = ecs::Query<ecs::Optional<TeamMember>, ecs::Write<EmpPulse>, ecs::Read<Transform>, ecs::Read<Owner>>;
	using Lookup = ecs::Lookup<ecs::Read<AttackTarget>, ecs::Read<EmpTraits>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<Relationships>, ecs::Write<DisableRequests>, ecs::Write<KillRequests>,
		ecs::Write<EmpStrikes>>;

	static constexpr std::uint32_t Airborne = target_class::AirborneVehicle | target_class::AirborneInfantry;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const Relationships &relationships = context.Read<Relationships>();
		DisableRequests &disables = context.Write<DisableRequests>();
		KillRequests &kills = context.Write<KillRequests>();
		auto &strikes = context.Write<EmpStrikes>().list;
		strikes.clear();
		auto &blackened = context.Write<EmpStrikes>().blackened;
		blackened.clear();
		const auto lookup = context.Lookup<Lookup>();
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto pulses = chunk.template Get<EmpPulse>();
			const auto transforms = chunk.template Get<Transform>();
			const auto owners = chunk.template Get<Owner>();
			const auto teamRows = chunk.template Get<TeamMember>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < pulses.size(); ++row)
			{
				EmpPulse &pulse = pulses[row];
				if (pulse.done == 0 && tick >= pulse.fadeTick)
				{
					pulse.done = 1;
					const auto at = transforms[row].position;
					const std::uint32_t player = owners[row].player;
					const std::uint32_t team = teamRows.empty() ? Relationships::NoTeam : teamRows[row].team;
					const std::uint64_t until = tick + pulse.duration;
					// Its producer's victim: an airborne one limits it to the airborne.
					ecs::Entity intended{};
					const SpatialEntry *intendedEntry = nullptr;
					if (const AttackTarget *aim = lookup.IsAlive(pulse.producer) ? lookup.Get<AttackTarget>(pulse.producer) : nullptr)
						if (aim->target.IsValid())
						{
							intended = aim->target;
							intendedEntry = spatial.Find(intended);
						}
					const bool onlyAirborne = intendedEntry != nullptr && (intendedEntry->classes & Airborne) != 0;
					bool intendedDone = false;
					spatial.ForEachWithin(at.XY(), pulse.radius, [&](const SpatialEntry &entry) {
						if (entry.entity == entities[row])
							return;
						// FROM_BOUNDINGSPHERE_3D.
						const Engine::Math::Fixed reach = pulse.radius + entry.radius;
						if (Engine::Math::DistanceSquared(entry.position, at) > reach * reach)
							return;
						const bool airborne = (entry.classes & Airborne) != 0;
						if (onlyAirborne && !airborne)
							return;
						const EmpTraits *traits = lookup.Get<EmpTraits>(entry.entity);
						const std::uint32_t flags = traits != nullptr ? traits->flags : 0u;
						const bool structure = (entry.classes & target_class::Structure) != 0;
						if (pulse.sparesOwnBuildings != 0 && structure && entry.player == player)
							return;
						if ((entry.classes & (target_class::Vehicle | target_class::Structure)) == 0 && (flags & emp_trait::SpawnsAreWeapons) == 0)
							return;
						if ((entry.classes & target_class::Aircraft) != 0 && airborne)
						{
							if ((flags & emp_trait::Hardened) == 0)
							{
								kills.entities.push_back(entry.entity);
								blackened.push_back(entry.entity);
							}
							return;
						}
						if (structure)
						{
							if ((flags & emp_trait::FactionStructure) == 0)
								return;
						}
						else if (pulse.sparesAllies != 0 && relationships.Allies(entry.team, entry.player, team, player))
							return;
						disables.list.push_back({entry.entity, disabled_type::Emp, 0, until});
						strikes.push_back({entry.entity, entities[row], pulse.duration});
						intendedDone = intendedDone || entry.entity == intended;
					});
					// The intended victim missed by the sphere: an aircraft, not hardened, near enough (the original's
					// lengthSqr <= radius * 2 or within 40).
					if (intendedEntry != nullptr && !intendedDone && (intendedEntry->classes & target_class::Aircraft) != 0)
					{
						const EmpTraits *traits = lookup.Get<EmpTraits>(intended);
						const Engine::Math::Fixed distance = Engine::Math::DistanceSquared(intendedEntry->position, at);
						if ((traits == nullptr || (traits->flags & emp_trait::Hardened) == 0) &&
							(distance <= pulse.radius * Engine::Math::Fixed::FromInt(2) || distance <= Engine::Math::Fixed::FromInt(1600)))
							disables.list.push_back({intended, disabled_type::Emp, 0, until});
					}
				}
				if (tick >= pulse.dieTick)
					kills.entities.push_back(entities[row]);
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::EmpPulseSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.emp_pulses";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
