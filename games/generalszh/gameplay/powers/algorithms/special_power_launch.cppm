export module games.generalszh.gameplay.powers.algorithms.special_power_launch;
import engine.gameplay.rts.navigation.resources.navigation_grid;
import engine.gameplay.common.spatial.resources.spatial_index;
import engine.gameplay.common.spatial.algorithms.find_position;
import engine.gameplay.common.random.resources.random_seed;
import games.generalszh.gameplay.effects.algorithms.radius_decals;
import std;
import games.generalszh.gameplay.battleplans.algorithms.battle_plan_bonuses;
import games.generalszh.gameplay.creation.algorithms.creation_list_runner;
import games.generalszh.gameplay.powers.algorithms.particle_cannons;
import games.generalszh.gameplay.powers.algorithms.spectre_gunships;
import engine.gameplay.rts.vision.components.vision;
import games.generalszh.gameplay.containment.algorithms.parachuting;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.teams.algorithms.defection;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.rts.parachute.components.parachute;
import engine.gameplay.common.status.algorithms.disable_now;
import engine.gameplay.common.physics.algorithms.forces;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.delivery.components.delivery;
import engine.gameplay.rts.movement.components.move_order;
import Engine.Core.Math.FixedRandom;
import engine.config.binding.values;
import games.generalszh.content.objects.model_conditions;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
export import games.generalszh.gameplay.powers.algorithms.power_trigger;
import games.generalszh.gameplay.abilities.algorithms.special_ability_update;
import engine.gameplay.rts.stealth.components.stealth;
import engine.gameplay.common.status.components.status_flags;
import engine.gameplay.common.spatial.components.targetable;
import games.generalszh.content.objects.object_status;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.vision.resources.shroud_map;
import engine.gameplay.common.spatial.components.transform;
import games.generalszh.gameplay.scripts.resources.script_records;
import engine.gameplay.rts.death.components.death_credit;
import engine.gameplay.common.lifetime.components.lifetime;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.sciences.resources.player_sciences;
import games.generalszh.gameplay.powers.resources.cash_notices;
import games.generalszh.gameplay.eva.resources.eva_notices;

