export module games.generalszh.gameplay.objects.resources.object_templates;
export import games.generalszh.content.aircraft.aircraft_content;
export import games.generalszh.content.containment.railed_transport_content;
import games.generalszh.content.objects.model_conditions;
import std;
import games.generalszh.content.combat.weapon_bonus_content;
export import games.generalszh.gameplay.combat.components.firestorm;
export import games.generalszh.gameplay.powers.components.leaflet_drop;
export import games.generalszh.content.vision.dynamic_clearing_content;
export import engine.gameplay.rts.parachute.definitions.parachute_definition;

export import games.generalszh.content.loading.game_content;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.health.resources.armor_catalog;
export import engine.gameplay.rts.combat.resources.launch_layouts;
export import engine.gameplay.common.identity.resources.template_equivalence;
export import engine.gameplay.rts.upgrades.resources.upgrade_triggers;
export import engine.gameplay.rts.veterancy.resources.veterancy_catalog;
export import games.generalszh.content.horde.horde_content;
export import engine.gameplay.rts.harvesting.resources.harvest_catalog;
export import engine.gameplay.rts.loadout.resources.loadout_catalog;
export import games.generalszh.gameplay.powers.resources.spy_vision_catalog;
export import games.generalszh.gameplay.combat.resources.cooldown_creation_catalog;
export import games.generalszh.gameplay.powers.components.launcher_door;
export import games.generalszh.gameplay.powers.components.particle_cannon;
export import games.generalszh.gameplay.ai.components.mob_member;
export import games.generalszh.gameplay.abilities.components.sticky_bomb;
export import games.generalszh.gameplay.hacking.components.internet_hack;
export import games.generalszh.gameplay.battleplans.components.battle_plan;
export import games.generalszh.gameplay.economy.components.warehouse_crippling;
export import games.generalszh.gameplay.combat.components.battle_bus;
export import games.generalszh.gameplay.combat.components.weapon_bonus_pulse;
export import games.generalszh.gameplay.creation.components.ocl_timer;
export import games.generalszh.gameplay.powers.components.spectre_gunship;
export import games.generalszh.gameplay.railroad.resources.rail_network;
import games.generalszh.content.railroad.railroad_content;
export import games.generalszh.content.containment.combat_drop_content;
export import games.generalszh.content.crates.crate_content;
import games.generalszh.content.combat.cooldown_creation_content;
import games.generalszh.content.combat.combat_catalog;
import games.generalszh.content.powers.special_powers;
export import games.generalszh.content.combat.loadout_content;
export import games.generalszh.gameplay.upgrades.resources.upgrade_effects;
export import engine.gameplay.common.spatial.components.targetable;
export import games.generalszh.content.death.death_content;
import engine.ecs.system.system;
export import engine.core.serialization.byte_stream;

// The definitions, weapons, armors and deaths in play, indexed in the order
// they first appear (deterministic: every peer spawns the same things in the
// same order). DefinitionRef, Armament, Health and Mortality hold these
// indices; the engine systems read the catalogs (world resources). Death effects (FX lists,
// object creation lists, weapons) are named ids the game resolves.
export namespace generalszh::gameplay
{
// A definition's BunkerBusterBehavior (see bunker_busters): UpgradeRequired, DetonationFX, ShockwaveWeaponTemplate and
// OccupantDamageWeaponTemplate by name (empty: none).
struct BunkerBusterConfig
{
	bool present{false};
	std::string upgrade;
	std::string detonationFX;
	std::string shockwaveWeapon;
	std::string occupantWeapon;
};

// A definition's railed transport (the ferry): its content and its dock's bones in its own frame (DockWaitingNN,
// DockStart, DockAction, DockEnd; no DockStart: boneless).
struct RailedTransportConfig
{
	content::RailedTransportContent content;
	std::vector<Engine::Math::FixedVector3> approach;
	std::optional<Engine::Math::FixedVector3> enter;
	Engine::Math::FixedVector3 action;
	Engine::Math::FixedVector3 exit;
};

// A definition's NeutronBlastBehavior (see neutron_blasts).
struct NeutronBlastConfig
{
	bool present{false};
	bool affectAirborne{true};
	bool affectAllies{true};
	bool detonateCallsKill{false};
	Engine::Math::Fixed radius{Engine::Math::Fixed::FromInt(10)};
};

class ObjectTemplates
{
public:
	// The catalogs are world resources the systems read; the templates fill them.
	ObjectTemplates(const content::GameContent &content, engine::time::FixedStep step, engine::gameplay::WeaponCatalog &weapons,
		engine::gameplay::ArmorCatalog &armors, engine::gameplay::DeathCatalog &deaths, engine::gameplay::LaunchLayouts &launches,
		engine::gameplay::UpgradeTriggers &upgradeTriggers, UpgradeEffects &upgradeEffects, engine::gameplay::VeterancyCatalog &veterancy,
		engine::gameplay::HordeCatalog &hordes, engine::gameplay::HarvestCatalog &harvest, engine::gameplay::LoadoutCatalog &loadouts,
		engine::gameplay::ParachuteCatalog &parachutes, engine::gameplay::DynamicClearingCatalog &dynamicClearings,
		engine::gameplay::TemplateEquivalence &equivalence) :
		weapons(weapons), armors(armors), deaths(deaths), launches(launches), upgradeTriggers(upgradeTriggers), upgradeEffects(upgradeEffects),
		veterancy(veterancy), hordes(hordes), harvest(harvest), loadouts(loadouts), parachutes(parachutes), dynamicClearings(dynamicClearings),
		equivalence(equivalence),
		m_content(content), m_step(step)
	{
	}

	// A projectile object's body (its ActiveBody, armor, geometry and kinds), to find and hurt it in flight.
	struct ProjectileBody
	{
		bool valid{false};
		Engine::Math::Fixed health;
		std::uint32_t armor{0};
		Engine::Math::Fixed radius;
		std::uint32_t classes{0};
		// Its ActiveBody's subdual damage (a SubdualDamageCap above 0: it can be jammed).
		Engine::Math::Fixed subdualCap;
		Engine::Math::Fixed subdualHealAmount;
		std::uint64_t subdualHealTicks{0};
	};

	const ProjectileBody *BodyOf(std::uint32_t definition) const noexcept
	{
		return definition < m_bodies.size() && m_bodies[definition].valid ? &m_bodies[definition] : nullptr;
	}

