export module engine.gameplay.rts.death.systems.death_system;
import std;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.common.identity.components.producer;
export import engine.gameplay.rts.death.components.death_credit;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.rts.containment.systems.unloading_system;
export import engine.gameplay.rts.navigation.definitions.pathfind_cell;
export import engine.gameplay.rts.navigation.resources.navigation_grid;

export import engine.ecs.system.system;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.death.components.crash;
export import engine.gameplay.rts.death.components.collapse;
export import engine.gameplay.rts.death.components.structure_topple;
export import engine.gameplay.rts.death.algorithms.structure_topple;
export import engine.gameplay.rts.death.components.blast_wave;
export import engine.gameplay.common.appearance.components.draw_offset;
export import engine.gameplay.common.physics.resources.physics_settings;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.spatial.components.attitude;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.death.resources.death_events;
export import engine.gameplay.rts.death.algorithms.death_choice;
export import engine.gameplay.rts.lifecycle.algorithms.retirement;
export import engine.gameplay.rts.lifecycle.resources.kill_requests;
export import engine.gameplay.common.lifetime.resources.expirations;
export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.common.physics.algorithms.forces;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.combat.components.aggression;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.containment.resources.evacuations;
export import engine.gameplay.rts.containment.systems.cargo_transfer_system;
export import engine.gameplay.rts.lifecycle.systems.removal_system;
export import engine.gameplay.rts.veterancy.components.experience;
export import engine.gameplay.rts.upgrades.components.upgradable;
export import engine.gameplay.rts.upgrades.resources.player_upgrades;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.status.components.status_flags;
export import engine.gameplay.rts.death.resources.hulk_lifetime;

// After the step, one pass over this tick's deaths (from damage, and entities
// killed outright, lives run out), in their deterministic order: each leaves its team,
// name and transport (riders go down with it), is recorded as a casualty,
// and plays its die behaviours: effects at the moment of death, then either
// removal at once or a slow death. A dying entity keeps its body but stops
// taking part: it loses what makes it targetable, armed, aggressive and
// able to move (as a cleanup-state entity), all through the tick's commands.
export namespace engine::gameplay
{
struct DeathSystem
{
	using Query = ecs::Query<ecs::Read<Mortality>>;
	using Lookup = ecs::Lookup<ecs::Read<TeamMember>, ecs::Read<DefinitionRef>, ecs::Read<Transform>, ecs::Read<Mortality>, ecs::Read<Dying>,
		ecs::Read<Locomotion>, ecs::Read<Attitude>, ecs::Read<Experience>, ecs::Read<PhysicsBody>, ecs::Read<Upgradable>, ecs::Read<Owner>, ecs::Read<HealthFloor>,
		ecs::Read<Health>, ecs::Read<Subdual>,
		ecs::Read<StatusFlags>, ecs::Read<Transport>, ecs::Read<OffMap>, ecs::Read<DeathCredit>, ecs::Read<Producer>, ecs::Read<UnderConstruction>, ecs::Read<ScriptedTopple>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>, ecs::Read<Deaths>, ecs::Write<KillRequests>, ecs::Read<Expirations>, ecs::Read<DeathCatalog>, ecs::Write<DeathEvents>,
		ecs::Write<TeamRoster>, ecs::Write<NameRegistry>, ecs::Write<CargoManifest>, ecs::Write<Casualties>, ecs::Read<GroundHeight>, ecs::Read<PhysicsSettings>, ecs::Read<PlayerUpgrades>,
		ecs::Read<NavigationGrid>, ecs::Read<Evacuations>, ecs::Read<HulkLifetime>>;