// Firing a special power (AIGroup::groupDoSpecialPowerAtLocation / AtObject -> SpecialPowerModule::doSpecialPowerAt... ->
// triggerSpecialPower): the source's module for it must not be paused, nor the source disabled; a player's order also
// needs the science the power requires and the power ready (ActionManager::canDoSpecialPowerAt...: a script's needs
// neither). Triggering tells the scripts, leaves the power's look where it lands and starts the recharge; then an
// OCLSpecialPower's payload runs are flown in from the map edge nearest the source (CREATE_AT_EDGE_NEAR_SOURCE), over
// the target and away, and a CashHackSpecialPower steals from the target's player.
export namespace generalszh::gameplay
{
namespace gameplay = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

namespace detail
{
// SpecialPowerCompletionDie::setCreator (ObjectCreationList's DeliverPayloadNugget): an object with the module takes
// the creator given (the first time).
void SetCreator(GameWorld &game, ecs::Entity entity, ecs::Entity creator)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gameplay::DefinitionRef>(entity) : nullptr;
	if (ref == nullptr || game.world.Has<gameplay::DeathCredit>(entity))
		return;
	const auto &modules = game.templates.DefinitionAt(ref->index).modules;
	if (std::none_of(modules.begin(), modules.end(), [](const content::ModuleEntry &module) { return module.type == "SpecialPowerCompletionDie"; }))
		return;
	game.world.Add<gameplay::DeathCredit>(entity);
	*game.world.Get<gameplay::DeathCredit>(entity) = {creator};
}

// DeliverPayloadAIUpdateModuleData DoorDelay of the transport (milliseconds, rounded up to ticks).
std::uint32_t DoorDelayOf(const content::ObjectDefinition &transport, std::uint32_t ticksPerSecond)
{
	for (const content::ModuleEntry &module : transport.modules)
		if (module.block != nullptr && module.type == "DeliverPayloadAIUpdate")
			if (const auto *door = module.block->Find("DoorDelay"))
			{
				const std::int64_t milliseconds = engine::config::values::ParseFixed(door->Value()).value_or(Fixed{}).Ceil();
				return milliseconds <= 0 ? 0u : static_cast<std::uint32_t>((milliseconds * ticksPerSecond + 999) / 1000);
			}
	return 0;
}

// ObjectCreationList's DeliverPayloadNugget::create from `primary` (where the run comes in) to `secondary` (the target):
// each transport of the formation (the first on the line, then alternately clockwise and counter-clockwise of it, a
// whole number of spacings further out each pair) comes in parallel, heading for the target offset with it, dropping
// on it offset less the convergence (and off by up to the error radius, but the first); pulled back along its heading
// by one and a half its delivery distance, facing along it, at its preferred height (StartAtPreferredHeight), already
// at speed (StartAtMaxSpeed); held back a random while (DelayDeliveryMax: disabled until then, DISABLED_DEFAULT, its
// motive force left to its physics); loaded with its payload (each in its own container, PutInContainer); its run's dive,
// strafing, weapon delivery and visible payload given it (deliverPayload); only the first of the formation lays the
// target decal.
void Deliver(GameWorld &game, const content::DeliveryNugget &run, Engine::Math::FixedVector3 primary, FixedVector2 secondary, std::uint32_t team,
	ecs::Entity creator = {})
{
	auto &world = game.world;
	FixedVector2 ccw{}, cw{};
	if (run.formationSize > 1)
	{
		const FixedVector2 d = Engine::Math::Normalize(primary.XY() - secondary);
		ccw = {d.x - d.y, d.y + d.x}; // turned +90 degrees, plus itself
		cw = {d.x + d.y, d.y - d.x};  // turned -90 degrees, plus itself
	}
	const content::ObjectDefinition *transportKind = game.templates.Content().objects.Find(run.transport);
	if (transportKind == nullptr)
		return;
	const std::uint32_t doorDelay = DoorDelayOf(*transportKind, game.step.TicksPerSecond());
	for (std::uint32_t formation = 0; formation < run.formationSize; ++formation)
	{
		// Int offsetMultiplier = (formationIndex + 1) / 2 * m_formationSpacing (a whole number).
		const Fixed multiplier = Fixed::FromInt((Fixed::FromInt((formation + 1) / 2) * run.formationSpacing).Floor());
		const FixedVector2 offset = (formation % 2 != 0 ? ccw : cw) * multiplier;
		FixedVector2 start = primary.XY() + offset;
		const FixedVector2 moveTo = secondary + offset;
		FixedVector2 target = secondary + offset * (Fixed::One() - run.convergenceFactor);
		if (run.errorRadius > Fixed::One() && formation > 0)
		{
			const Fixed radius = Engine::Math::UniformFixed(game.random, Fixed{}, run.errorRadius);
			const Fixed angle = Engine::Math::UniformFixed(game.random, Fixed{}, Fixed::FromRaw(411775)); // 2 pi
			target = target + Engine::Math::Direction(Engine::Math::TurnFromRadians(angle)) * radius;
		}
		const Engine::Math::TurnAngle orient = Engine::Math::Heading(moveTo - start);
		if (run.deliveryDistance > Fixed{})
			start = start - Engine::Math::Direction(orient) * (run.deliveryDistance * Fixed::FromRatio(3, 2));
		const ecs::Entity carrier = SpawnObject(game, run.transport, start, orient, team, {});
		if (!world.IsAlive(carrier) || !world.Has<gameplay::Locomotion>(carrier))
			continue;
		// setDisabledUntil(DISABLED_DEFAULT, now + GameLogicRandomValue(0, DelayDeliveryMax)).
		const bool delayed = run.delayDeliveryMax > 0;
		if (delayed)
			gameplay::DisableNow(world, carrier, gameplay::disabled_type::Default,
				game.tick + static_cast<std::uint64_t>(Engine::Math::UniformInt(game.random, 0, static_cast<std::int64_t>(run.delayDeliveryMax))));
		world.Get<gameplay::Transform>(carrier)->position = {start.x, start.y, primary.z};
		SetCreator(game, carrier, formation == 0 ? creator : ecs::Entity{});
		if (creator != ecs::Entity{})
			SetProducer(game, carrier, creator); // transport->setProducer(primaryObj)
		auto &motion = *world.Get<gameplay::Locomotion>(carrier);
		if (run.startAtMaxSpeed)
		{
			motion.speed = motion.locomotor.maxSpeed;
			// applyMotiveForce(its facing x its top speed x its mass): what moves it while its AI is held back (a delayed
			// one; otherwise its locomotor has it at that speed already).
			if (auto *body = world.Get<gameplay::PhysicsBody>(carrier); body != nullptr && delayed)
			{
				const FixedVector2 push = Engine::Math::Direction(orient) * (motion.locomotor.maxSpeed * body->mass);
				gameplay::ApplyMotiveForce(*body, {push.x, push.y, Fixed{}}, game.tick);
			}
		}
		if (run.startAtPreferredHeight)
			world.Get<gameplay::Transform>(carrier)->position.z = game.ground.At(start) + motion.locomotor.preferredHeight;
		std::uint32_t slots = 0;
		for (const auto &item : run.payload)
			slots += item.count;
		// The payload goes into the carrier's own contain module (its TransportContain rules: exit bone, orientation,
		// velocity, riders it may not let out); DeliveringState lets one out each DropDelay, in the air.
		gameplay::TransportDefinition held;
		if (const auto *own = world.Get<gameplay::Transport>(carrier))
			held = own->definition;
		else
			world.Add<gameplay::Transport>(carrier);
		held.slots = std::max(held.slots, slots);
		held.exitDelay = run.dropDelay;
		held.unloadInAir = true;
		*world.Get<gameplay::Transport>(carrier) = {.definition = held, .cruiseHeight = motion.locomotor.preferredHeight};
		// deliverPayload: the run's data; the state machine starts in its approach (a move to where it heads).
		gameplay::Delivery delivery;
		delivery.target = target;
		delivery.moveTo = moveTo;
		delivery.dropOffset = run.dropOffset;
		delivery.dropVariance = run.dropVariance;
		delivery.distance = run.deliveryDistance;
		delivery.preOpen = run.preOpenDistance;
		delivery.maxAttempts = run.maxAttempts;
		delivery.doorDelay = doorDelay;
		delivery.dropDelay = static_cast<std::uint32_t>(run.dropDelay);
		delivery.doorOpening = content::ModelConditionBit("DOOR_1_OPENING");
		delivery.doorClosing = content::ModelConditionBit("DOOR_1_CLOSING");
		delivery.parachuteDirectly = run.parachuteDirectly ? 1 : 0;
		delivery.selfDestruct = run.selfDestruct ? 1 : 0;
		delivery.entered = 1;
		// The target's height (m_targetPos.z: the power's spot), which its first move heads for.
		delivery.targetHeight = game.ground.At(secondary);
		delivery.goalHeight = delivery.targetHeight;
		// deliverPayload: no dive without a DiveStartDistance (POSTDIVE).
		delivery.dive = run.diveStartDistance > Fixed{} ? gameplay::DiveState::PreDive : gameplay::DiveState::PostDive;
		delivery.diveStart = run.diveStartDistance;
		delivery.diveEnd = run.diveEndDistance;
		delivery.strafeSlot = run.strafingWeaponSlot >= 0 ? static_cast<std::uint8_t>(run.strafingWeaponSlot) : gameplay::Delivery::NoSlot;
		if (!run.strafeFx.empty())
			delivery.strafeEffect = game.templates.PlayedEffect(engine::gameplay::DeathEffectKind::Effect, run.strafeFx);
		delivery.fireWeapon = run.fireWeapon ? 1 : 0;
		delivery.inheritVelocity = run.inheritTransportVelocity ? 1 : 0;
		delivery.visibleBones = run.visibleNumBones;
		delivery.visiblePerDrop = run.visibleItemsPerInterval;
		delivery.visibleRun = run.visibleRun;
		// Its velocity so far: at its top speed along its heading (StartAtMaxSpeed), else none.
		{
			const auto &placed = world.Get<gameplay::Transform>(carrier)->position;
			const FixedVector2 moving = run.startAtMaxSpeed ? Engine::Math::Direction(orient) * motion.locomotor.maxSpeed : FixedVector2{};
			delivery.lastPosition = {placed.x - moving.x, placed.y - moving.y, placed.z};
		}
		world.Add<gameplay::Delivery>(carrier);
		*world.Get<gameplay::Delivery>(carrier) = delivery;
		// DeliverPayloadAIUpdate::deliverPayload: its DeliveryDecal on its target, until it heads off the map (only the first
		// of a formation's: the others' radius is 0).
		if (formation == 0)
			LayRadiusDecal(game, carrier, run.deliveryDecal, run.deliveryDecalRadius, {target.x, target.y, game.ground.At(target)}, RadiusDecalUntil::HeadsOffMap);
		*world.Get<gameplay::MoveOrder>(carrier) = gameplay::MoveToPoint(moveTo);
		const FixedVector2 heading = Engine::Math::Direction(orient);
		for (const auto &item : run.payload)
			for (std::uint32_t index = 0; index < item.count; ++index)
			{
				ecs::Entity passenger = SpawnObject(game, item.object, start, Engine::Math::Heading(heading), team, {});
				if (!world.IsAlive(passenger))
					continue;
				// payload->setProducer(transport): its blast spares the carrier (Weapon::dealDamageInternal).
				SetProducer(game, passenger, carrier);
				// The first of each payload type of the first transport is the creator's; the rest none.
				SetCreator(game, passenger, formation == 0 && index == 0 ? creator : ecs::Entity{});
				// PutInContainer: each in its own parachute, which rides the carrier (landing on the target, ParachuteDirectly).
				if (!run.putInContainer.empty())
				{
					const ecs::Entity chute = SpawnObject(game, run.putInContainer, start, Engine::Math::Heading(heading), team, {});
					SetProducer(game, chute, carrier); // container->setProducer(transport)
					if (world.IsAlive(chute) && PutInParachute(game, chute, passenger))
					{
						world.Add<gameplay::OffMap>(passenger);
						if (run.parachuteDirectly)
						{
							auto &state = *world.Get<gameplay::Parachute>(chute);
							state.landing = {target.x, target.y, game.ground.At(target)};
							state.Set(gameplay::parachute_flag::Override, true);
						}
						passenger = chute;
					}
				}
				world.Add<gameplay::Passenger>(passenger);
				*world.Get<gameplay::Passenger>(passenger) = {carrier, 1};
				world.Add<gameplay::OffMap>(passenger);
				game.manifest.Board(carrier, passenger);
				++world.Get<gameplay::Transport>(carrier)->occupied;
			}
	}
}
}

