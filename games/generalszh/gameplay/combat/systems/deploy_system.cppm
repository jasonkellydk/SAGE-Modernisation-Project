export module games.generalszh.gameplay.combat.systems.deploy_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.algorithms.deploy_states;
export import engine.gameplay.rts.combat.systems.targeting_system;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.rts.combat.algorithms.attack_goal;
export import games.generalszh.gameplay.ai.components.guard;

// DeployStyleAIUpdate::update, each tick after targeting has set its moves and before movement, chunk-parallel:
//   an unpacking or packing done by now is deployed, or ready to move;
//   not moving and in its current weapon's range of its target (or idle on guard: deployed for the quickest
//   answer), it unpacks (reversing a packing under way), and turrets swinging back to rest stop and fire;
//   moving, a deployed unit sends its turret back to rest first (TurretsMustCenterBeforePacking) and then packs; one
//   unpacking reverses it;
//   while unpacking, packing or aligning it is busy: held where it is, its move kept for after.
//   Retail quirk fixed (the SuperHackers' @bugfix, the non-retail branch): a unit in range while moving does not
//   deploy (the pathfinder's stricter range left retail units deploying and packing on the move).
export namespace generalszh::gameplay
{
struct DeploySystem
{
	using Query = ecs::Query<ecs::Write<engine::gameplay::Deploy>, ecs::Write<engine::gameplay::MoveOrder>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::Armament>, ecs::Read<engine::gameplay::AttackTarget>, ecs::OptionalWrite<engine::gameplay::Turret>,
		ecs::OptionalWrite<engine::gameplay::AltTurret>, ecs::Optional<engine::gameplay::WeaponSlots>, ecs::Optional<engine::gameplay::Targetable>,
		ecs::Optional<engine::gameplay::WeaponBonusConditions>, ecs::Optional<Guard>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::WeaponCatalog>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const gp::SpatialIndex &spatial = context.Read<gp::SpatialIndex>();
		const gp::WeaponCatalog &weapons = context.Read<gp::WeaponCatalog>();
		const std::uint64_t now = context.Tick();
		auto deploys = chunk.Get<gp::Deploy>();
		auto orders = chunk.Get<gp::MoveOrder>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto armaments = chunk.Get<gp::Armament>();
		const auto targets = chunk.Get<gp::AttackTarget>();
		auto turrets = chunk.Get<gp::Turret>();
		auto alts = chunk.Get<gp::AltTurret>();
		const auto slotSets = chunk.Get<gp::WeaponSlots>();
		const auto bodies = chunk.Get<gp::Targetable>();
		const auto bonuses = chunk.Get<gp::WeaponBonusConditions>();
		const auto guards = chunk.Get<Guard>();
		for (std::size_t row = 0; row < deploys.size(); ++row)
		{
			gp::Deploy &deploy = deploys[row];
			gp::MoveOrder &order = orders[row];
			const gp::Armament &armament = armaments[row];
			// getWhichTurretForCurWeapon.
			gp::Turret *turret = nullptr;
			if (!slotSets.empty())
			{
				const gp::SlotAim aim = slotSets[row].slots[slotSets[row].current].aim;
				turret = aim == gp::SlotAim::Turret && !turrets.empty() ? &turrets[row] : aim == gp::SlotAim::AltTurret && !alts.empty() ? &alts[row].turret : nullptr;
			}
			else if (!turrets.empty())
				turret = &turrets[row];
			const bool moving = order.mode != gp::MoveMode::Idle;
			bool inRange = false;
			if (armament.weapon != gp::WeaponCatalog::None)
			{
				gp::SpatialEntry point;
				if (const gp::SpatialEntry *goal = gp::AttackGoal(spatial, targets[row], point))
				{
					const gp::WeaponDefinition &weapon = weapons.At(armament.weapon);
					const Engine::Math::Fixed range = gp::BonusAttackRange(weapon.attackRange, weapons.Bonus(weapon, bonuses.empty() ? 0u : bonuses[row].Effective()));
					const Engine::Math::Fixed radius = bodies.empty() ? Engine::Math::Fixed{} : bodies[row].radius;
					const auto from = transforms[row].position.XY();
					inRange = gp::WithinAttackRange(range, from, radius, *goal) && !gp::TooCloseToAttack(weapon.minimumRange, from, radius, *goal);
				}
			}
			const bool guardIdle = !guards.empty() && guards[row].state == GuardState::Idle;
			if (deploy.waitTick != 0 && now >= deploy.waitTick)
			{
				if (deploy.state == gp::DeployState::Deploy)
					gp::SetDeployState(deploy, turret, gp::DeployState::ReadyToAttack, now);
				else if (deploy.state == gp::DeployState::Undeploy)
					gp::SetDeployState(deploy, turret, gp::DeployState::ReadyToMove, now);
			}
			if (!moving && (inRange || guardIdle))
			{
				switch (deploy.state)
				{
				case gp::DeployState::ReadyToMove:
					gp::SetDeployState(deploy, turret, gp::DeployState::Deploy, now);
					break;
				case gp::DeployState::Undeploy:
					if (deploy.waitTick != 0)
						gp::SetDeployState(deploy, turret, gp::DeployState::Deploy, now, true);
					break;
				case gp::DeployState::AligningTurrets:
					gp::SetDeployState(deploy, turret, gp::DeployState::ReadyToAttack, now);
					break;
				default:
					break;
				}
			}
			else if (moving)
			{
				switch (deploy.state)
				{
				case gp::DeployState::ReadyToAttack:
					gp::SetDeployState(deploy, turret, turret != nullptr && deploy.turretsMustCenter ? gp::DeployState::AligningTurrets : gp::DeployState::Undeploy, now);
					break;
				case gp::DeployState::Deploy:
					if (deploy.waitTick != 0)
						gp::SetDeployState(deploy, turret, gp::DeployState::Undeploy, now, true);
					break;
				case gp::DeployState::AligningTurrets:
					if (turret != nullptr && gp::TurretAtRest(*turret))
						gp::SetDeployState(deploy, turret, gp::DeployState::Undeploy, now);
					break;
				default:
					break;
				}
			}
			const bool busy = deploy.state == gp::DeployState::Deploy || deploy.state == gp::DeployState::Undeploy || deploy.state == gp::DeployState::AligningTurrets;
			order.held = busy ? 1u : 0u;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::DeploySystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.deploy";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::TargetingSystem>;
};
}