	void Execute(ecs::SystemContext &context)
	{
		const Deaths &deaths = context.Read<Deaths>();
		KillRequests &kills = context.Write<KillRequests>();
		const Expirations &expirations = context.Read<Expirations>();
		DeathEvents &events = context.Write<DeathEvents>();
		TeamRoster &roster = context.Write<TeamRoster>();
		NameRegistry &names = context.Write<NameRegistry>();
		CargoManifest &manifest = context.Write<CargoManifest>();
		Casualties &casualties = context.Write<Casualties>();
		events.events.clear();
		events.removed.clear();
		struct Cause
		{
			std::uint32_t deathType{0};
			Engine::Math::Fixed overkill;
			std::uint32_t damageType{0xFFFFFFFFu};
			std::optional<Engine::Math::Fixed> previous; // its health before the killing blow (killed outright: as it is)
		};
		std::vector<Retiree> retirees;
		std::unordered_map<std::uint64_t, Cause> causes; // by entity; the first cause of a tick wins
		const auto key = [](ecs::Entity entity) { return (static_cast<std::uint64_t>(entity.index) << 32) | entity.generation; };
		const auto lookup = context.Lookup<Lookup>();
		deaths.ForEach([&](const Death &death) {
			if (lookup.Get<Dying>(death.entity) == nullptr)
			{
				retirees.push_back({death.entity, death.killer, Departure::Killed});
				causes.try_emplace(key(death.entity), Cause{death.deathType, death.overkill, death.damageType, death.previous});
			}
		});
		// An immortal body survives even being killed outright (kill() is unresistable damage it keeps a point of).
		// An indestructible body is not killed either (kill() is damage it ignores).
		const auto immortal = [&](ecs::Entity entity) {
			const HealthFloor *floor = lookup.Get<HealthFloor>(entity);
			const Health *health = lookup.Get<Health>(entity);
			return (floor != nullptr && floor->Immortal()) || (health != nullptr && health->indestructible);
		};
		for (const ecs::Entity killed : kills.entities)
			if (lookup.IsAlive(killed) && lookup.Get<Dying>(killed) == nullptr && !immortal(killed))
			{
				retirees.push_back({killed, {}, Departure::Killed});
				causes.try_emplace(key(killed), Cause{});
			}
		kills.entities.clear();
		for (const TypedKill &killed : kills.typed)
			if (lookup.IsAlive(killed.entity) && lookup.Get<Dying>(killed.entity) == nullptr && !immortal(killed.entity))
			{
				retirees.push_back({killed.entity, killed.killer, Departure::Killed});
				causes.try_emplace(key(killed.entity), Cause{killed.deathType, {}, killed.damageType});
			}
		kills.typed.clear();
		// Lives that ran out die as if killed outright (in the way they say: toppled, ...).
		expirations.ForEach([&](const Expiration &expired) {
			if (lookup.IsAlive(expired.entity) && lookup.Get<Dying>(expired.entity) == nullptr && !immortal(expired.entity))
			{
				retirees.push_back({expired.entity, {}, Departure::Killed});
				causes.try_emplace(key(expired.entity), Cause{expired.deathType});
			}
		});
		if (retirees.empty())
			return;

		// Riders are added after the dead by Retire: a killed container's by what its death does to them (RiderFate);
		// others go down with theirs.
		const GroundHeight &ground = context.Read<GroundHeight>();
		const NavigationGrid &grid = context.Read<NavigationGrid>();
		auto &commands = context.Commands();
		const Evacuations &evacuations = context.Read<Evacuations>();
		const auto fate = [&](const Retiree &container, ecs::Entity rider) {
			// Emptied first (evacuateTeam): out where it is, unhurt.
			if (evacuations.Contains(container.entity) && lookup.IsAlive(rider))
			{
				commands.Remove<OffMap>(rider);
				commands.Remove<Passenger>(rider);
				return RiderFate::Survives;
			}
			const Transport *transport = lookup.Get<Transport>(container.entity);
			if (container.departure != Departure::Killed || transport == nullptr || !lookup.IsAlive(rider))
				return RiderFate::GoesDown;
			const TransportDefinition &carrier = transport->definition;
			const Health *health = lookup.Get<Health>(rider);
			const HealthFloor *floor = lookup.Get<HealthFloor>(rider);
			// An indestructible rider takes none of it and is not killed (attemptDamage / kill ignore it).
			const bool indestructible = health != nullptr && health->indestructible;
			const bool immortal = (floor != nullptr && floor->Immortal()) || indestructible;
			Engine::Math::Fixed left = health != nullptr ? health->current : Engine::Math::Fixed{};
			// processDamageToContained: its share of each one's maximum, unresistable; all of it kills.
			if (carrier.riderDamage > Engine::Math::Fixed{} && health != nullptr && !indestructible)
			{
				const Engine::Math::Fixed damage = health->maximum * carrier.riderDamage;
				if (left - damage <= Engine::Math::Fixed{} && !immortal)
				{
					causes.try_emplace(key(rider), Cause{carrier.riderDeathType, (damage - left) / std::max(health->maximum, Engine::Math::Fixed::One()),
						carrier.riderDamageType, left});
					commands.Remove<OffMap>(rider);
					commands.Remove<Passenger>(rider);
					return carrier.deletesRiders ? RiderFate::KilledAndDeleted : RiderFate::Killed;
				}
				left = std::max(left - damage, immortal ? Engine::Math::Fixed::One() : Engine::Math::Fixed{});
			}
			// RiderChangeContain::onRemoving: a dead bike's rider is destroyed.
			if (carrier.deletesRiders)
				return RiderFate::Deleted;
			// killRidersWhoAreNotFreeToExit (TransportContain::isSpecificRiderFreeToExit): one that may not get out here
			// (the carrier has to land first; the ground under it is not the rider's) is killed or deleted.
			if (carrier.checksRiderExit)
			{
				const Transform *at = lookup.Get<Transform>(container.entity);
				bool free = at != nullptr;
				if (free && !carrier.unloadInAir && at->position.z - ground.Surface(at->position.XY()) > LandedHeight())
					free = false;
				const Locomotion *walker = lookup.Get<Locomotion>(rider);
				if (free && walker == nullptr)
					free = false;
				if (free && at->position.z - ground.Surface(at->position.XY()) <= LandedHeight())
				{
					const auto cell = [](Engine::Math::Fixed value) { return static_cast<std::int32_t>((value / Engine::Math::Fixed::FromInt(PathfindCellSize)).Floor()); };
					const std::int32_t x = cell(at->position.x), y = cell(at->position.y);
					if (grid.Contains(x, y))
					{
						const std::uint8_t surfaces = walker->locomotor.surfaces;
						switch (grid.Type(x, y))
						{
						case PathfindCellType::Water: free = (surfaces & 2u) != 0; break;
						case PathfindCellType::Cliff: free = (surfaces & 4u) != 0; break;
						case PathfindCellType::Impassable: free = false; break;
						default: free = (surfaces & 1u) != 0; break;
						}
					}
				}
				if (!free)
				{
					if (carrier.deletesStuckRiders)
						return RiderFate::Deleted;
					if (!immortal)
					{
						causes.try_emplace(key(rider), Cause{carrier.stuckDeathType, {}, carrier.riderDamageType, left});
						commands.Remove<OffMap>(rider);
						commands.Remove<Passenger>(rider);
						return RiderFate::Killed;
					}
				}
			}
			// removeAllContained: out where it was, as hurt as it is now.
			if (health != nullptr && left != health->current)
			{
				Health hurt = *health;
				hurt.current = left;
				hurt.lastAttacker = container.entity;
				commands.Set<Health>(rider, hurt);
			}
			commands.Remove<OffMap>(rider);
			commands.Remove<Passenger>(rider);
			return RiderFate::Survives;
		};
		Retire(std::move(retirees), {roster, names, manifest},
			[&](ecs::Entity entity) {
				return RetireeState{lookup.IsAlive(entity), lookup.Get<TeamMember>(entity), lookup.Get<DefinitionRef>(entity), lookup.Get<Transform>(entity)};
			},
			[&](const Retiree &retiree) {
				const auto found = causes.find(key(retiree.entity));
				const Cause cause = found != causes.end() ? found->second : Cause{};
				Die(context, lookup, retiree, cause.deathType, cause.overkill, cause.damageType, cause.previous);
			},
			casualties, fate);
	}

private:
	// CrushDie's crushLocationCheck: the nearest to the crusher of the victim's centre (all of it) and its front and
	// back crush points (half its major radius along its facing), first found winning ties.
	static CrushLocation CrushedAt(Engine::Math::FixedVector2 crusher, Engine::Math::FixedVector2 victim, Engine::Math::TurnAngle facing,
		Engine::Math::Fixed majorRadius) noexcept
	{
		const auto offset = Engine::Math::Direction(facing) * (majorRadius / Engine::Math::Fixed::FromInt(2));
		CrushLocation at = CrushLocation::Total;
		Engine::Math::Fixed best = Engine::Math::DistanceSquared(victim, crusher);
		if (const auto front = Engine::Math::DistanceSquared(victim + offset, crusher); front < best)
		{
			at = CrushLocation::FrontEnd;
			best = front;
		}
		if (const auto back = Engine::Math::DistanceSquared(victim - offset, crusher); back < best)
			at = CrushLocation::BackEnd;
		return at;
	}