// `atLocation`: fired at the spot (doSpecialPowerAtLocation), else with no target (doSpecialPower: a command button
// needing none; most powers then use where their source stands). `options`: the button's command options.
void FireSpecialPower(GameWorld &game, ecs::Entity source, const std::string &power, FixedVector2 target, bool fromScript = false, bool atLocation = true,
	std::uint32_t options = 0)
{
	auto &world = game.world;
	gameplay::SpecialPowerTimer *timer = PowerModuleFor(game, source, power, fromScript);
	if (timer == nullptr || IsDisabled(game, source) || timer->pausedCount > 0)
		return;
	const content::GameContent &content = game.templates.Content();
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(world.Get<gameplay::DefinitionRef>(source)->index);
	// SpectreGunshipDeploymentUpdate::initiateIntentToDoSpecialPower: a gunship called in at the spot, by the first module for
	// the power whose RequiredScience its player has (SpecialPowerModule::initiateIntentToDoSpecialPower,
	// doesSpecialPowerUpdatePassScienceTest).
	for (const SpectreDeploymentConfig &deployment : game.templates.SpectreDeploymentsOf(world.Get<gameplay::DefinitionRef>(source)->index))
	{
		if (deployment.power != timer->power)
			continue;
		if (deployment.requiredScience != 0xFFFFFFFFu &&
			!world.Resource<gameplay::PlayerSciences>().Has(world.Get<gameplay::Owner>(source)->player, deployment.requiredScience))
			continue;
		DeploySpectreGunship(game, source, deployment, target, fromScript);
		return;
	}
	// ParticleUplinkCannonUpdate::initiateIntentToDoSpecialPower: a player's order starts the attack at the spot, the beam
	// driven by the player's clicks; a script's makes it ready at once and sweeps the spot by itself; then
	// markSpecialPowerTriggered (the power's trigger and recharge).
	if (auto *cannon = world.Get<ParticleCannon>(source))
		if (const ParticleCannonConfig *config = game.templates.ParticleCannonOf(world.Get<gameplay::DefinitionRef>(source)->index); config != nullptr && config->power == timer->power)
		{
			const Engine::Math::FixedVector3 spot{target.x, target.y, game.ground.At(target)};
			StartCannonAttack(*cannon, *config, game.tick, spot, !fromScript, std::nullopt, nullptr, 0, world.Resource<ParticleCannonEvents>(), source);
			TriggerSpecialPower(game, source, timer->power, cannon->initialTarget);
			return;
		}
	// BaikonurLaunchPower: not while disabled; triggered; with no target (doSpecialPower) its launch doors open
	// (DOOR_1_OPENING); at a spot its DetonationObject is made there, on its team.
	for (const content::PowerModule &module : content::PowerModulesOf(kind))
		if (module.type == "BaikonurLaunchPower" && module.power == power)
		{
			if (!atLocation)
			{
				TriggerSpecialPower(game, source, timer->power, std::nullopt);
				if (auto *look = world.Get<gameplay::Appearance>(source))
					look->Set(content::ModelConditionBit("DOOR_1_OPENING"), true);
				return;
			}
			const Engine::Math::FixedVector3 spot{target.x, target.y, game.ground.At(target)};
			TriggerSpecialPower(game, source, timer->power, spot);
			if (!module.detonationObject.empty() && content.objects.Find(module.detonationObject) != nullptr)
				SpawnObject(game, module.detonationObject, target, {}, world.Get<gameplay::TeamMember>(source)->team, {});
			return;
		}
	// SpyVisionSpecialPower::doSpecialPower: not while disabled; triggered, then its first SpyVisionUpdate turns on for
	// BaseDuration, plus BonusDurationPerCaptured for each thing it holds (then no more than MaxDuration).
	for (const content::PowerModule &module : content::PowerModulesOf(kind))
		if (module.type == "SpyVisionSpecialPower" && module.power == power)
		{
			TriggerSpecialPower(game, source, timer->power, std::nullopt);
			std::uint64_t duration = module.baseDurationTicks;
			if (world.Has<gameplay::Transport>(source))
			{
				duration += static_cast<std::uint64_t>(game.manifest.Count(source)) * module.bonusDurationTicks;
				duration = std::min(duration, module.maxDurationTicks);
			}
			if (auto *spy = world.Get<SpyVision>(source); spy != nullptr && spy->count > 0)
			{
				spy->modules[0].activateAsked = 1;
				spy->modules[0].activateTicks = duration;
			}
			return;
		}
	// CleanupAreaPower::doSpecialPowerAtLocation (no trigger of its own): its CleanupHazardUpdate cleans round the spot
	// out to MaxMoveDistanceFromLocation, and it goes there (aiMoveToPosition from its AI).
	for (const content::PowerModule &module : content::PowerModulesOf(kind))
		if (module.type == "CleanupAreaPower" && module.power == power)
		{
			if (auto *cleaner = world.Get<CleanupHazard>(source))
			{
				cleaner->moveRange = module.moveRange;
				cleaner->position = target;
				OrderMove(game, source, target, false, false);
			}
			return;
		}
	// FireWeaponPower::doSpecialPowerAtLocation: the power triggered, every weapon reloaded at once, and an attack on the
	// spot (and its turrets), MaxShotsToFire shots, from its own AI.
	for (const content::PowerModule &module : content::PowerModulesOf(kind))
		if (module.type == "FireWeaponPower" && module.power == power)
		{
			const Engine::Math::FixedVector3 spot{target.x, target.y, game.ground.At(target)};
			TriggerSpecialPower(game, source, timer->power, spot);
			ReloadAllAmmo(game, source);
			OrderAttackPosition(game, source, spot, module.maxShotsToFire, false);
			return;
		}
	// SpecialAbility::doSpecialPower / doSpecialPowerAtLocation (not while disabled or paused): its update module starts
	// it, with no target or at the spot; the power fires now unless the update does it (UpdateModuleStartsAttack).
	for (const content::PowerModule &module : content::PowerModulesOf(kind))
		if (module.type == "SpecialAbility" && module.power == power)
		{
			const std::uint32_t index = timer->power;
			// BattlePlanUpdate::initiateIntentToDoSpecialPower: a Strategy Center's plan chosen (its update starts it).
			if (ChooseBattlePlan(game, source, index, options))
			{
				if (!module.updateModuleStartsAttack)
					TriggerSpecialPower(game, source, index, std::nullopt);
				return;
			}
			const std::optional<Engine::Math::FixedVector3> spot =
				atLocation ? std::optional<Engine::Math::FixedVector3>{Engine::Math::FixedVector3{target.x, target.y, game.ground.At(target)}} : std::nullopt;
			InitiateAbility(game, source, index, {}, spot);
			if (!module.updateModuleStartsAttack)
				TriggerSpecialPower(game, source, index, spot);
			return;
		}
	auto ocl = content::FindOclPower(kind, power);
	if (!ocl)
		return;
	// OCLAdjustPositionToPassable: findPositionAround(FPF_CLEAR_CELLS_ONLY, maxRadius 500, a random start angle): not a
	// cliff, clear pathfinding ground, no water over it, nothing within 5; none found: the target as given. Off the map
	// (a scripted setup) it stays, with no random draw.
	if (ocl->adjustToPassable)
	{
		const auto [low, high] = game.ground.Extent();
		if (target.x >= low.x && target.y >= low.y && target.x <= high.x && target.y <= high.y)
		{
			const auto &spatial = world.Resource<gameplay::SpatialIndex>();
			const auto &grid = world.Resource<gameplay::NavigationGrid>();
			const auto clear = [&](FixedVector2 point) {
				const auto cellX = static_cast<std::int32_t>((point.x / Fixed::FromInt(gameplay::PathfindCellSize)).Floor());
				const auto cellY = static_cast<std::int32_t>((point.y / Fixed::FromInt(gameplay::PathfindCellSize)).Floor());
				if (grid.Width() > 0 && (!grid.Contains(cellX, cellY) || grid.Type(cellX, cellY) != gameplay::PathfindCellType::Clear))
					return false;
				Fixed water;
				if (game.ground.Water(point, water) && water > game.ground.At(point))
					return false;
				bool free = true;
				const Fixed reach = Fixed::FromInt(5);
				spatial.ForEachWithin(point, reach, [&](const gameplay::SpatialEntry &entry) {
					const Fixed apart = reach + entry.radius;
					free = free && Engine::Math::DistanceSquared(point, entry.position.XY()) >= apart * apart;
				});
				return free;
			};
			const Engine::Math::TurnAngle start{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 0xFFFFFFFFll))};
			if (const auto spot = gameplay::FindPositionAround(target, Fixed{}, Fixed::FromInt(500), start, clear))
				target = *spot;
		}
	}
	const std::uint32_t player = world.Get<gameplay::Owner>(source)->player;
	// OCLSpecialPower::findOCL: the first UpgradeOCL whose science its player has, else its OCL.
	for (const auto &[science, upgraded] : ocl->upgrades)
		if (const auto known = content.Science(science); known && world.Resource<gameplay::PlayerSciences>().Has(player, *known))
		{
			ocl->creationList = upgraded;
			break;
		}
	auto runs = content.powers.deliveries.find(ocl->creationList);
	if (runs != content.powers.deliveries.end() && runs->second.empty())
		runs = content.powers.deliveries.end(); // no delivery runs in it: its nuggets are made as a list's
	if (runs == content.powers.deliveries.end() && content.creation.find(ocl->creationList) == content.creation.end())
		return;
	TriggerSpecialPower(game, source, timer->power, Engine::Math::FixedVector3{target.x, target.y, game.ground.At(target)});
	// Payloads join the player's default team ("team<Player>"), as the original.
	const std::uint32_t team = DefaultTeamOf(game, source, player);
	// OCLSpecialPower::doSpecialPowerAtLocation: where the runs come in from (CreateLocation).
	const FixedVector2 from = world.Get<gameplay::Transform>(source)->position.XY();
	Engine::Math::FixedVector3 primary{target.x, target.y, game.ground.At(target)};
	switch (ocl->location)
	{
	case content::CreateLocation::AtEdgeNearSource: primary = game.ground.ClosestEdgePoint(from); break;
	case content::CreateLocation::AtEdgeNearTarget: primary = game.ground.ClosestEdgePoint(target); break;
	case content::CreateLocation::AtEdgeFarthestFromTarget:
	{
		// findFarthestEdgePoint: the far corner (by the map's halves), CREATE_ABOVE_LOCATION_HEIGHT (300) up.
		const auto [low, high] = game.ground.Extent();
		const FixedVector2 corner{target.x < (high.x - low.x) / Fixed::FromInt(2) ? high.x : low.x,
			target.y < (high.y - low.y) / Fixed::FromInt(2) ? high.y : low.y};
		primary = {corner.x, corner.y, game.ground.At(corner) + Fixed::FromInt(300)};
		break;
	}
	case content::CreateLocation::AboveLocation: primary.z += Fixed::FromInt(300); break;
	case content::CreateLocation::AtLocation:
	case content::CreateLocation::UseOwnerObject: break;
	}
	if (runs == content.powers.deliveries.end())
	{
		// ObjectCreationList::create(ocl, source, primary, target): its other nuggets (a superweapon's FireWeapon or
		// Attack).
		const auto *member = world.Get<gameplay::TeamMember>(source);
		RunCreationList(game, ocl->creationList, {primary, world.Get<gameplay::Transform>(source)->facing, member != nullptr ? member->team : team, source, 0u, 0u,
			Engine::Math::FixedVector3{target.x, target.y, game.ground.At(target)}});
		return;
	}
	for (const content::DeliveryNugget &run : runs->second)
		detail::Deliver(game, run, primary, target, team, source);
}

