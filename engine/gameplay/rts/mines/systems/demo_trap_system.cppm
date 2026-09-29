export module engine.gameplay.rts.mines.systems.demo_trap_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.mines.components.demo_trap;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.rts.combat.resources.shots;
export import engine.gameplay.rts.lifecycle.resources.kill_requests;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.construction.components.sale;

// DemoTrapUpdate::update and detonate (GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/DemoTrapUpdate.cpp),
// see DemoTrap: nothing while under construction or being sold; killed, it goes off if it says so; in its detonation
// slot it goes off; otherwise it counts down to its next scan, and in proximity mode scans (itself left out: the
// original's scan finds the trap too, which only a trap refusing friendly detonation would notice). Going off: its
// weapon where it is (a queued shot) and a kill request.
export namespace engine::gameplay
{
struct DemoTrapSystem
{
	using Query = ecs::Query<ecs::Optional<TeamMember>, ecs::Write<DemoTrap>, ecs::Read<Transform>, ecs::Read<Owner>, ecs::Read<Health>, ecs::OptionalWrite<WeaponSlots>,
		ecs::Optional<UnderConstruction>, ecs::Optional<Sale>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<Relationships>, ecs::Write<KillRequests>, ecs::Write<ShotQueue>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const Relationships &relationships = context.Read<Relationships>();
		KillRequests &kills = context.Write<KillRequests>();
		ShotQueue &shots = context.Write<ShotQueue>();
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto traps = chunk.template Get<DemoTrap>();
			const auto transforms = chunk.template Get<Transform>();
			const auto owners = chunk.template Get<Owner>();
			const auto teamRows = chunk.template Get<TeamMember>();
			const auto healths = chunk.template Get<Health>();
			auto slots = chunk.template Get<WeaponSlots>();
			const auto building = chunk.template Get<UnderConstruction>();
			const auto selling = chunk.template Get<Sale>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < traps.size(); ++row)
			{
				DemoTrap &trap = traps[row];
				if (trap.detonated != 0 || !building.empty() || !selling.empty())
					continue;
				const auto at = transforms[row].position;
				const std::uint32_t player = owners[row].player;
				const std::uint32_t team = teamRows.empty() ? Relationships::NoTeam : teamRows[row].team;
				const auto detonate = [&] {
					if (trap.weapon != DemoTrap::NoWeapon)
						shots.Add(Shot{entities[row], {}, trap.weapon, player, at, at, tick, tick});
					kills.entities.push_back(entities[row]);
					trap.detonated = 1;
				};
				if (IsDead(healths[row]))
				{
					if (trap.detonateWhenKilled != 0)
						detonate();
					continue;
				}
				if (!slots.empty() && slots[row].locked == WeaponSlots::Unlocked)
					slots[row].locked = trap.defaultSlot;
				const std::uint8_t slot = slots.empty() ? 0 : slots[row].locked != WeaponSlots::Unlocked ? slots[row].locked : slots[row].current;
				if (slot == trap.detonationSlot)
				{
					detonate();
					continue;
				}
				if (trap.countdown > 0)
				{
					--trap.countdown;
					continue;
				}
				if (slot == trap.manualSlot)
					continue;
				trap.countdown = trap.scanTicks;
				bool shallDetonate = false, stopped = false;
				spatial.ForEachWithin(at.XY(), trap.range, [&](const SpatialEntry &entry) {
					if (stopped || entry.entity == entities[row] || (entry.classes & trap.ignoreClasses) != 0)
						return;
					if (!relationships.Enemies(team, player, entry.team, entry.player))
					{
						if (trap.friendlyDetonation == 0)
						{
							shallDetonate = false;
							stopped = true;
						}
						return;
					}
					if ((entry.classes & (target_class::AirborneVehicle | target_class::AirborneInfantry)) != 0)
						return;
					if (Engine::Math::DistanceSquared(entry.position.XY(), at.XY()) <= trap.range * trap.range)
					{
						shallDetonate = true;
						if (trap.friendlyDetonation != 0)
							stopped = true;
					}
				});
				if (shallDetonate)
					detonate();
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::DemoTrapSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.demo_traps";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