	template<typename LookupType>
	static void Die(ecs::SystemContext &context, const LookupType &lookup, const Retiree &retiree, std::uint32_t deathType, Engine::Math::Fixed overkill,
		std::uint32_t damageType, std::optional<Engine::Math::Fixed> previous = std::nullopt)
	{
		const DeathCatalog &catalog = context.Read<DeathCatalog>();
		DeathEvents &events = context.Write<DeathEvents>();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0xDEADu;
		auto &commands = context.Commands();
		const ecs::Entity entity = retiree.entity;
		// Riders lost with their transport are simply gone.
		if (retiree.departure != Departure::Killed)
		{
			commands.Destroy(entity);
			events.removed.push_back(entity);
			return;
		}
		const Mortality *mortality = lookup.template Get<Mortality>(entity);
		const DeathDefinition &definition = catalog.At(mortality != nullptr ? mortality->death : 0);
		const Transform *transform = lookup.template Get<Transform>(entity);
		const Engine::Math::FixedVector3 position = transform != nullptr ? transform->position : Engine::Math::FixedVector3{};
		const Engine::Math::TurnAngle facing = transform != nullptr ? transform->facing : Engine::Math::TurnAngle{};
		const TeamMember *member = lookup.template Get<TeamMember>(entity);
		const std::uint32_t team = member != nullptr ? member->team : NoTeam;
		auto random = Engine::Math::Stream(seed, {context.Tick(), entity.index, entity.generation, 0xDEADu});
		// Die modules filter by its veterancy level (DieMuxData::isDieApplicable).
		const Experience *experience = lookup.template Get<Experience>(entity);
		const std::uint32_t veterancy = experience != nullptr ? experience->level : 0u;
		// And by its status (its ExemptStatus and RequiredStatus).
		const StatusFlags *flags = lookup.template Get<StatusFlags>(entity);
		const std::uint64_t status = flags != nullptr ? flags->bits : 0u;

		// Upgrade-switched die modules look at the object's and its player's upgrades.
		UpgradeMask upgrades;
		if (const Upgradable *own = lookup.template Get<Upgradable>(entity))
			upgrades.Add(own->completed);
		if (const Owner *owner = lookup.template Get<Owner>(entity))
			upgrades.Add(context.Read<PlayerUpgrades>().Completed(owner->player));
		// Object::isSignificantlyAboveTerrain.
		const bool aloft = position.z - context.Read<GroundHeight>().At(position.XY()) > context.Read<PhysicsSettings>().SignificantHeight();
		const auto where = [&](DieAltitude altitude) { return altitude == DieAltitude::Any || (altitude == DieAltitude::Air) == aloft; };
		for (const DieEffect &effect : definition.atDeath)
			if (effect.filter.Applies(deathType, veterancy, status) && effect.gate.Active(upgrades) && where(effect.altitude))
				if (const auto id = PickEffect(effect.candidates, random))
				{
					DeathEvent event{entity, retiree.killer, effect.kind, *id, position, facing, team, veterancy, effect.orient};
					// TransferPreviousHealth: its maximum less its health before the killing blow, from its last attacker.
					if (effect.transferHealth)
						if (const Health *health = lookup.template Get<Health>(entity))
						{
							event.transfer = true;
							event.transferDamage = health->maximum - previous.value_or(health->current);
							event.transferSource = health->lastAttacker;
							if (const Subdual *subdual = lookup.template Get<Subdual>(entity))
								event.transferSubdual = subdual->damage;
						}
					if (effect.kind == DeathEffectKind::Notice)
						if (const DeathCredit *credit = lookup.template Get<DeathCredit>(entity))
							event.credit = credit->credit;
					if (effect.kind == DeathEffectKind::Release || effect.kind == DeathEffectKind::Weapon)
						if (const Producer *producer = lookup.template Get<Producer>(entity))
							event.credit = producer->entity;
					if (const DefinitionRef *kind = lookup.template Get<DefinitionRef>(entity))
						event.definition = kind->index;
					event.underConstruction = lookup.template Get<UnderConstruction>(entity) != nullptr;
					events.events.push_back(event);
				}

		// Crushed (CrushDie): which end, and that crush's sound.
		std::uint8_t crushed = 0;
		for (const CrushDieDefinition &crush : definition.crushed)
		{
			if (damageType != catalog.crushDamageType || !crush.filter.Applies(deathType, veterancy, status))
				continue;
			const Transform *crusher = lookup.IsAlive(retiree.killer) ? lookup.template Get<Transform>(retiree.killer) : nullptr;
			const CrushLocation at = crusher != nullptr ? CrushedAt(crusher->position.XY(), position.XY(), facing, definition.majorRadius) : CrushLocation::Total;
			const auto index = static_cast<std::size_t>(at);
			if (crush.sound[index] != CrushDieDefinition::NoSound && Engine::Math::UniformInt(random, 0, 99) < crush.percent[index])
				events.events.push_back({entity, retiree.killer, DeathEffectKind::Sound, crush.sound[index], position, facing, team, veterancy});
			crushed = static_cast<std::uint8_t>((at != CrushLocation::BackEnd ? 1u : 0u) | (at != CrushLocation::FrontEnd ? 2u : 0u));
		}

		bool removeNow = false;
		for (const DeathFilter &filter : definition.destroyedAtOnce)
			removeNow = removeNow || filter.Applies(deathType, veterancy, status);
		// Killed, then destroyed at once (a dead bike's rider): its slow death begins (its initial effects) but no body stays.
		if (retiree.deleteAfter && !removeNow)
		{
			if (const auto slow = ChooseSlowDeath(definition, deathType, veterancy, overkill, random, status))
				for (std::uint8_t kind = 0; kind < DeathEffectKinds; ++kind)
					if (const auto id = PickEffect(definition.slow[*slow].effects.Of(DeathPhase::Initial, static_cast<DeathEffectKind>(kind)), random))
						events.events.push_back({entity, retiree.killer, static_cast<DeathEffectKind>(kind), *id, position, facing, team, veterancy});
			removeNow = true;
		}
		if (removeNow)
		{
			commands.Destroy(entity);
			events.removed.push_back(entity);
			return;
		}

		// StructureCollapseUpdate::beginStructureCollapse: when it comes down, and its initial effects.
		for (std::uint32_t index = 0; index < definition.collapses.size(); ++index)
		{
			const CollapseDefinition &collapse = definition.collapses[index];
			if (!collapse.filter.Applies(deathType, veterancy, status))
				continue;
			const auto delay = static_cast<std::uint64_t>(Engine::Math::UniformInt(random, static_cast<std::int64_t>(collapse.minCollapseDelay),
				static_cast<std::int64_t>(std::max(collapse.minCollapseDelay, collapse.maxCollapseDelay))));
			commands.Add<Collapse>(entity, Collapse{context.Tick() + delay, 0, {}, {}, index, CollapseState::Waiting, {}});
			commands.Add<DrawOffset>(entity, DrawOffset{{}, collapse.maxShudder});
			const auto phase = static_cast<std::size_t>(CollapsePhase::Initial);
			if (const auto id = PickEffect(collapse.effects[phase], random))
				events.events.push_back({entity, retiree.killer, DeathEffectKind::Effect, *id, position, facing, team, veterancy});
			if (const auto id = PickEffect(collapse.objects[phase], random))
				events.events.push_back({entity, retiree.killer, DeathEffectKind::Objects, *id, position, facing, team, veterancy});
			break;
		}

		// StructureToppleUpdate::onDie / beginStructureTopple: which way it goes over and when, and its start effects.
		for (std::uint32_t index = 0; index < definition.topples.size(); ++index)
		{
			const StructureToppleDefinition &how = definition.topples[index];
			if (!how.filter.Applies(deathType, veterancy, status))
				continue;
			const Transform *attacker = lookup.IsAlive(retiree.killer) ? lookup.template Get<Transform>(retiree.killer) : nullptr;
			const ScriptedTopple *scripted = lookup.template Get<ScriptedTopple>(entity);
			const structure_topple::ToppleContext at{how, position, facing, context.Read<GroundHeight>(), context.Tick()};
			const StructureTopple topple = structure_topple::BeginStructureTopple(at, index,
				attacker != nullptr ? std::optional{attacker->position.XY()} : std::nullopt, scripted != nullptr ? std::optional{scripted->direction} : std::nullopt,
				damageType, random, [&](DeathEffectKind kind, std::uint32_t id, Engine::Math::FixedVector3 where, bool orient) {
					DeathEvent event{entity, retiree.killer, kind, id, where, facing, team, veterancy};
					event.orient = orient;
					events.events.push_back(event);
				});
			commands.Add<StructureTopple>(entity, topple);
			break;
		}

		const auto slow = ChooseSlowDeath(definition, deathType, veterancy, overkill, random, status);
		const CrashDefinition *crash = slow && definition.slow[*slow].crash.kind != CrashKind::None ? &definition.slow[*slow].crash : nullptr;
		if (crash != nullptr)
		{
			// A jet on the ground (or barely off it) just blows up.
			const PhysicsSettings &physics = context.Read<PhysicsSettings>();
			if (crash->kind == CrashKind::Jet && position.z - context.Read<GroundHeight>().At(position.XY()) <= physics.SignificantHeight())
			{
				for (std::uint8_t kind = 0; kind < DeathEffectKinds; ++kind)
					if (const auto id = PickEffect(definition.slow[*slow].effects.Of(DeathPhase::OnGround, static_cast<DeathEffectKind>(kind)), random))
						events.events.push_back({entity, retiree.killer, static_cast<DeathEffectKind>(kind), *id, position, facing, team, veterancy});
				commands.Destroy(entity);
				events.removed.push_back(entity);
				return;
			}
			// In the air: a jet flies straight on at its speed; both fall and spin.
			const Locomotion *motion = lookup.template Get<Locomotion>(entity);
			const Engine::Math::Fixed speed = motion != nullptr ? motion->speed : Engine::Math::Fixed{};
			Crash state;
			state.velocity = {Engine::Math::Cos(facing) * speed, Engine::Math::Sin(facing) * speed, {}};
			state.fall = physics.gravity * crash->fallFactor;
			state.deathTick = state.spinTick = context.Tick();
			state.rollRate = crash->rollRate;
			if (crash->kind == CrashKind::Helicopter)
			{
				// Down the spiral from its heading, at the spiral's speed, spinning at its least.
				state.velocity = {};
				state.forwardAngle = facing.units;
				state.forwardSpeed = crash->spiralSpeed;
				state.selfSpin = crash->minSelfSpin;
				// m_lastSelfSpinUpdateFrame starts at 0: its spin first changes on its first update.
				state.spinTick = 0;
				if (crash->bladeDelayMax != 0)
					state.bladeTick = context.Tick() + static_cast<std::uint64_t>(Engine::Math::UniformInt(random, static_cast<std::int64_t>(crash->bladeDelayMin),
															   static_cast<std::int64_t>(std::max(crash->bladeDelayMin, crash->bladeDelayMax))));
			}
			commands.Add<Crash>(entity, state);
			if (lookup.template Get<Attitude>(entity) == nullptr)
				commands.Add<Attitude>(entity, Attitude{});
			// A helicopter flown by its locomotor keeps its body, going on at the speed it flew (its physics' velocity);
			// the crash moves it (its locomotor holds it from the physics pass).
			const PhysicsBody *carried = lookup.template Get<PhysicsBody>(entity);
			if (crash->kind == CrashKind::Helicopter && crash->hovering && carried != nullptr)
			{
				PhysicsBody body = *carried;
				body.velocity = {Engine::Math::Cos(facing) * speed, Engine::Math::Sin(facing) * speed, {}};
				body.acceleration = {};
				body.Set(physics_flag::Locomotive, true);
				commands.Set<PhysicsBody>(entity, body);
			}
			else
				commands.Remove<PhysicsBody>(entity);
		}
		Dying dying;
		dying.since = context.Tick();
		dying.crushed = crushed;
		dying.killer = retiree.killer;
		dying.death = mortality != nullptr ? mortality->death : 0;
		dying.deathType = deathType;
		if (slow)
		{
			const SlowDeathDefinition &chosen = definition.slow[*slow];
			const std::uint64_t now = context.Tick();
			dying.slow = *slow;
			dying.sinkRate = chosen.sinkRate;
			if (definition.hulk && context.Read<HulkLifetime>().Overridden())
			{
				// Scripts want no hulks around: it sinks at once, its midpoint half a second on, gone after a second.
				dying.sinkTick = now + 1;
				dying.midpointTick = now + 30 / 2 + 1;
				dying.destructionTick = now + 30 + 1;
			}
			else
			{
				dying.sinkTick = now + VariedDelay(chosen.sinkDelay, chosen.sinkDelayVariance, random);
				const std::uint64_t destruction = VariedDelay(chosen.destructionDelay, chosen.destructionDelayVariance, random);
				dying.destructionTick = now + destruction;
				// The midpoint falls between 35% and 65% of the way to destruction, as the original.
				dying.midpointTick = now + static_cast<std::uint64_t>(Engine::Math::UniformInt(random, static_cast<std::int64_t>(destruction * 35 / 100),
											   static_cast<std::int64_t>(destruction * 65 / 100)));
			}
			// Flung (SlowDeathBehavior::beginSlowDeath, calcRandomForce): at least a unit off the ground, pushed by a
			// random force (any direction, raised by the pitch), facing where it flies, on its own physics (no
			// longer its locomotor's), sliding a little further on the ground.
			const PhysicsBody *body = lookup.template Get<PhysicsBody>(entity);
			if (chosen.fling.force > Engine::Math::Fixed{} && body != nullptr && transform != nullptr)
			{
				using Engine::Math::Fixed;
				const Engine::Math::TurnAngle angle{static_cast<std::uint32_t>(Engine::Math::UniformInt(random, 0, 0xFFFFFFFFll))};
				const Engine::Math::TurnAngle pitch{static_cast<std::uint32_t>(chosen.fling.pitch.units +
					static_cast<std::uint32_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(chosen.fling.pitchVariance.units))))};
				const Fixed magnitude = Engine::Math::UniformFixed(random, chosen.fling.force, chosen.fling.force + chosen.fling.forceVariance);
				const Engine::Math::FixedVector3 force{Engine::Math::Cos(angle) * Engine::Math::Cos(pitch) * magnitude,
					Engine::Math::Sin(angle) * Engine::Math::Cos(pitch) * magnitude, Engine::Math::Sin(pitch) * magnitude};
				PhysicsBody flung = *body;
				ApplyForce(flung, force);
				flung.Set(physics_flag::AllowToFall, true);
				flung.Set(physics_flag::AllowBouncing, true);
				flung.Set(physics_flag::Locomotive, false);
				flung.extraFriction = Fixed{} - Fixed::FromRatio(3, 30); // -3 * SECONDS_PER_LOGICFRAME_REAL
				commands.Set<PhysicsBody>(entity, flung);
				Transform thrown = *transform;
				const Fixed floor = context.Read<GroundHeight>().At(thrown.position.XY());
				if (thrown.position.z - floor < Fixed::One())
					thrown.position.z += Fixed::One();
				thrown.facing = Engine::Math::Heading(force.XY());
				commands.Set<Transform>(entity, thrown);
				if (lookup.template Get<Attitude>(entity) != nullptr)
					commands.Set<Attitude>(entity, Attitude{});
				dying.flung = 1;
			}
			// A sinking body is held: forces no longer move it (as the original). A flung one lets go when it sinks.
			else if (chosen.sinkRate > Engine::Math::Fixed{})
				commands.Remove<PhysicsBody>(entity);
			// NeutronMissileSlowDeathBehavior: its blast waves (BlastWaveSystem) count from now; its first update (woken at
			// once: its SinkDelay is none) plays its FXList on the ground under it (doFXPos).
			if (!chosen.wave.blasts.empty() || chosen.wave.effect != BlastWaveDefinition::NoEffect)
			{
				commands.Add<BlastWave>(entity, BlastWave{});
				if (chosen.wave.effect != BlastWaveDefinition::NoEffect)
				{
					DeathEvent event{entity, retiree.killer, DeathEffectKind::Effect, chosen.wave.effect,
						{position.x, position.y, context.Read<GroundHeight>().At(position.XY())}, facing, team, veterancy};
					event.orient = false;
					events.events.push_back(event);
				}
			}
		}
		else
		{
			// Nothing removes it: the body stays, dead.
			dying.slow = Dying::Lingering;
			dying.sinkTick = dying.midpointTick = dying.destructionTick = Dying::Never;
		}
		commands.Add<Dying>(entity, dying);
		// No longer part of the fight.
		commands.Remove<Targetable>(entity);
		commands.Remove<Armament>(entity);
		commands.Remove<AttackTarget>(entity);
		commands.Remove<Aggression>(entity);
		commands.Remove<MoveOrder>(entity);
		commands.Remove<Boarding>(entity);
		if (!slow)
			return;
		const PhaseEffects &effects = definition.slow[*slow].effects;
		for (std::uint8_t kind = 0; kind < DeathEffectKinds; ++kind)
			if (const auto id = PickEffect(effects.Of(DeathPhase::Initial, static_cast<DeathEffectKind>(kind)), random))
				events.events.push_back({entity, retiree.killer, static_cast<DeathEffectKind>(kind), *id, position, facing, team, veterancy});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::DeathSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.death";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	// Deaths settle teams, names and cargo before carriers retire.
	using Before = SystemTypeList<engine::gameplay::RemovalSystem>;
	// Both keep the cargo manifest: boarding and unloading settle first.
	using After = SystemTypeList<engine::gameplay::CargoTransferSystem>;
};
}