// isObjectShroudedForAction: a human player's order (not a script's) at something fogged or shrouded to that player
// (where it stands).
inline bool ShroudedForAction(const GameWorld &game, ecs::Entity source, ecs::Entity target, bool fromScript = false)
{
	if (fromScript)
		return false;
	const std::uint32_t player = OwnerPlayer(game, source);
	const auto *shroud = game.world.FindResource<gameplay::ShroudMap>();
	const auto *at = game.world.Get<gameplay::Transform>(target);
	if (shroud == nullptr || at == nullptr || player >= game.roster.PlayerCount() || !game.roster.PlayerAt(player).human)
		return false;
	return shroud->StatusAt(player, at->position.x, at->position.y) != gameplay::CellShroud::Clear;
}

// The actions' test for a hidden target (OBJECT_STATUS_STEALTHED, not DETECTED and not DISGUISED).
inline bool HiddenFromActions(const GameWorld &game, ecs::Entity target)
{
	const auto *stealth = game.world.Get<gameplay::Stealth>(target);
	return stealth != nullptr && stealth->HiddenUndisguised();
}

// ActionManager's appearsToContainFriendlies: a container whose apparent controlling player to the source's player
// (ContainModuleInterface::getApparentControllingPlayer: its own, but a building held only by undetected stealthy
// garrisoners looks to the players its side is not allied with as it did, its original player's) is not an enemy of the
// source's team.
inline bool AppearsToContainFriendlies(const GameWorld &game, ecs::Entity source, ecs::Entity target)
{
	if (!game.world.Has<gameplay::Transport>(target))
		return false;
	const auto *relationships = game.world.FindResource<gameplay::Relationships>();
	if (relationships == nullptr)
		return false;
	const std::uint32_t viewer = OwnerPlayer(game, source);
	std::uint32_t apparent = OwnerPlayer(game, target);
	if (const auto *garrison = game.world.Get<gameplay::Garrison>(target);
		garrison != nullptr && garrison->originalPlayer != gameplay::Garrison::NoTeam && viewer < 32 && ((garrison->hiddenFrom >> viewer) & 1u) != 0)
		apparent = garrison->originalPlayer;
	const auto *member = game.world.Get<gameplay::TeamMember>(source);
	return relationships->Between(member != nullptr ? member->team : gameplay::Relationships::NoTeam, viewer, gameplay::Relationships::NoTeam, apparent) !=
		gameplay::Relationship::Enemies;
}