	std::uint32_t Definition(const content::ObjectDefinition &object)
	{
		if (const auto found = m_definitionIndex.find(object.name); found != m_definitionIndex.end())
			return found->second;
		m_definitions.push_back(&object);
		m_catalogEntries.push_back(m_content.objects.Find(object.name));
		m_history.emplace_back(Kind::Definition, object.name);
		const content::ObjectCombat combat = content::ReadObjectCombat(object, m_step);
		m_hasAI.push_back(combat.hasAI);
		ProjectileBody body;
		if (object.Is("PROJECTILE") && combat.maxHealth && *combat.maxHealth > Engine::Math::Fixed{})
		{
			body.valid = true;
			body.health = combat.initialHealth > Engine::Math::Fixed{} ? combat.initialHealth : *combat.maxHealth;
			body.armor = Armor(combat.armor);
			body.radius = object.geometry.majorRadius;
			body.subdualCap = combat.subdualCap;
			body.subdualHealAmount = combat.subdualHealAmount;
			body.subdualHealTicks = combat.subdualHealTicks;
			const std::pair<const char *, std::uint32_t> kinds[] = {{"PROJECTILE", engine::gameplay::target_class::Projectile},
				{"SMALL_MISSILE", engine::gameplay::target_class::SmallMissile}, {"BALLISTIC_MISSILE", engine::gameplay::target_class::BallisticMissile}};
			for (const auto &[kind, bit] : kinds)
				if (object.Is(kind))
					body.classes |= bit;
		}
		m_bodies.push_back(body);
		{
			auto death = content::ReadObjectDeath(object, m_step, [this](engine::gameplay::DeathEffectKind kind, std::string_view name) {
				return DeathEffect(kind, name);
			}, &m_content.upgrades);
			// A helicopter's blades come off at its blade bone.
			if (const auto blade = m_content.bladeBones.find(object.name); blade != m_content.bladeBones.end())
				for (auto &slow : death.slow)
					if (slow.crash.kind == engine::gameplay::CrashKind::Helicopter)
						slow.crash.bladeOffset = blade->second;
			// Its spiral is flown by its NORMAL locomotor, working on while it is dead.
			if (const auto hover = m_content.helicopterLocomotors.find(object.name); hover != m_content.helicopterLocomotors.end())
				for (auto &slow : death.slow)
					if (slow.crash.kind == engine::gameplay::CrashKind::Helicopter)
					{
						slow.crash.hover = hover->second;
						slow.crash.hovering = true;
					}
			m_deathOf.push_back(deaths.Add(std::move(death)));
		}
		// Its parachute (ParachuteContain), when it is one.
		if (const auto parachute = m_content.parachutes.find(object.name); parachute != m_content.parachutes.end())
			m_parachuteOf.push_back(parachutes.Add(parachute->second.definition));
		else
			m_parachuteOf.push_back(NoParachute);
		// Where it launches projectiles from (none known: its origin).
		const auto layout = m_content.launchLayouts.find(object.name);
		launches.byDefinition.push_back(layout != m_content.launchLayouts.end() ? layout->second : engine::gameplay::LaunchLayout{});
		// Its index is taken before its upgrades name weapons (whose projectiles are definitions in turn).
		const auto index = static_cast<std::uint32_t>(m_definitions.size() - 1);
		// Its DynamicShroudClearingRangeUpdate.
		dynamicClearings.definitions.resize(std::max<std::size_t>(dynamicClearings.definitions.size(), index + 1));
		dynamicClearings.definitions[index] = content::ReadDynamicClearing(object, m_step);
		// Which kinds it counts as the same as (ThingTemplate::isEquivalentTo): its name's key and its reskin source's.
		const auto family = [&](const std::string &name) {
			return m_families.try_emplace(name, static_cast<std::uint32_t>(m_families.size())).first->second;
		};
		equivalence.self.resize(std::max<std::size_t>(equivalence.self.size(), index + 1), engine::gameplay::TemplateEquivalence::None);
		equivalence.reskinnedFrom.resize(equivalence.self.size(), engine::gameplay::TemplateEquivalence::None);
		equivalence.self[index] = family(object.name);
		equivalence.reskinnedFrom[index] = object.reskinnedFrom.empty() ? engine::gameplay::TemplateEquivalence::None : family(object.reskinnedFrom);
		// Its SpawnPointProductionExitUpdate's places.
		if (const auto points = m_content.spawnPoints.find(object.name); points != m_content.spawnPoints.end())
			m_spawnPointsOf.push_back(&points->second);
		else
			m_spawnPointsOf.push_back(nullptr);
		// Its BoneFXUpdate.
		if (const auto boneFx = m_content.boneFx.find(object.name); boneFx != m_content.boneFx.end())
			m_boneFxOf.push_back(&boneFx->second);
		else
			m_boneFxOf.push_back(nullptr);
		m_definitionIndex.emplace(object.name, index);
		upgradeTriggers.byDefinition.emplace_back();
		// Its veterancy (IGNORED_IN_GUI objects score no kills).
		veterancy.byDefinition.push_back({object.experienceValue, object.experienceRequired, object.trainable, !object.Is("IGNORED_IN_GUI"),
			{object.SkillPointValue(0), object.SkillPointValue(1), object.SkillPointValue(2), object.SkillPointValue(3)}});
		// How it hordes (HordeUpdate), with its kinds and bounding sphere.
		hordes.byDefinition.push_back(content::ReadObjectHorde(object, m_step.TicksPerSecond()));
		// Where a supply truck with nothing to do regroups (RegroupingState): a cash generator, else a command
		// centre, else any structure.
		harvest.homeRank.push_back(object.Is("CASH_GENERATOR") ? 0 : object.Is("COMMANDCENTER") ? 1 : object.Is("STRUCTURE") ? 2 : engine::gameplay::HarvestCatalog::NoHome);
		upgradeEffects.byDefinition.emplace_back();
		// Its SpyVisionUpdate modules.
		{
			std::vector<SpyVisionConfig> modules;
			for (const content::SpyVisionModule &module : content::ReadSpyVisions(object, m_step))
				modules.push_back({module.needsUpgrade, module.selfPowered, module.durationTicks, module.intervalTicks});
			spyVisions.byDefinition.resize(std::max<std::size_t>(spyVisions.byDefinition.size(), index + 1));
			spyVisions.byDefinition[index] = std::move(modules);
		}
		// Its MissileLauncherBuildingUpdate (a superweapon's launch door).
		{
			LauncherDoorConfig door;
			if (const auto launcher = content::ReadMissileLauncher(object, m_step))
			{
				door.present = true;
				door.power = m_content.powers.Template(launcher->power).value_or(0xFFFFFFFFu);
				door.openTicks = launcher->openTicks;
				door.waitOpenTicks = launcher->waitOpenTicks;
				door.closeTicks = launcher->closeTicks;
				const std::array<const std::string *, 5> names{&launcher->closedFX, &launcher->openingFX, &launcher->openFX, &launcher->waitingToCloseFX,
					&launcher->closingFX};
				for (std::size_t state = 0; state < names.size(); ++state)
					if (!names[state]->empty())
						door.effects[state] = DeathEffect(engine::gameplay::DeathEffectKind::Effect, *names[state]);
				door.openIdleAudio = launcher->openIdleAudio;
			}
			launcherDoors.resize(std::max<std::size_t>(launcherDoors.size(), index + 1));
			launcherDoors[index] = std::move(door);
		}
		// Its ParticleUplinkCannonUpdate.
		{
			ParticleCannonConfig cannon;
			if (const auto uplink = content::ReadParticleCannon(object, m_step))
			{
				cannon.present = true;
				cannon.power = m_content.powers.Template(uplink->power).value_or(0xFFFFFFFFu);
				cannon.beginChargeTicks = uplink->beginChargeTicks;
				cannon.raiseAntennaTicks = uplink->raiseAntennaTicks;
				cannon.readyDelayTicks = uplink->readyDelayTicks;
				cannon.widthGrowTicks = uplink->widthGrowTicks;
				cannon.beamTravelTicks = uplink->beamTravelTicks;
				cannon.totalFiringTicks = uplink->totalFiringTicks;
				cannon.launchFxTicks = uplink->launchFxTicks;
				cannon.doubleClickTicks = uplink->doubleClickTicks;
				cannon.swathDistance = uplink->swathDistance;
				cannon.swathAmplitude = uplink->swathAmplitude;
				cannon.totalScorchMarks = uplink->totalScorchMarks;
				cannon.scorchScalar = uplink->scorchScalar;
				cannon.damagePerSecond = uplink->damagePerSecond;
				cannon.totalPulses = uplink->totalPulses;
				cannon.damageType = content::DamageTypeIndex(uplink->damageType).value_or(0u);
				cannon.deathType = content::DeathTypeIndex(uplink->deathType).value_or(0u);
				cannon.damageRadiusScalar = uplink->damageRadiusScalar;
				cannon.drivingSpeed = uplink->drivingSpeed;
				cannon.fastDrivingSpeed = uplink->fastDrivingSpeed;
				if (!uplink->groundHitFX.empty())
					cannon.groundHitEffect = DeathEffect(engine::gameplay::DeathEffectKind::Effect, uplink->groundHitFX);
				if (!uplink->launchFX.empty())
					cannon.launchEffect = DeathEffect(engine::gameplay::DeathEffectKind::Effect, uplink->launchFX);
				// LaserUpdate::getTemplateLaserRadius: its beam's W3DLaserDraw OuterBeamWidth, halved (13 without one).
				if (const content::ObjectDefinition *beam = m_content.objects.Find(uplink->beamObject))
					for (const content::ModuleEntry &draw : beam->modules)
						if (draw.block != nullptr && draw.type == "W3DLaserDraw")
							if (const auto *width = draw.block->Find("OuterBeamWidth"))
								cannon.beamRadius = engine::config::values::ParseFixed(width->Value()).value_or(Engine::Math::Fixed::FromInt(26)) / Engine::Math::Fixed::FromInt(2);
				cannon.remnant = uplink->remnant;
			}
			particleCannons.resize(std::max<std::size_t>(particleCannons.size(), index + 1));
			particleCannons[index] = std::move(cannon);
		}
		// Its MobMemberSlavedUpdate.
		{
			MobMemberConfig mob;
			for (const content::ModuleEntry &module : object.modules)
				if (module.block != nullptr && module.type == "MobMemberSlavedUpdate")
				{
					const auto real = [&](std::string_view key) {
						const auto *node = module.block->Find(key);
						return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{}) : Engine::Math::Fixed{};
					};
					mob.present = true;
					mob.mustCatchUpRadius = real("MustCatchUpRadius");
					mob.noNeedToCatchUpRadius = real("NoNeedToCatchUpRadius");
					// onObjectCreated: MIN(MAX_SQUIRRELLINESS (1), MAX(0, ...)).
					mob.squirrelliness = std::clamp(real("Squirrelliness"), Engine::Math::Fixed{}, Engine::Math::Fixed::One());
					if (const auto *bail = module.block->Find("CatchUpCrisisBailTime"))
						mob.crisisBailTime = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::values::ParseInt(bail->Value()).value_or(0)));
					break;
				}
			mobMembers.resize(std::max<std::size_t>(mobMembers.size(), index + 1));
			mobMembers[index] = mob;
		}
		// Its HackInternetAIUpdate, and whether it is an InternetHackContain (an Internet Center).
		{
			InternetHackConfig hack;
			bool hackContainer = false;
			for (const content::ModuleEntry &module : object.modules)
			{
				if (module.block == nullptr)
					continue;
				if (module.type == "InternetHackContain")
					hackContainer = true;
				if (module.type != "HackInternetAIUpdate" || hack.present)
					continue;
				hack.present = true;
				engine::config::Diagnostics diagnostics;
				engine::config::BindContext bind{diagnostics, m_step};
				const auto ticks = [&](std::string_view key) -> std::uint32_t {
					const auto *node = module.block->Find(key);
					return node != nullptr && !node->values.empty() ? static_cast<std::uint32_t>(engine::config::ReadDurationTicks(*node, bind).value_or(0)) : 0u;
				};
				const auto number = [&](std::string_view key) -> std::uint32_t {
					const auto *node = module.block->Find(key);
					return node != nullptr && !node->values.empty()
						? static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::values::ParseInt(node->Value()).value_or(0)))
						: 0u;
				};
				hack.unpackTicks = ticks("UnpackTime");
				hack.packTicks = ticks("PackTime");
				hack.cashTicks = ticks("CashUpdateDelay");
				hack.cashTicksFast = ticks("CashUpdateDelayFast");
				hack.cash = {number("RegularCashAmount"), number("VeteranCashAmount"), number("EliteCashAmount"), number("HeroicCashAmount")};
				hack.xpPerCash = number("XpPerCashUpdate");
				if (const auto *node = module.block->Find("PackUnpackVariationFactor"); node != nullptr && !node->values.empty())
					hack.variation = engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{});
			}
			internetHacks.resize(std::max<std::size_t>(internetHacks.size(), index + 1));
			internetHacks[index] = hack;
			internetHackContainers.resize(std::max<std::size_t>(internetHackContainers.size(), index + 1));
			internetHackContainers[index] = hackContainer ? 1 : 0;
		}
		// Its WeaponBonusUpdate.
		{
			WeaponBonusPulseConfig pulse;
			for (const content::ModuleEntry &module : object.modules)
				if (module.block != nullptr && module.type == "WeaponBonusUpdate" && !pulse.present)
				{
					pulse.present = true;
					engine::config::Diagnostics diagnostics;
					engine::config::BindContext bind{diagnostics, m_step};
					const auto ticks = [&](std::string_view key) -> std::uint32_t {
						const auto *node = module.block->Find(key);
						return node != nullptr && !node->values.empty() ? static_cast<std::uint32_t>(engine::config::ReadDurationTicks(*node, bind).value_or(0)) : 0u;
					};
					const auto kinds = [&](std::string_view key) {
						content::KindOfMask mask{};
						if (const auto *node = module.block->Find(key))
							for (const std::string_view name : node->values)
								if (const std::size_t bit = content::KindOfBit(name); bit < content::KindOfNames.size())
									mask[bit / 64] |= std::uint64_t{1} << (bit % 64);
						return mask;
					};
					pulse.required = kinds("RequiredAffectKindOf");
					pulse.forbidden = kinds("ForbiddenAffectKindOf");
					pulse.durationTicks = ticks("BonusDuration");
					pulse.delayTicks = ticks("BonusDelay");
					if (const auto *node = module.block->Find("BonusRange"); node != nullptr && !node->values.empty())
						pulse.range = engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{});
					if (const auto *node = module.block->Find("BonusConditionType"); node != nullptr && !node->values.empty())
						for (std::size_t bit = 0; bit < content::weapon_bonus::Names.size(); ++bit)
							if (content::weapon_bonus::Names[bit] == node->Value())
								pulse.bit = 1u << bit;
				}
			weaponBonusPulses.resize(std::max<std::size_t>(weaponBonusPulses.size(), index + 1));
			weaponBonusPulses[index] = pulse;
		}
		// Its NeutronBlastBehavior (BlastRadius 10, AffectAirborne and AffectAllies Yes unless given), and whether a projectile
		// of its kind dies as it detonates (DumbProjectileBehavior DetonateCallsKill: its die modules then run).
		{
			NeutronBlastConfig blast;
			for (const content::ModuleEntry &module : object.modules)
			{
				if (module.block == nullptr)
					continue;
				const auto flag = [&](std::string_view key, bool fallback) {
					const auto *node = module.block->Find(key);
					return node != nullptr ? engine::config::values::ParseBool(node->Value()).value_or(fallback) : fallback;
				};
				if (module.type == "NeutronBlastBehavior" && !blast.present)
				{
					blast.present = true;
					if (const auto *node = module.block->Find("BlastRadius"))
						blast.radius = engine::config::values::ParseFixed(node->Value()).value_or(blast.radius);
					blast.affectAirborne = flag("AffectAirborne", true);
					blast.affectAllies = flag("AffectAllies", true);
				}
				else if (module.type == "DumbProjectileBehavior")
					blast.detonateCallsKill = flag("DetonateCallsKill", false);
			}
			neutronBlasts.resize(std::max<std::size_t>(neutronBlasts.size(), index + 1));
			neutronBlasts[index] = blast;
		}
		// Its EnemyNearUpdate: ScanDelayTime (a second unless given), in ticks.
		{
			std::optional<std::uint32_t> scan;
			for (const content::ModuleEntry &module : object.modules)
				if (module.block != nullptr && module.type == "EnemyNearUpdate" && !scan)
				{
					scan = static_cast<std::uint32_t>(m_step.TicksPerSecond());
					if (const auto *node = module.block->Find("ScanDelayTime"))
					{
						engine::config::Diagnostics diagnostics;
						engine::config::BindContext bind{diagnostics, m_step};
						scan = static_cast<std::uint32_t>(engine::config::ReadDurationTicks(*node, bind).value_or(*scan));
					}
				}
			enemyNears.resize(std::max<std::size_t>(enemyNears.size(), index + 1));
			enemyNears[index] = scan;
			// Its CheckpointUpdate (no fields of its own).
			checkpoints.resize(std::max<std::size_t>(checkpoints.size(), index + 1));
			checkpoints[index] = std::ranges::any_of(object.modules, [](const content::ModuleEntry &module) {
				return module.block != nullptr && module.type == "CheckpointUpdate";
			}) ? 1u : 0u;
		}
		// Its FirestormDynamicGeometryInfoUpdate (DelayBetweenDamageFrames: parseDurationReal, a real number of frames).
		{
			FirestormConfig storm;
			for (const content::ModuleEntry &module : object.modules)
				if (module.block != nullptr && module.type == "FirestormDynamicGeometryInfoUpdate" && !storm.present)
				{
					storm.present = true;
					const auto fixed = [&](std::string_view key, Engine::Math::Fixed fallback) {
						const auto *node = module.block->Find(key);
						return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(fallback) : fallback;
					};
					storm.damageDelay = fixed("DelayBetweenDamageFrames", {}) * Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(m_step.TicksPerSecond())) /
						Engine::Math::Fixed::FromInt(1000);
					storm.damage = fixed("DamageAmount", {});
					storm.maxHeight = fixed("MaxHeightForDamage", storm.maxHeight);
					storm.scorchSize = fixed("ScorchSize", {});
					storm.particleOffsetZ = fixed("ParticleOffsetZ", {});
					if (const auto *node = module.block->Find("FXList"); node != nullptr && node->Value() != "None")
						storm.fx = std::string(node->Value());
					for (int system = 1; system <= 16; ++system)
						if (const auto *node = module.block->Find("ParticleSystem" + std::to_string(system)); node != nullptr && node->Value() != "None")
							storm.particles.push_back(std::string(node->Value()));
				}
			firestorms.resize(std::max<std::size_t>(firestorms.size(), index + 1));
			firestorms[index] = std::move(storm);
		}
		// Its LeafletDropBehavior (DisabledDuration ticks, AffectRadius, LeafletFXParticleSystem).
		{
			LeafletDropConfig leaflet;
			for (const content::ModuleEntry &module : object.modules)
				if (module.block != nullptr && module.type == "LeafletDropBehavior" && !leaflet.present)
				{
					leaflet.present = true;
					engine::config::Diagnostics diagnostics;
					engine::config::BindContext bind{diagnostics, m_step};
					if (const auto *node = module.block->Find("DisabledDuration"))
						leaflet.durationTicks = engine::config::ReadDurationTicks(*node, bind).value_or(0);
					if (const auto *node = module.block->Find("AffectRadius"))
						leaflet.radius = engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{});
					if (const auto *node = module.block->Find("LeafletFXParticleSystem"); node != nullptr && node->Value() != "None")
						leaflet.particles = std::string(node->Value());
				}
			leafletDrops.resize(std::max<std::size_t>(leafletDrops.size(), index + 1));
			leafletDrops[index] = std::move(leaflet);
		}
		// Its BunkerBusterBehavior.
		{
			BunkerBusterConfig buster;
			for (const content::ModuleEntry &module : object.modules)
				if (module.block != nullptr && module.type == "BunkerBusterBehavior" && !buster.present)
				{
					buster.present = true;
					const auto text = [&](std::string_view key) {
						const auto *node = module.block->Find(key);
						return node != nullptr && !node->values.empty() && node->Value() != "None" ? std::string(node->Value()) : std::string{};
					};
					buster.upgrade = text("UpgradeRequired");
					buster.detonationFX = text("DetonationFX");
					buster.shockwaveWeapon = text("ShockwaveWeaponTemplate");
					buster.occupantWeapon = text("OccupantDamageWeaponTemplate");
				}
			bunkerBusters.resize(std::max<std::size_t>(bunkerBusters.size(), index + 1));
			bunkerBusters[index] = std::move(buster);
		}
		// Its OCLUpdate.
		{
			OclTimerConfig timer;
			for (const content::ModuleEntry &module : object.modules)
				if (module.block != nullptr && module.type == "OCLUpdate" && !timer.present)
				{
					timer.present = true;
					engine::config::Diagnostics diagnostics;
					engine::config::BindContext bind{diagnostics, m_step};
					for (const engine::config::Node &child : module.block->children)
					{
						const std::string_view key = child.key;
						const auto flag = [&] { return engine::config::values::ParseBool(child.Value()).value_or(false); };
						if (key == "OCL")
							timer.list = std::string(child.Value());
						else if (key == "MinDelay")
							timer.minDelay = engine::config::ReadDurationTicks(child, bind).value_or(0);
						else if (key == "MaxDelay")
							timer.maxDelay = engine::config::ReadDurationTicks(child, bind).value_or(0);
						else if (key == "CreateAtEdge")
							timer.atEdge = flag();
						else if (key == "FactionTriggered")
							timer.factionTriggered = flag();
						else if (key == "FactionOCL")
						{
							// parseFactionObjectCreationList: "Faction:<side> OCL:<list>".
							std::string side, list;
							for (const std::string_view token : child.values)
							{
								if (token.starts_with("Faction:"))
									side = std::string(token.substr(8));
								else if (token.starts_with("OCL:"))
									list = std::string(token.substr(4));
							}
							timer.factionLists.emplace_back(std::move(side), std::move(list));
						}
					}
				}
			oclTimers.resize(std::max<std::size_t>(oclTimers.size(), index + 1));
			oclTimers[index] = std::move(timer);
		}
		// Its BattleBusSlowDeathBehavior, and which of its slow deaths (in the death content's order) it is.
		{
			BattleBusConfig bus;
			std::uint32_t slow = 0;
			for (const content::ModuleEntry &module : object.modules)
			{
				if (module.slot != content::ModuleSlot::Behavior || module.block == nullptr || !std::string_view(module.type).ends_with("SlowDeathBehavior"))
					continue;
				if (module.type == "BattleBusSlowDeathBehavior" && !bus.present)
				{
					bus.present = true;
					bus.slowIndex = slow;
					engine::config::Diagnostics diagnostics;
					engine::config::BindContext bind{diagnostics, m_step};
					const auto text = [&](std::string_view key) {
						const auto *node = module.block->Find(key);
						return node != nullptr && !node->values.empty() ? std::string(node->Value()) : std::string{};
					};
					bus.fxStart = text("FXStartUndeath");
					bus.oclStart = text("OCLStartUndeath");
					bus.fxHitGround = text("FXHitGround");
					bus.oclHitGround = text("OCLHitGround");
					if (const auto *node = module.block->Find("ThrowForce"); node != nullptr && !node->values.empty())
						bus.throwForce = engine::config::values::ParseFixed(node->Value()).value_or(bus.throwForce);
					if (const auto *node = module.block->Find("PercentDamageToPassengers"); node != nullptr && !node->values.empty())
						bus.passengerDamage = engine::config::ReadPercent(*node, bind).value_or(Engine::Math::Fixed{});
					if (const auto *node = module.block->Find("EmptyHulkDestructionDelay"); node != nullptr && !node->values.empty())
						bus.hulkDelayTicks = static_cast<std::uint32_t>(engine::config::ReadDurationTicks(*node, bind).value_or(0));
				}
				++slow;
			}
			battleBuses.resize(std::max<std::size_t>(battleBuses.size(), index + 1));
			battleBuses[index] = std::move(bus);
		}
		// Its SupplyWarehouseCripplingBehavior.
		{
			WarehouseCripplingConfig crippling;
			for (const content::ModuleEntry &module : object.modules)
				if (module.block != nullptr && module.type == "SupplyWarehouseCripplingBehavior" && !crippling.present)
				{
					crippling.present = true;
					engine::config::Diagnostics diagnostics;
					engine::config::BindContext bind{diagnostics, m_step};
					const auto ticks = [&](std::string_view key) -> std::uint32_t {
						const auto *node = module.block->Find(key);
						return node != nullptr && !node->values.empty() ? static_cast<std::uint32_t>(engine::config::ReadDurationTicks(*node, bind).value_or(0)) : 0u;
					};
					crippling.suppressionTicks = ticks("SelfHealSupression");
					crippling.delayTicks = ticks("SelfHealDelay");
					if (const auto *node = module.block->Find("SelfHealAmount"); node != nullptr && !node->values.empty())
						crippling.amount = engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{});
				}
			warehouseCripplings.resize(std::max<std::size_t>(warehouseCripplings.size(), index + 1));
			warehouseCripplings[index] = crippling;
		}
		// Its BattlePlanUpdate (a Strategy Center).
		{
			BattlePlanConfig plan;
			for (const content::ModuleEntry &module : object.modules)
			{
				if (module.block == nullptr || module.type != "BattlePlanUpdate" || plan.present)
					continue;
				plan.present = true;
				engine::config::Diagnostics diagnostics;
				engine::config::BindContext bind{diagnostics, m_step};
				const auto text = [&](std::string_view key) -> std::string_view {
					const auto *node = module.block->Find(key);
					return node != nullptr && !node->values.empty() ? node->Value() : std::string_view{};
				};
				const auto ticks = [&](std::string_view key) -> std::uint32_t {
					const auto *node = module.block->Find(key);
					return node != nullptr && !node->values.empty() ? static_cast<std::uint32_t>(engine::config::ReadDurationTicks(*node, bind).value_or(0)) : 0u;
				};
				const auto real = [&](std::string_view key, Engine::Math::Fixed fallback) {
					const std::string_view value = text(key);
					return value.empty() ? fallback : engine::config::values::ParseFixed(value).value_or(fallback);
				};
				const auto kinds = [&](std::string_view key) {
					content::KindOfMask mask{};
					if (const auto *node = module.block->Find(key))
						for (const std::string_view name : node->values)
							if (const std::size_t bit = content::KindOfBit(name); bit < content::KindOfNames.size())
								mask[bit / 64] |= std::uint64_t{1} << (bit % 64);
					return mask;
				};
				plan.power = m_content.powers.Template(std::string(text("SpecialPowerTemplate"))).value_or(BattlePlanConfig::NoPower);
				plan.animationTicks = {ticks("BombardmentPlanAnimationTime"), ticks("HoldTheLinePlanAnimationTime"), ticks("SearchAndDestroyPlanAnimationTime")};
				plan.transitionIdleTicks = ticks("TransitionIdleTime");
				plan.paralyzeTicks = ticks("BattlePlanChangeParalyzeTime");
				plan.valid = kinds("ValidMemberKindOf");
				plan.invalid = kinds("InvalidMemberKindOf");
				const Engine::Math::Fixed one = Engine::Math::Fixed::One();
				plan.holdTheLineArmorScalar = real("HoldTheLinePlanArmorDamageScalar", one);
				plan.searchAndDestroySightScalar = real("SearchAndDestroyPlanSightRangeScalar", one);
				plan.centerSightScalar = real("StrategyCenterSearchAndDestroySightRangeScalar", one);
				plan.centerMaxHealthScalar = real("StrategyCenterHoldTheLineMaxHealthScalar", one);
				if (const std::string_view detects = text("StrategyCenterSearchAndDestroyDetectsStealth"); !detects.empty())
					plan.centerDetectsStealth = detects == "Yes" || detects == "yes" || detects == "YES";
				constexpr std::array<std::string_view, 4> changes{"SAME_CURRENTHEALTH", "PRESERVE_RATIO", "ADD_CURRENT_HEALTH_TOO", "FULLY_HEAL"};
				for (std::size_t at = 0; at < changes.size(); ++at)
					if (text("StrategyCenterHoldTheLineMaxHealthChangeType") == changes[at])
						plan.centerMaxHealthChange = static_cast<engine::gameplay::MaxHealthChange>(at);
			}
			battlePlans.resize(std::max<std::size_t>(battlePlans.size(), index + 1));
			battlePlans[index] = plan;
		}
		// Its StickyBombUpdate.
		{
			StickyBombConfig bomb;
			for (const content::ModuleEntry &module : object.modules)
				if (module.block != nullptr && module.type == "StickyBombUpdate")
				{
					bomb.present = true;
					if (const auto *node = module.block->Find("OffsetZ"); node != nullptr && !node->values.empty())
						bomb.offsetZ = engine::config::values::ParseFixed(node->Value()).value_or(bomb.offsetZ);
					if (const auto *node = module.block->Find("GeometryBasedDamageWeapon"); node != nullptr && !node->values.empty())
						bomb.weapon = Weapon(std::string(node->Value()));
					if (const auto *node = module.block->Find("GeometryBasedDamageFX"); node != nullptr && !node->values.empty())
						bomb.effect = DeathEffect(engine::gameplay::DeathEffectKind::Effect, node->Value());
					break;
				}
			stickyBombs.resize(std::max<std::size_t>(stickyBombs.size(), index + 1));
			stickyBombs[index] = bomb;
		}
		// Its SpectreGunshipUpdate and SpectreGunshipDeploymentUpdate.
		{
			SpectreGunshipConfig gunship;
			std::vector<SpectreDeploymentConfig> deployments;
			for (const content::ModuleEntry &module : object.modules)
			{
				if (module.block == nullptr)
					continue;
				const auto real = [&](std::string_view key) {
					const auto *node = module.block->Find(key);
					return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{}) : Engine::Math::Fixed{};
				};
				const auto name = [&](std::string_view key) {
					const auto *node = module.block->Find(key);
					return node != nullptr && !node->values.empty() ? std::string(node->Value()) : std::string{};
				};
				// parseDurationUnsignedInt: ms up to whole ticks.
				const auto ticks = [&](std::string_view key) -> std::uint64_t {
					const std::int64_t ms = real(key).Ceil();
					return ms <= 0 ? 0 : static_cast<std::uint64_t>((ms * static_cast<std::int64_t>(m_step.TicksPerSecond()) + 999) / 1000);
				};
				if (module.type == "SpectreGunshipUpdate" && !gunship.present)
				{
					gunship.present = true;
					gunship.power = m_content.powers.Template(name("SpecialPowerTemplate")).value_or(0xFFFFFFFFu);
					gunship.gattling = name("GattlingTemplateName");
					if (const std::string howitzer = name("HowitzerWeaponTemplate"); !howitzer.empty())
						gunship.howitzer = Weapon(howitzer);
					gunship.howitzerFiringTicks = ticks("HowitzerFiringRate");
					gunship.orbitTicks = ticks("OrbitTime");
					gunship.howitzerFollowLag = ticks("HowitzerFollowLag");
					gunship.attackAreaRadius = real("AttackAreaRadius");
					gunship.strafingIncrement = real("StrafingIncrement");
					gunship.orbitInsertionSlope = real("OrbitInsertionSlope");
					gunship.randomOffset = real("RandomOffsetForHowitzer");
					gunship.reticleRadius = real("TargetingReticleRadius");
					gunship.orbitRadius = real("GunshipOrbitRadius");
					if (const auto *decal = module.block->Find("AttackAreaDecal"))
						gunship.attackAreaDecal = content::ReadRadiusDecal(*decal, m_step.TicksPerSecond());
					if (const auto *decal = module.block->Find("TargetingReticleDecal"))
						gunship.reticleDecal = content::ReadRadiusDecal(*decal, m_step.TicksPerSecond());
				}
				else if (module.type == "SpectreGunshipDeploymentUpdate")
				{
					SpectreDeploymentConfig &deployment = deployments.emplace_back();
					deployment.present = true;
					if (const std::string science = name("RequiredScience"); !science.empty() && science != "SCIENCE_INVALID")
						deployment.requiredScience = m_content.Science(science).value_or(0xFFFFFFFFu);
					deployment.power = m_content.powers.Template(name("SpecialPowerTemplate")).value_or(0xFFFFFFFFu);
					deployment.gunship = name("GunshipTemplateName");
					const std::string entry = name("CreateLocation");
					deployment.entry = entry == "CREATE_AT_EDGE_NEAR_SOURCE" ? GunshipEntry::EdgeNearSource
						: entry == "CREATE_AT_EDGE_FARTHEST_FROM_SOURCE" ? GunshipEntry::EdgeFarthestFromSource
						: entry == "CREATE_AT_EDGE_NEAR_TARGET" ? GunshipEntry::EdgeNearTarget : GunshipEntry::EdgeFarthestFromTarget;
				}
			}
			spectreGunships.resize(std::max<std::size_t>(spectreGunships.size(), index + 1));
			spectreGunships[index] = std::move(gunship);
			spectreDeployments.resize(std::max<std::size_t>(spectreDeployments.size(), index + 1));
			spectreDeployments[index] = std::move(deployments);
		}
		// Its RailroadBehavior.
		if (const auto railroad = content::ReadRailroad(object, m_step))
		{
			RailroadConfig config{true, railroad->locomotive, railroad->carriages,
				!railroad->carriages.empty() && m_content.objects.Find(railroad->carriages.front()) != nullptr, object.geometry.majorRadius, railroad->runningGarrisonSpeedMax,
				railroad->killSpeedMin, railroad->speedMax, railroad->acceleration, railroad->braking, railroad->friction,
				static_cast<std::int32_t>(railroad->waitAtStationTicks), railroad->runningSound, railroad->clicketyClackSound, railroad->whistleSound,
				railroad->bigMetalBounceSound, railroad->smallMetalBounceSound, railroad->meatyBounceSound};
			railroads.resize(std::max<std::size_t>(railroads.size(), index + 1));
			railroads[index] = std::move(config);
		}
		// Its railed transport, with its dock's bones.
		if (auto railed = content::ReadRailedTransport(object, m_step))
		{
			RailedTransportConfig config;
			config.content = std::move(*railed);
			if (const auto layout = m_content.docks.find(object.name); layout != m_content.docks.end())
			{
				for (const content::RestBone &bone : layout->second.approach)
					config.approach.push_back(bone.position);
				if (layout->second.enter)
				{
					config.enter = layout->second.enter->position;
					config.action = layout->second.action.position;
					config.exit = layout->second.exit.position;
				}
			}
			railedTransports.resize(std::max<std::size_t>(railedTransports.size(), index + 1));
			railedTransports[index] = std::move(config);
		}
		// Its FlightDeckBehavior.
		if (auto deck = content::ReadFlightDeck(object, m_step))
		{
			flightDecks.resize(std::max<std::size_t>(flightDecks.size(), index + 1));
			flightDecks[index] = std::move(*deck);
		}
		// Its ChinookAIUpdate's combat drop, with its model's rope bones.
		if (auto drop = content::ReadCombatDrop(object, m_step))
		{
			CombatDropConfig config;
			config.content = std::move(*drop);
			if (const auto bones = m_content.ropeBones.find(object.name); bones != m_content.ropeBones.end())
			{
				config.ropeStarts = bones->second.starts;
				config.ropeEnds = bones->second.ends;
			}
			config.present = true;
			combatDrops.resize(std::max<std::size_t>(combatDrops.size(), index + 1));
			combatDrops[index] = std::move(config);
		}
		// Its ConvertToCarBombCrateCollide.
		{
			std::optional<content::CrateCollideContent> carBomb;
			if (auto collide = content::ReadCrateCollide(object); collide && collide->kind == content::CrateKind::CarBomb)
				carBomb = std::move(*collide);
			carBombs.resize(std::max<std::size_t>(carBombs.size(), index + 1));
			carBombs[index] = std::move(carBomb);
		}
		// Its Sabotage*CrateCollide modules (the Saboteur's), in module order.
		sabotages.resize(std::max<std::size_t>(sabotages.size(), index + 1));
		sabotages[index] = content::ReadSabotageCollides(object);
		// Its ConvertToHijackedVehicleCrateCollide and HijackerUpdate's ParachuteName.
		{
			std::optional<content::CrateCollideContent> hijack;
			if (auto collide = content::ReadCrateCollide(object); collide && collide->kind == content::CrateKind::Hijack)
				hijack = std::move(*collide);
			std::string parachute;
			for (const content::ModuleEntry &module : object.modules)
				if (module.block != nullptr && module.type == "HijackerUpdate")
					if (const auto *node = module.block->Find("ParachuteName"); node != nullptr && !node->values.empty())
						parachute = std::string(node->Value());
			hijacks.resize(std::max<std::size_t>(hijacks.size(), index + 1));
			hijacks[index] = std::move(hijack);
			hijackerParachutes.resize(std::max<std::size_t>(hijackerParachutes.size(), index + 1));
			hijackerParachutes[index] = std::move(parachute);
		}
		// Its FireOCLAfterWeaponCooldownUpdate modules.
		{
			std::vector<CooldownCreationConfig> modules;
			for (const content::CooldownCreationModule &module : content::ReadCooldownCreations(object, m_content.upgrades, m_step.TicksPerSecond()))
				if (modules.size() < CooldownCreationCatalog::MaxModules)
					modules.push_back({module.slot, module.creation.empty() ? 0xFFFFFFFFu : DeathEffect(engine::gameplay::DeathEffectKind::Objects, module.creation),
						module.minShots, module.lifetimePerSecond, module.maxTicks, module.activation, module.conflicting, module.requiresAll});
			cooldownCreations.byDefinition.resize(std::max<std::size_t>(cooldownCreations.byDefinition.size(), index + 1));
			cooldownCreations.byDefinition[index] = std::move(modules);
		}
		// Its weapon and armor sets by their conditions (WeaponSetFlags, ArmorSetFlags).
		{
			engine::gameplay::DefinitionLoadout loadout;
			const content::ObjectLoadout sets = content::ReadObjectLoadout(object);
			for (const content::WeaponSetContent &set : sets.weaponSets)
				loadout.weaponSets.push_back({set.conditions, {Weapon(set.weapons[0]), Weapon(set.weapons[1]), Weapon(set.weapons[2])}, set.lockShared, set.rules});
			for (const content::ArmorSetContent &set : sets.armorSets)
				loadout.armorSets.push_back({set.conditions, Armor(set.armor)});
			loadouts.byDefinition.resize(std::max<std::size_t>(loadouts.byDefinition.size(), index + 1));
			loadouts.byDefinition[index] = std::move(loadout);
		}
		// Its upgrade triggers and what they do (armors and weapons by index), in module order.
		std::vector<engine::gameplay::UpgradeTrigger> triggers;
		std::vector<UpgradeEffect> effects;
		for (const content::ObjectUpgradeContent &entry : content::ReadObjectUpgrades(object, m_content.upgrades))
		{
			engine::gameplay::UpgradeTrigger trigger = entry.trigger;
			trigger.reaction = static_cast<std::uint32_t>(entry.effect.kind);
			triggers.push_back(trigger);
			UpgradeEffect effect;
			effect.kind = entry.effect.kind;
			effect.amount = entry.effect.amount;
			effect.change = entry.effect.change;
			if (!entry.effect.armor.empty())
				effect.armor = Armor(entry.effect.armor);
			if (!entry.effect.creation.empty())
				effect.creation = DeathEffect(engine::gameplay::DeathEffectKind::Objects, entry.effect.creation);
			if (!entry.effect.commandSet.empty())
				effect.commandSet = CommandSet(entry.effect.commandSet);
			if (!entry.effect.commandSetAlt.empty())
				effect.commandSetAlt = CommandSet(entry.effect.commandSetAlt);
			if (entry.effect.triggerAlt)
				effect.triggerAlt = *entry.effect.triggerAlt;
			effect.disableProof = entry.effect.disableProof;
			effect.parts = entry.effect.ordinal;
			effect.conflicting = entry.trigger.conflicting;
			effect.kinds = entry.effect.kinds;
			effect.share = entry.effect.share;
			if (!entry.effect.replacement.empty())
				effect.replacement = DeathEffect(engine::gameplay::DeathEffectKind::Spawn, entry.effect.replacement);
			if (!entry.effect.science.empty())
				if (const auto science = m_content.Science(entry.effect.science))
					effect.science = *science;
			if (entry.effect.kind == content::UpgradeEffectKind::LocomotorSet)
				if (const auto *upgraded = content::ObjectLocomotor(object, m_content.locomotors, "SET_NORMAL_UPGRADED"))
					effect.locomotor = *upgraded;
			if (!entry.effect.condition.empty())
				if (const std::uint32_t bit = content::ModelConditionBit(entry.effect.condition); bit != content::NoCondition)
					effect.condition = bit;
			if (const auto power = m_content.powers.Template(entry.effect.power); power && !entry.effect.power.empty())
				effect.power = *power;
			for (std::size_t slot = 0; slot < 3; ++slot)
				if (!entry.effect.weapons[slot].empty())
				{
					effect.weapons[slot] = Weapon(entry.effect.weapons[slot]);
					effect.hasWeapons = true;
				}
			effects.push_back(effect);
		}
		upgradeTriggers.byDefinition[index] = std::move(triggers);
		upgradeEffects.byDefinition[index] = std::move(effects);
		return index;
	}

	const content::ObjectDefinition &DefinitionAt(std::uint32_t index) const { return *m_definitions.at(index); }
	// The catalog's entry of a definition's name (content objects.Find(DefinitionAt(index).name)), found when it was
	// added: the scripts' type tests ask it for every object they count.
	const content::ObjectDefinition *CatalogEntryOf(std::uint32_t index) const { return m_catalogEntries.at(index); }
	// The death (index into `deaths`) of a definition.
	std::uint32_t DeathOf(std::uint32_t definition) const { return m_deathOf.at(definition); }
	// Whether a definition has an AI module (its dead enter the dying state).
	bool HasAI(std::uint32_t definition) const noexcept { return definition < m_hasAI.size() && m_hasAI[definition]; }

	// The parachute a definition is (an index into `parachutes`), NoParachute when none.
	static constexpr std::uint32_t NoParachute = 0xFFFFFFFFu;
	std::uint32_t ParachuteOf(std::uint32_t definition) const noexcept { return definition < m_parachuteOf.size() ? m_parachuteOf[definition] : NoParachute; }

	// A death effect's id by name, per kind; and back.
	std::uint32_t DeathEffect(engine::gameplay::DeathEffectKind kind, std::string_view name)
	{
		auto &index = m_deathEffectIndex[static_cast<std::size_t>(kind)];
		if (const auto found = index.find(name); found != index.end())
			return found->second;
		auto &names = m_deathEffectNames[static_cast<std::size_t>(kind)];
		names.emplace_back(name);
		return index.emplace(std::string(name), static_cast<std::uint32_t>(names.size() - 1)).first->second;
	}
	// A death effect first named as a creation list plays (a debris piece's FXFinal or BounceSound), kept for checkpoints.
	std::uint32_t PlayedEffect(engine::gameplay::DeathEffectKind kind, std::string_view name)
	{
		const auto &index = m_deathEffectIndex[static_cast<std::size_t>(kind)];
		if (const auto found = index.find(name); found != index.end())
			return found->second;
		m_history.emplace_back(Kind::Effect, std::string(1, static_cast<char>('0' + static_cast<std::uint8_t>(kind))) + std::string(name));
		return DeathEffect(kind, name);
	}
	// A command set's id by name (upgrades swap them in, hunts keep their button by it), and back. A new one is part of
	// the checkpoint's history.
	std::uint32_t CommandSet(std::string_view name)
	{
		for (std::size_t index = 0; index < m_commandSetNames.size(); ++index)
			if (m_commandSetNames[index] == name)
				return static_cast<std::uint32_t>(index);
		m_commandSetNames.emplace_back(name);
		m_history.emplace_back(Kind::CommandSet, std::string(name));
		return static_cast<std::uint32_t>(m_commandSetNames.size() - 1);
	}
	const engine::time::FixedStep &Step() const noexcept { return m_step; }
	std::string_view CommandSetName(std::uint32_t id) const noexcept { return id < m_commandSetNames.size() ? std::string_view(m_commandSetNames[id]) : std::string_view{}; }

	// A model shown instead of a definition's (debris pieces), by name; ids from 1.
	std::uint32_t Model(std::string_view name)
	{
		if (const auto found = m_modelIndex.find(name); found != m_modelIndex.end())
			return found->second;
		m_history.emplace_back(Kind::Model, std::string(name));
		m_modelNames.emplace_back(name);
		return m_modelIndex.emplace(std::string(name), static_cast<std::uint32_t>(m_modelNames.size())).first->second;
	}
	std::string_view ModelName(std::uint32_t id) const noexcept
	{
		return id != 0 && id <= m_modelNames.size() ? std::string_view(m_modelNames[id - 1]) : std::string_view{};
	}

	std::string_view DeathEffectName(engine::gameplay::DeathEffectKind kind, std::uint32_t id) const
	{
		const auto &names = m_deathEffectNames[static_cast<std::size_t>(kind)];
		return id < names.size() ? std::string_view(names[id]) : std::string_view{};
	}
	std::size_t DefinitionCount() const noexcept { return m_definitions.size(); }

	std::uint32_t Weapon(const std::string &name)
	{
		if (name.empty())
			return engine::gameplay::WeaponCatalog::None;
		if (const auto found = m_weaponIndex.find(name); found != m_weaponIndex.end())
			return found->second;
		m_history.emplace_back(Kind::Weapon, name);
		// "weapon|object": a DeliverPayload run's visible payload item flying for its VisiblePayloadWeaponTemplate
		// (MissileAIUpdate::projectileFireAtObjectOrPosition with that weapon): the weapon with that object as its projectile.
		const std::size_t bar = name.find('|');
		const content::WeaponContent *weapon = m_content.weapons.Find(bar == std::string::npos ? name : name.substr(0, bar));
		if (weapon == nullptr)
			return m_weaponIndex.emplace(name, engine::gameplay::WeaponCatalog::None).first->second;
		const std::string projectileObject = bar == std::string::npos ? weapon->projectileObject : name.substr(bar + 1);
		engine::gameplay::WeaponDefinition simulation = weapon->simulation;
		if (bar != std::string::npos)
			simulation.projectile = true;
		if (simulation.projectile)
			if (const content::ObjectDefinition *object = m_content.objects.Find(projectileObject); object != nullptr && object->geometry.majorRadius > Engine::Math::Fixed{})
				simulation.projectileRadius = object->geometry.majorRadius;
		// A projectile object that lobs (DumbProjectileBehavior) flies as an object along its arc.
		// A projectile object that is a guided missile (MissileAIUpdate) flies as one.
		if (simulation.projectile)
			if (const auto missile = m_content.missiles.find(projectileObject); missile != m_content.missiles.end())
				if (const content::ObjectDefinition *object = m_content.objects.Find(projectileObject))
				{
					simulation.guided = true;
					simulation.smallMissile = object->Is("SMALL_MISSILE");
					simulation.missile = missile->second;
					simulation.projectileDefinition = Definition(*object);
				}
		// A projectile object that flies itself (NeutronMissileUpdate) is the game's to make and fly.
		if (simulation.projectile && !simulation.guided)
			if (const content::ObjectDefinition *object = m_content.objects.Find(projectileObject))
				if (const auto neutron = content::ReadNeutronMissile(*object, m_step))
				{
					simulation.objectFlown = true;
					simulation.neutron = neutron->flight;
					if (!neutron->launchFX.empty())
						simulation.neutron.launchEffect = DeathEffect(engine::gameplay::DeathEffectKind::Effect, neutron->launchFX);
					if (!neutron->ignitionFX.empty())
						simulation.neutron.ignitionEffect = DeathEffect(engine::gameplay::DeathEffectKind::Effect, neutron->ignitionFX);
					simulation.projectileDefinition = Definition(*object);
				}
		if (simulation.projectile && !simulation.guided && !simulation.objectFlown)
			if (const auto arc = m_content.projectileArcs.find(projectileObject); arc != m_content.projectileArcs.end())
				if (const content::ObjectDefinition *object = m_content.objects.Find(projectileObject))
				{
					simulation.lobbed = true;
					simulation.arc = arc->second;
					simulation.projectileDefinition = Definition(*object);
				}
		// Its HistoricBonusWeapon (none: itself, as the original never fires its own; a weapon being made already: none).
		if (!weapon->historicBonusWeapon.empty() && weapon->historicBonusWeapon != name && !m_historicResolving.contains(name))
		{
			m_historicResolving.insert(name);
			simulation.historicBonusWeapon = Weapon(weapon->historicBonusWeapon);
			m_historicResolving.erase(name);
		}
		if (weapon->extraBonus)
		{
			simulation.extraBonus = static_cast<std::uint32_t>(weapons.extraBonuses.size());
			weapons.extraBonuses.push_back(*weapon->extraBonus);
		}
		if (!weapon->scatterTargets.empty())
		{
			simulation.scatterFirst = static_cast<std::uint32_t>(weapons.scatterTargets.size());
			simulation.scatterCount = static_cast<std::uint32_t>(weapon->scatterTargets.size());
			weapons.scatterTargets.insert(weapons.scatterTargets.end(), weapon->scatterTargets.begin(), weapon->scatterTargets.end());
		}
		const std::uint32_t index = weapons.Add(simulation);
		m_weaponContent.push_back(weapon);
		return m_weaponIndex.emplace(name, index).first->second;
	}

	std::uint32_t WeaponCount() const noexcept { return static_cast<std::uint32_t>(m_weaponContent.size()); }

	const content::WeaponContent *WeaponContentAt(std::uint32_t index) const
	{
		return index < m_weaponContent.size() ? m_weaponContent[index] : nullptr;
	}

	std::uint32_t Armor(const std::string &name)
	{
		if (name.empty())
			return 0;
		if (const auto found = m_armorIndex.find(name); found != m_armorIndex.end())
			return found->second;
		m_history.emplace_back(Kind::Armor, name);
		const engine::gameplay::ArmorDefinition *armor = m_content.armors.Find(name);
		return m_armorIndex.emplace(name, armor != nullptr ? armors.Add(*armor) : 0u).first->second;
	}

	// An armor's name (none for plain armor).
	std::string_view ArmorName(std::uint32_t index) const noexcept
	{
		if (index == 0)
			return {};
		for (const auto &[name, armor] : m_armorIndex)
			if (armor == index)
				return name;
		return {};
	}

	const content::GameContent &Content() const noexcept { return m_content; }

	// Checkpoints: the lookups that added entries, in order; replaying them
	// on the same content rebuilds identical indices and catalogs.
	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_history.size()));
		for (const auto &[kind, name] : m_history)
		{
			writer.U8(static_cast<std::uint8_t>(kind));
			writer.Text(name);
		}
	}

	// Into templates that have added nothing yet.
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		if (!m_history.empty())
			return false;
		const auto count = reader.U32();
		for (std::uint32_t index = 0; count && index < *count; ++index)
		{
			const auto kind = reader.U8();
			const auto name = reader.Text();
			if (!kind || !name)
				return false;
			switch (static_cast<Kind>(*kind))
			{
			case Kind::Definition:
			{
				const content::ObjectDefinition *object = m_content.objects.Find(*name);
				if (object == nullptr)
					return false;
				Definition(*object);
				break;
			}
			case Kind::Weapon: Weapon(*name); break;
			case Kind::CommandSet: CommandSet(*name); break;
			case Kind::Armor: Armor(*name); break;
			case Kind::Model: Model(*name); break;
			case Kind::Effect:
				if (name->empty() || static_cast<std::uint8_t>(name->front() - '0') >= engine::gameplay::DeathEffectKinds)
					return false;
				PlayedEffect(static_cast<engine::gameplay::DeathEffectKind>(name->front() - '0'), std::string_view(*name).substr(1));
				break;
			default: return false;
			}
		}
		return count.has_value();
	}

	engine::gameplay::WeaponCatalog &weapons;
	engine::gameplay::ArmorCatalog &armors;
	engine::gameplay::DeathCatalog &deaths;
	engine::gameplay::LaunchLayouts &launches;
	engine::gameplay::UpgradeTriggers &upgradeTriggers;
	std::vector<std::string> m_commandSetNames; // command sets upgrades swap in, by id
	UpgradeEffects &upgradeEffects;
	SpyVisionCatalog spyVisions; // each definition's SpyVisionUpdate modules
	CooldownCreationCatalog cooldownCreations; // each definition's FireOCLAfterWeaponCooldownUpdate modules
	std::vector<LauncherDoorConfig> launcherDoors; // each definition's MissileLauncherBuildingUpdate (present or not)
	std::vector<ParticleCannonConfig> particleCannons; // each definition's ParticleUplinkCannonUpdate (present or not)
	std::vector<MobMemberConfig> mobMembers;           // each definition's MobMemberSlavedUpdate (present or not)
	std::vector<StickyBombConfig> stickyBombs;         // each definition's StickyBombUpdate (present or not)
	std::vector<InternetHackConfig> internetHacks;     // each definition's HackInternetAIUpdate (present or not)
	std::vector<BattlePlanConfig> battlePlans;         // each definition's BattlePlanUpdate (present or not)
	std::vector<WarehouseCripplingConfig> warehouseCripplings; // each definition's SupplyWarehouseCripplingBehavior
	std::vector<BattleBusConfig> battleBuses;                  // each definition's BattleBusSlowDeathBehavior
	std::vector<WeaponBonusPulseConfig> weaponBonusPulses;     // each definition's WeaponBonusUpdate
	std::vector<OclTimerConfig> oclTimers;                     // each definition's OCLUpdate
	std::vector<NeutronBlastConfig> neutronBlasts;             // each definition's NeutronBlastBehavior
	std::vector<BunkerBusterConfig> bunkerBusters;             // each definition's BunkerBusterBehavior
	std::vector<LeafletDropConfig> leafletDrops;               // each definition's LeafletDropBehavior
	std::vector<FirestormConfig> firestorms;                   // each definition's FirestormDynamicGeometryInfoUpdate
	const FirestormConfig *FirestormOf(std::uint32_t definition) const noexcept
	{
		return definition < firestorms.size() && firestorms[definition].present ? &firestorms[definition] : nullptr;
	}
	const LeafletDropConfig *LeafletDropOf(std::uint32_t definition) const noexcept
	{
		return definition < leafletDrops.size() && leafletDrops[definition].present ? &leafletDrops[definition] : nullptr;
	}
	std::vector<std::optional<std::uint32_t>> enemyNears;       // each definition's EnemyNearUpdate ScanDelayTime (ticks)
	std::vector<std::uint8_t> checkpoints;                      // each definition's CheckpointUpdate (1: has one)
	bool CheckpointOf(std::uint32_t definition) const noexcept { return definition < checkpoints.size() && checkpoints[definition] != 0; }
	const std::uint32_t *EnemyNearOf(std::uint32_t definition) const noexcept
	{
		return definition < enemyNears.size() && enemyNears[definition] ? &*enemyNears[definition] : nullptr;
	}
	const BunkerBusterConfig *BunkerBusterOf(std::uint32_t definition) const noexcept
	{
		return definition < bunkerBusters.size() && bunkerBusters[definition].present ? &bunkerBusters[definition] : nullptr;
	}
	const NeutronBlastConfig *NeutronBlastOf(std::uint32_t definition) const noexcept
	{
		return definition < neutronBlasts.size() && neutronBlasts[definition].present ? &neutronBlasts[definition] : nullptr;
	}
	// Its SpawnPointProductionExitUpdate's places (its SpawnPoint bones); none: it has no such exit.
	// Its BoneFXUpdate (none: nullptr).
	const content::BoneFxContent *BoneFxOf(std::uint32_t definition) const noexcept
	{
		return definition < m_boneFxOf.size() ? m_boneFxOf[definition] : nullptr;
	}
	const std::vector<content::RestBone> *SpawnPointsOf(std::uint32_t definition) const noexcept
	{
		return definition < m_spawnPointsOf.size() ? m_spawnPointsOf[definition] : nullptr;
	}
	const OclTimerConfig *OclTimerOf(std::uint32_t definition) const noexcept
	{
		return definition < oclTimers.size() && oclTimers[definition].present ? &oclTimers[definition] : nullptr;
	}
	const WeaponBonusPulseConfig *WeaponBonusPulseOf(std::uint32_t definition) const noexcept
	{
		return definition < weaponBonusPulses.size() && weaponBonusPulses[definition].present ? &weaponBonusPulses[definition] : nullptr;
	}
	const BattleBusConfig *BattleBusOf(std::uint32_t definition) const noexcept
	{
		return definition < battleBuses.size() && battleBuses[definition].present ? &battleBuses[definition] : nullptr;
	}
	const WarehouseCripplingConfig *WarehouseCripplingOf(std::uint32_t definition) const noexcept
	{
		return definition < warehouseCripplings.size() && warehouseCripplings[definition].present ? &warehouseCripplings[definition] : nullptr;
	}
	const BattlePlanConfig *BattlePlanOf(std::uint32_t definition) const noexcept
	{
		return definition < battlePlans.size() && battlePlans[definition].present ? &battlePlans[definition] : nullptr;
	}
	std::vector<std::uint8_t> internetHackContainers;  // each definition: an InternetHackContain
	const InternetHackConfig *InternetHackOf(std::uint32_t definition) const noexcept
	{
		return definition < internetHacks.size() && internetHacks[definition].present ? &internetHacks[definition] : nullptr;
	}
	bool InternetHackContainer(std::uint32_t definition) const noexcept
	{
		return definition < internetHackContainers.size() && internetHackContainers[definition] != 0;
	}
	const StickyBombConfig *StickyBombOf(std::uint32_t definition) const noexcept
	{
		return definition < stickyBombs.size() && stickyBombs[definition].present ? &stickyBombs[definition] : nullptr;
	}
	std::vector<SpectreGunshipConfig> spectreGunships;      // each definition's SpectreGunshipUpdate (present or not)
	std::vector<RailroadConfig> railroads;                  // each definition's RailroadBehavior (present or not)
	std::vector<std::vector<SpectreDeploymentConfig>> spectreDeployments; // each definition's SpectreGunshipDeploymentUpdates, in module order
	std::vector<std::optional<content::CrateCollideContent>> carBombs; // each definition's ConvertToCarBombCrateCollide
	// Each definition's ChinookAIUpdate combat drop (present: it has one) and its model's rope bones (RopeStart01..,
	// RopeEnd01..: where each rope hangs from and where its rappellers start down it, in its own frame).
	struct CombatDropConfig
	{
		bool present{false};
		content::CombatDropContent content;
		std::vector<Engine::Math::FixedVector3> ropeStarts;
		std::vector<content::RestBone> ropeEnds;
	};
	std::vector<CombatDropConfig> combatDrops;
	std::vector<std::optional<content::FlightDeckContent>> flightDecks; // each definition's FlightDeckBehavior
	std::vector<std::optional<RailedTransportConfig>> railedTransports; // each definition's railed transport
	const RailedTransportConfig *RailedTransportOf(std::uint32_t definition) const noexcept
	{
		return definition < railedTransports.size() && railedTransports[definition] ? &*railedTransports[definition] : nullptr;
	}
	std::vector<std::vector<content::SabotageCollideContent>> sabotages; // each definition's Sabotage*CrateCollides
	std::span<const content::SabotageCollideContent> SabotagesOf(std::uint32_t definition) const noexcept
	{
		return definition < sabotages.size() ? std::span<const content::SabotageCollideContent>(sabotages[definition]) : std::span<const content::SabotageCollideContent>{};
	}
	std::vector<std::optional<content::CrateCollideContent>> hijacks;  // each definition's ConvertToHijackedVehicleCrateCollide
	std::vector<std::string> hijackerParachutes;                      // each definition's HijackerUpdate ParachuteName
	const content::CrateCollideContent *HijackOf(std::uint32_t definition) const noexcept
	{
		return definition < hijacks.size() && hijacks[definition] ? &*hijacks[definition] : nullptr;
	}
	std::string HijackerParachuteOf(std::uint32_t definition) const
	{
		return definition < hijackerParachutes.size() ? hijackerParachutes[definition] : std::string{};
	}
	const content::FlightDeckContent *FlightDeckOf(std::uint32_t definition) const noexcept
	{
		return definition < flightDecks.size() && flightDecks[definition] ? &*flightDecks[definition] : nullptr;
	}
	const CombatDropConfig *CombatDropOf(std::uint32_t definition) const noexcept
	{
		return definition < combatDrops.size() && combatDrops[definition].present ? &combatDrops[definition] : nullptr;
	}
	const content::CrateCollideContent *CarBombOf(std::uint32_t definition) const noexcept
	{
		return definition < carBombs.size() && carBombs[definition] ? &*carBombs[definition] : nullptr;
	}
	const RailroadConfig *RailroadOf(std::uint32_t definition) const noexcept
	{
		return definition < railroads.size() && railroads[definition].present ? &railroads[definition] : nullptr;
	}
	const SpectreGunshipConfig *SpectreGunshipOf(std::uint32_t definition) const noexcept
	{
		return definition < spectreGunships.size() && spectreGunships[definition].present ? &spectreGunships[definition] : nullptr;
	}
	std::span<const SpectreDeploymentConfig> SpectreDeploymentsOf(std::uint32_t definition) const noexcept
	{
		return definition < spectreDeployments.size() ? std::span<const SpectreDeploymentConfig>(spectreDeployments[definition]) : std::span<const SpectreDeploymentConfig>{};
	}
	const MobMemberConfig *MobMemberOf(std::uint32_t definition) const noexcept
	{
		return definition < mobMembers.size() && mobMembers[definition].present ? &mobMembers[definition] : nullptr;
	}
	const ParticleCannonConfig *ParticleCannonOf(std::uint32_t definition) const noexcept
	{
		return definition < particleCannons.size() && particleCannons[definition].present ? &particleCannons[definition] : nullptr;
	}
	const LauncherDoorConfig *LauncherDoorOf(std::uint32_t definition) const noexcept
	{
		return definition < launcherDoors.size() && launcherDoors[definition].present ? &launcherDoors[definition] : nullptr;
	}
	engine::gameplay::VeterancyCatalog &veterancy;
	engine::gameplay::HordeCatalog &hordes;
	engine::gameplay::HarvestCatalog &harvest;
	engine::gameplay::LoadoutCatalog &loadouts;
	engine::gameplay::ParachuteCatalog &parachutes;
	engine::gameplay::DynamicClearingCatalog &dynamicClearings;
	engine::gameplay::TemplateEquivalence &equivalence;