// ActionManager::canCaptureBuilding: the source's capture power (the infantry's, else the Black Lotus') fully ready; not
// something IMMUNE_TO_CAPTURE, dying, no STRUCTURE, being built or sold, or fogged to a human player's order; an
// enemy's, or a CAPTURABLE one not an ally's; not stealthed, undetected and undisguised; not garrisoned (stealthy
// garrisons count as empty); not seeming to hold friends (appearsToContainFriendlies).
inline bool CanCaptureBuilding(GameWorld &game, ecs::Entity source, ecs::Entity target, bool fromScript = false)
{
	const auto &world = game.world;
	const auto *ref = world.Get<gameplay::DefinitionRef>(target);
	const auto *relationships = world.FindResource<gameplay::Relationships>();
	if (ref == nullptr || relationships == nullptr)
		return false;
	const gameplay::SpecialPowerTimer *capture = nullptr;
	if (const auto *timers = world.Get<gameplay::SpecialPowerTimers>(source))
		for (const std::string_view type : {"SPECIAL_INFANTRY_CAPTURE_BUILDING", "SPECIAL_BLACKLOTUS_CAPTURE_BUILDING"})
		{
			for (std::uint32_t index = 0; index < timers->count && capture == nullptr; ++index)
				if (game.templates.Content().powers.templates[timers->timers[index].power].type == type)
					capture = &timers->timers[index];
			if (capture != nullptr)
				break;
		}
	if (capture == nullptr || gameplay::PercentReady(*capture, ClockFor(game, OwnerPlayer(game, source))) < Fixed::One())
		return false;
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
	if (kind.Is("IMMUNE_TO_CAPTURE") || world.Get<gameplay::Dying>(target) != nullptr || !kind.Is("STRUCTURE"))
		return false;
	if (world.Has<gameplay::UnderConstruction>(target) || world.Has<gameplay::Sale>(target) || ShroudedForAction(game, source, target, fromScript))
		return false;
	const gameplay::Relationship relation = RelationOf(game, source, target);
	if (!(relation == gameplay::Relationship::Enemies || (kind.Is("CAPTURABLE") && relation != gameplay::Relationship::Allies)))
		return false;
	if (HiddenFromActions(game, target))
		return false;
	if (world.Has<gameplay::Garrison>(target))
		for (const ecs::Entity inside : game.manifest.Aboard(target))
		{
			const auto *insideRef = world.Get<gameplay::DefinitionRef>(inside);
			if (insideRef == nullptr || !game.templates.DefinitionAt(insideRef->index).Is("STEALTH_GARRISON"))
				return false;
		}
	return !AppearsToContainFriendlies(game, source, target);
}

// ActionManager::canDisableVehicleViaHacking without the source's own checks (canDoSpecialPowerAtObject's
// SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK): not dead, no AIRCRAFT nor an airborne target, not fogged to a human player's
// order; an enemy's VEHICLE, not hidden, not seeming to hold friends.
inline bool CanDisableVehicleViaHacking(GameWorld &game, ecs::Entity source, ecs::Entity target, bool fromScript = false)
{
	const auto &world = game.world;
	const auto *ref = world.Get<gameplay::DefinitionRef>(target);
	if (ref == nullptr || world.Get<gameplay::Dying>(target) != nullptr)
		return false;
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
	const auto *body = world.Get<gameplay::Targetable>(target);
	if (kind.Is("AIRCRAFT") || (body != nullptr && (body->classes & (gameplay::target_class::AirborneVehicle | gameplay::target_class::AirborneInfantry)) != 0))
		return false;
	if (ShroudedForAction(game, source, target, fromScript))
		return false;
	if (RelationOf(game, source, target) != gameplay::Relationship::Enemies || !kind.Is("VEHICLE"))
		return false;
	return !HiddenFromActions(game, target) && !AppearsToContainFriendlies(game, source, target);
}

// ActionManager::canStealCashViaHacking's target checks (its source's power and readiness are the caller's): not dead,
// not under construction, not fogged to a human player's order; an enemy's CASH_GENERATOR, CAPTURABLE and no
// REBUILD_HOLE, not hidden, not seeming to hold friends.
inline bool CanStealCashViaHacking(GameWorld &game, ecs::Entity source, ecs::Entity target, bool fromScript = false)
{
	const auto &world = game.world;
	const auto *ref = world.Get<gameplay::DefinitionRef>(target);
	if (ref == nullptr || world.Get<gameplay::Dying>(target) != nullptr || world.Has<gameplay::UnderConstruction>(target))
		return false;
	if (ShroudedForAction(game, source, target, fromScript) || RelationOf(game, source, target) != gameplay::Relationship::Enemies)
		return false;
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
	if (!kind.Is("CASH_GENERATOR") || !kind.Is("CAPTURABLE") || kind.Is("REBUILD_HOLE"))
		return false;
	return !HiddenFromActions(game, target) && !AppearsToContainFriendlies(game, source, target);
}