private:
	enum class Kind : std::uint8_t
	{
		Definition,
		Weapon,
		Armor,
		Model,
		Effect,
		CommandSet,
	};

	const content::GameContent &m_content;
	engine::time::FixedStep m_step;
	std::vector<std::uint32_t> m_deathOf;
	std::vector<bool> m_hasAI;
	std::vector<std::uint32_t> m_parachuteOf;
	std::vector<const std::vector<content::RestBone> *> m_spawnPointsOf;
	std::vector<const content::BoneFxContent *> m_boneFxOf;
	std::set<std::string, std::less<>> m_historicResolving; // weapons whose historic bonus weapon is being made
	std::vector<ProjectileBody> m_bodies;
	std::vector<std::string> m_modelNames;
	std::map<std::string, std::uint32_t, std::less<>> m_modelIndex;
	std::array<std::vector<std::string>, engine::gameplay::DeathEffectKinds> m_deathEffectNames;
	std::array<std::map<std::string, std::uint32_t, std::less<>>, engine::gameplay::DeathEffectKinds> m_deathEffectIndex;
	std::vector<std::pair<Kind, std::string>> m_history;
	std::vector<const content::ObjectDefinition *> m_definitions;
	std::vector<const content::ObjectDefinition *> m_catalogEntries;
	std::map<std::string, std::uint32_t, std::less<>> m_definitionIndex;
	// Kinds' names as TemplateEquivalence keys, in the order met.
	std::map<std::string, std::uint32_t, std::less<>> m_families;
	std::vector<const content::WeaponContent *> m_weaponContent;
	std::map<std::string, std::uint32_t, std::less<>> m_weaponIndex;
	std::map<std::string, std::uint32_t, std::less<>> m_armorIndex;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::ObjectTemplates>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.object_templates";
};
}