// ActionManager::canDoSpecialPowerAtObject's target checks (the source's own, readiness and science, are the caller's):
// the target not dead, the source with a module for the power, the target not fogged to a human player's order; a
// capture as canCaptureBuilding; the Black Lotus' vehicle and cash hacks as canDisableVehicleViaHacking and
// canStealCashViaHacking; a hacker's building hack an enemy's CAPTURABLE structure that is no rebuild hole; a cash hack
// an enemy's finished CAPTURABLE CASH_GENERATOR structure that is no rebuild hole; remote or timed charges a structure or vehicle (no bridge nor bridge tower) the source has a charge left for and
// none on already (from either its remote or its timed charges); TNT a structure or a vehicle that does not fly; a booby
// trap a neutral or allied structure. The other object powers are not ported (the Helix's napalm bomb among them): none
// may be ordered.
inline bool CanTargetWithPower(GameWorld &game, ecs::Entity source, std::uint32_t power, ecs::Entity target, bool fromScript = false)
{
	const auto &world = game.world;
	if (!world.IsAlive(target) || world.Get<gameplay::Dying>(target) != nullptr)
		return false;
	const auto *timers = world.Get<gameplay::SpecialPowerTimers>(source);
	if (timers == nullptr || timers->Find(power) == nullptr)
		return false;
	const auto *ref = world.Get<gameplay::DefinitionRef>(target);
	const auto *relationships = world.FindResource<gameplay::Relationships>();
	if (ref == nullptr || relationships == nullptr)
		return false;
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
	const std::string &type = game.templates.Content().powers.templates[power].type;
	if (ShroudedForAction(game, source, target, fromScript))
		return false;
	if (type == "SPECIAL_INFANTRY_CAPTURE_BUILDING" || type == "SPECIAL_BLACKLOTUS_CAPTURE_BUILDING")
		return CanCaptureBuilding(game, source, target, fromScript);
	if (type == "SPECIAL_BLACKLOTUS_DISABLE_VEHICLE_HACK")
		return CanDisableVehicleViaHacking(game, source, target, fromScript);
	if (type == "SPECIAL_BLACKLOTUS_STEAL_CASH_HACK")
		return CanStealCashViaHacking(game, source, target, fromScript);
	// SPECIAL_HACKER_DISABLE_BUILDING: an enemy's STRUCTURE, CAPTURABLE and no REBUILD_HOLE.
	if (type == "SPECIAL_HACKER_DISABLE_BUILDING")
		return kind.Is("STRUCTURE") && RelationOf(game, source, target) == gameplay::Relationship::Enemies && kind.Is("CAPTURABLE") && !kind.Is("REBUILD_HOLE");
	if (game.templates.Content().powers.templates[power].type == "SPECIAL_CASH_HACK")
		return kind.Is("STRUCTURE") && RelationOf(game, source, target) == gameplay::Relationship::Enemies && kind.Is("CAPTURABLE") &&
			!kind.Is("REBUILD_HOLE") && world.Get<gameplay::UnderConstruction>(target) == nullptr && kind.Is("CASH_GENERATOR");
	if (type == "SPECIAL_REMOTE_CHARGES" || type == "SPECIAL_TIMED_CHARGES")
	{
		if (kind.Is("BRIDGE") || kind.Is("BRIDGE_TOWER") || (!kind.Is("STRUCTURE") && !kind.Is("VEHICLE")))
			return false;
		const bool remote = type == "SPECIAL_REMOTE_CHARGES";
		const auto slot = SlotOfKind(game, source, remote ? AbilityKind::RemoteCharges : AbilityKind::TimedCharges);
		if (!slot || SpecialObjectCount(game, source, *slot) >= world.Get<SpecialAbilities>(source)->slots[*slot].maxSpecialObjects)
			return false;
		if (HasSpecialObjectOn(game, source, *slot, target))
			return false;
		// Nor where its other charges are (a remote charge where it has a timed one, or the other way round).
		const auto other = SlotOfKind(game, source, remote ? AbilityKind::TimedCharges : AbilityKind::RemoteCharges);
		return !other || !HasSpecialObjectOn(game, source, *other, target);
	}
	if (type == "SPECIAL_TANKHUNTER_TNT_ATTACK")
		return kind.Is("STRUCTURE") || (kind.Is("VEHICLE") && !kind.Is("AIRCRAFT"));
	if (type == "SPECIAL_BOOBY_TRAP")
	{
		const gameplay::Relationship relation = RelationOf(game, source, target);
		return kind.Is("STRUCTURE") && (relation == gameplay::Relationship::Neutral || relation == gameplay::Relationship::Allies);
	}
	return false;
}

// CashHackSpecialPower::doSpecialPowerAtObject: not while the hacker is disabled. The module's own firing (paused, it
// neither triggers nor recharges) comes first; the steal follows even so, as the original: the first UpgradeMoneyAmount
// whose science its player knows, else MoneyAmount, at most what the target's player has, from them to the hacker's.
inline void CashHack(GameWorld &game, ecs::Entity source, gameplay::SpecialPowerTimer &timer, ecs::Entity target)
{
	auto &world = game.world;
	const content::GameContent &content = game.templates.Content();
	const auto hack = content::FindCashHack(game.templates.DefinitionAt(world.Get<gameplay::DefinitionRef>(source)->index), content.powers.templates[timer.power].name);
	if (!hack || IsDisabled(game, source))
		return;
	const Engine::Math::FixedVector3 at = world.Get<gameplay::Transform>(target)->position;
	if (timer.pausedCount == 0)
		TriggerSpecialPower(game, source, timer.power, at);
	const std::uint32_t self = OwnerPlayer(game, source), victim = OwnerPlayer(game, target);
	std::int64_t amount = hack->amount;
	const auto &sciences = world.Resource<gameplay::PlayerSciences>();
	for (const auto &[science, steal] : hack->upgrades)
		if (const auto bit = game.templates.Content().Science(science); bit && sciences.Has(self, *bit))
		{
			amount = steal;
			break;
		}
	auto &money = world.Resource<gameplay::PlayerMoney>();
	const std::int64_t cash = std::min(amount, money.Balance(victim));
	if (cash <= 0)
		return;
	money.Withdraw(victim, cash);
	money.Earn(self, cash); // CashHackSpecialPower: addMoneyEarned
	if (auto *notices = world.FindResource<CashNotices>())
	{
		Engine::Math::FixedVector3 over = world.Get<gameplay::Transform>(source)->position;
		over.z = over.z + Fixed::FromInt(20);
		notices->list.push_back({CashNotice::Kind::Stolen, cash, over});
		Engine::Math::FixedVector3 under = at;
		under.z = under.z + Fixed::FromInt(30);
		notices->list.push_back({CashNotice::Kind::Lost, cash, under});
	}
}

// Object::doSpecialPowerUsingWaypoints (a script's, so forced: no readiness asked) -> SpecialPowerModule::
// doSpecialPowerUsingWaypoints: not while disabled or paused; its update modules told of the waypoint
// (initiateIntentToDoSpecialPower with no object or spot). Only a particle cannon follows one (its scripted waypoint
// mode: ready at once, the beam at the waypoint heading down one of its links chosen at random); it fires now (as when
// fired at a spot). Any other power fires now unless its update module starts it (UpdateModuleStartsAttack).
void FireSpecialPowerUsingWaypoints(GameWorld &game, ecs::Entity source, const std::string &power, std::uint32_t waypoint)
{
	auto &world = game.world;
	gameplay::SpecialPowerTimer *timer = PowerModuleFor(game, source, power, true);
	if (timer == nullptr || IsDisabled(game, source) || timer->pausedCount > 0 || waypoint >= game.waypoints.Size())
		return;
	const std::uint32_t definition = world.Get<gameplay::DefinitionRef>(source)->index;
	if (auto *cannon = world.Get<ParticleCannon>(source))
		if (const ParticleCannonConfig *config = game.templates.ParticleCannonOf(definition); config != nullptr && config->power == timer->power)
		{
			const auto *seed = world.FindResource<engine::gameplay::RandomSeed>();
			StartCannonAttack(*cannon, *config, game.tick, game.waypoints.Position(waypoint), false, waypoint, &game.waypoints, seed != nullptr ? seed->value : 0,
				world.Resource<ParticleCannonEvents>(), source);
			TriggerSpecialPower(game, source, timer->power, cannon->initialTarget);
			return;
		}
	for (const content::PowerModule &module : content::PowerModulesOf(game.templates.DefinitionAt(definition)))
		if (module.power == power)
		{
			if (!module.updateModuleStartsAttack)
				TriggerSpecialPower(game, source, timer->power, std::nullopt);
			return;
		}
}

// Firing a power at an object (AIGroup::groupDoSpecialPowerAtObject for a player's order, doNamedFireSpecialPowerAtNamed
// for a script's): its module's doSpecialPowerAtObject.
void FireSpecialPowerAtObject(GameWorld &game, ecs::Entity source, const std::string &power, ecs::Entity target, bool fromScript = false)
{
	auto &world = game.world;
	gameplay::SpecialPowerTimer *timer = PowerModuleFor(game, source, power, fromScript);
	if (timer == nullptr || !world.IsAlive(target) || world.Get<gameplay::Transform>(target) == nullptr)
		return;
	if (!fromScript && !CanTargetWithPower(game, source, timer->power, target))
		return;
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(world.Get<gameplay::DefinitionRef>(source)->index);
	if (content::FindCashHack(kind, power))
		CashHack(game, source, *timer, target);
	else
		for (const content::PowerModule &module : content::PowerModulesOf(kind))
		{
			// DefectorSpecialPower::doSpecialPowerAtObject: not while disabled; triggered, then the target defects to the
			// user's player's default team, hidden for the power's DetectionTime (Object::defect).
			if (module.type == "DefectorSpecialPower" && module.power == power)
			{
				if (IsDisabled(game, source) || timer->pausedCount > 0)
					break;
				const std::uint32_t index = timer->power;
				TriggerSpecialPower(game, source, index, world.Get<gameplay::Transform>(target)->position);
				const std::uint32_t player = world.Get<gameplay::Owner>(source)->player;
				Defect(game, target, DefaultTeamOf(game, source, player), game.templates.Content().powers.templates[index].detectionTicks);
				break;
			}
			// FireWeaponPower::doSpecialPowerAtObject: triggered, reloaded, an attack on it of MaxShotsToFire shots (aiAttackObject).
			if (module.type == "FireWeaponPower" && module.power == power)
			{
				if (IsDisabled(game, source) || timer->pausedCount > 0)
					break;
				TriggerSpecialPower(game, source, timer->power, world.Get<gameplay::Transform>(target)->position);
				ReloadAllAmmo(game, source);
				AiCommanded(game, source);
				if (auto *attack = world.Get<gameplay::AttackTarget>(source))
					*attack = gameplay::AttackTarget{.target = target, .ordered = true, .shotsLeft = module.maxShotsToFire};
				break;
			}
			if (module.type != "SpecialAbility" || module.power != power)
				continue;
			// SpecialAbility::doSpecialPowerAtObject: not while disabled; SpecialPowerModule's: not while paused. Its
			// update module starts it; the power fires now unless the update does it (UpdateModuleStartsAttack).
			if (IsDisabled(game, source) || timer->pausedCount > 0)
				break;
			const std::uint32_t index = timer->power;
			InitiateAbility(game, source, index, target, std::nullopt);
			if (!module.updateModuleStartsAttack && world.IsAlive(target))
				TriggerSpecialPower(game, source, index, world.Get<gameplay::Transform>(target)->position);
			break;
		}
}
}
