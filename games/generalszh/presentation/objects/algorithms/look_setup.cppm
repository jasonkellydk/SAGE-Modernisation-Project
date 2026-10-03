export module games.generalszh.presentation.objects.algorithms.look_setup;
import games.generalszh.presentation.objects.components.beacon_look;
import engine.gameplay.rts.harvesting.components.resource_store;
import games.generalszh.content.railroad.railroad_content;
import games.generalszh.content.water.wave_guide_content;
import games.generalszh.content.powers.special_ability_content;
import games.generalszh.content.eva.eva_content;
import games.generalszh.content.combat.combat_catalog;
import games.generalszh.content.slaves.slaved_content;
import std;
import games.generalszh.presentation.objects.components.tree_bend;
import games.generalszh.presentation.objects.components.bridge_look;
import games.generalszh.presentation.objects.components.object_icons;
import games.generalszh.presentation.objects.components.track_marks;
import games.generalszh.presentation.objects.components.uplink_effects;

export import engine.ecs.core.world;
export import games.generalszh.presentation.objects.components.object_presentation;
export import games.generalszh.presentation.objects.components.effect_attachments;
export import games.generalszh.presentation.objects.components.hit_fx;
export import games.generalszh.presentation.objects.systems.chassis_systems;
import games.generalszh.content.locomotors.locomotor_catalog;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.session.session_view;
import games.generalszh.content.objects.model_draw;
export import games.generalszh.presentation.objects.algorithms.debris_animation;
import games.generalszh.content.objects.model_conditions;
import engine.config.binding.values;
import games.generalszh.content.stealth.stealth_content;
import games.generalszh.content.fire.fire_content;
import games.generalszh.content.death.death_content;
import games.generalszh.content.topple.topple_content;
import games.generalszh.content.combat.emp_content;
import games.generalszh.content.upgrades.upgrade_content;
export import engine.gameplay.common.appearance.components.part_overrides;
import Engine.Core.Math.FixedPresentation;

// Filling presentation's look catalog between ticks (frames only read it):
// the looks of definitions the session has taken on since the last tick
// (every model state of each, read from the content once), and the looks
// of models shown instead of a definition's (debris) first seen this tick.
export namespace generalszh::presentation
{
void RegisterObjectPresentation(ecs::World &world)
{
	world.RegisterComponent<TickPose>();
	world.RegisterComponent<ShownLook>();
	world.RegisterComponent<ExtraShownLooks>();
	world.RegisterComponent<TreeSway>();
	world.RegisterComponent<HeatVision>();
	world.RegisterComponent<DisableHeard>();
	world.RegisterComponent<ObjectIcons>();
	world.RegisterComponent<ObjectEmoticon>();
	world.RegisterComponent<TintEnvelope>();
	world.RegisterComponent<SelectionFlash>();
	world.RegisterComponent<ScriptFlash>();
	world.RegisterComponent<CaptureFlash>();
	world.RegisterComponent<PrepSoundCue>();
	world.RegisterComponent<AbilityLaserView>();
	world.RegisterComponent<SpectreStrafeSeen>();
	world.RegisterComponent<GrantStealthView>();
	world.RegisterComponent<ObjectFade>();
	world.RegisterComponent<DefectorFlash>();
	world.RegisterComponent<ShroudSight>();
	world.RegisterComponent<DebrisMotion>();
	world.RegisterComponent<WeaponPose>();
	world.RegisterComponent<BarrelRecoil>();
	world.RegisterComponent<ConditionEmission>();
	world.RegisterComponent<CrashTrailEmission>();
	world.RegisterComponent<FirestormEmission>();
	world.RegisterComponent<FxEmission>();
	world.RegisterComponent<BoneFxEmission>();
	world.RegisterComponent<DamageEmission>();
	world.RegisterComponent<ExhaustState>();
	world.RegisterComponent<HitFxThrottle>();
	world.RegisterComponent<ExhaustEmission>();
	world.RegisterComponent<TrackMarks>();
	world.RegisterComponent<TreeBend>();
	world.RegisterComponent<BridgeLook>();
	world.RegisterComponent<UplinkEffects>();
	world.RegisterComponent<BeaconLook>();
	world.RegisterComponent<BeaconCaption>();
}

DefinitionLooks ReadDefinitionLooks(const session::SessionView &view, std::uint32_t definition, LookCatalog &catalog)
{
	const content::ObjectDefinition &object = view.Definition(definition);
	DefinitionLooks looks;
	looks.states = content::ReadModelStates(object);
	looks.recoil = {Engine::Math::ToFloat(looks.states.recoil.initial), Engine::Math::ToFloat(looks.states.recoil.max),
		Engine::Math::ToFloat(looks.states.recoil.damping), Engine::Math::ToFloat(looks.states.recoil.settle),
		!looks.states.Empty() && !looks.states.states.front().recoilBone.empty()};
	looks.scale = Engine::Math::ToFloat(object.scale);
	looks.castsShadow = object.shadow == 1u || object.shadow == 2u || object.shadow == 4u;
	looks.shadowKind = looks.castsShadow ? object.shadow : std::uint8_t{0};
	if (object.shadow == 1u)
	{
		looks.shadowDecal = static_cast<std::uint32_t>(catalog.shadowDecals.size());
		catalog.shadowDecals.push_back({ShadowDecalTexture(object.shadowTexture, object.geometry.shape == content::GeometryShape::Box),
			Engine::Math::ToFloat(object.shadowSizeX), Engine::Math::ToFloat(object.shadowSizeY), Engine::Math::ToFloat(object.shadowOffsetX),
			Engine::Math::ToFloat(object.shadowOffsetY)});
	}
	looks.constructionHeight = Engine::Math::ToFloat(object.geometry.shape == content::GeometryShape::Sphere ? object.geometry.majorRadius : object.geometry.height);
	looks.animatesWhileDisabled = object.Is("PRODUCED_AT_HELIPAD");
	looks.ignoredInGui = object.Is("IGNORED_IN_GUI");
	looks.revealsEnemyPaths = object.Is("REVEALS_ENEMY_PATHS");
	looks.majorRadius = Engine::Math::ToFloat(object.geometry.majorRadius);
	looks.minorRadius = Engine::Math::ToFloat(object.geometry.minorRadius);
	// Object::getObjectExitInterface: the first behaviour module with an exit (the rally line's).
	for (const content::ModuleEntry &module : object.modules)
	{
		if (module.slot != content::ModuleSlot::Behavior)
			continue;
		const std::string_view type = module.type;
		const bool production = type == "DefaultProductionExitUpdate" || type == "QueueProductionExitUpdate" || type == "SupplyCenterProductionExitUpdate";
		if (!production && type != "SpawnPointProductionExitUpdate" && type != "ParkingPlaceBehavior" && type != "FlightDeckBehavior")
			continue;
		if (type == "ParkingPlaceBehavior")
			looks.rallyExit = DefinitionLooks::RallyExit::Helipad;
		else if (production && module.block != nullptr)
		{
			looks.rallyExit = DefinitionLooks::RallyExit::Production;
			engine::config::Diagnostics diagnostics;
			engine::config::BindContext bind{diagnostics, engine::time::FixedStep{30}};
			const auto read = [&](std::string_view key, std::array<float, 3> &out) {
				if (const auto *node = module.block->Find(key))
					if (const auto point = engine::config::ReadVec3(*node, bind))
						out = {Engine::Math::ToFloat(point->x), Engine::Math::ToFloat(point->y), Engine::Math::ToFloat(point->z)};
			};
			read("UnitCreatePoint", looks.exitCreatePoint);
			read("NaturalRallyPoint", looks.exitRallyPoint);
		}
		break;
	}
	for (const content::ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.slot == content::ModuleSlot::Draw && module.type == "W3DScienceModelDraw")
		{
			looks.needsScience = true;
			if (const auto *science = module.block->Find("RequiredScience"); science != nullptr && !science->values.empty())
				looks.requiredScience = view.Content().Science(science->Value()).value_or(0xFFFFFFFFu);
		}
	for (const content::ModuleEntry &module : object.modules)
	{
		if (module.slot == content::ModuleSlot::ClientUpdate && module.type == "AnimatedParticleSysBoneClientUpdate")
			looks.animatedParticleBones = true;
		if (module.block != nullptr && module.slot == content::ModuleSlot::Draw && module.type.starts_with("W3D"))
			if (const auto *flag = module.block->Find("ParticlesAttachedToAnimatedBones"); flag != nullptr && !flag->values.empty())
				looks.animatedParticleBones = looks.animatedParticleBones || engine::config::values::ParseBool(flag->Value()).value_or(false);
	}
	for (const content::ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.slot == content::ModuleSlot::ClientUpdate && module.type == "BeaconClientUpdate")
		{
			// parseDurationUnsignedInt: milliseconds to frames, rounded up.
			const auto frames = [&](std::string_view key, std::uint64_t fallback) {
				const auto *node = module.block->Find(key);
				const auto ms = node != nullptr && !node->values.empty() ? engine::config::values::ParseFixed(node->Value()) : std::nullopt;
				if (!ms || *ms < Engine::Math::Fixed{})
					return fallback;
				const auto numerator = static_cast<std::uint64_t>(ms->Raw()) * 30u;
				const std::uint64_t denominator = std::uint64_t{1000} << Engine::Math::Fixed::FractionBits;
				return (numerator + denominator - 1) / denominator;
			};
			looks.beacon = true;
			looks.beaconPulseEvery = frames("RadarPulseFrequency", 30);
			looks.beaconPulseFor = frames("RadarPulseDuration", 15);
		}
	for (const content::ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.slot == content::ModuleSlot::Draw && module.type == "W3DSupplyDraw")
			if (const auto *prefix = module.block->Find("SupplyBonePrefix"); prefix != nullptr && !prefix->values.empty())
				looks.supplyBonePrefix = std::string(prefix->Value());
	for (const content::ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.type == "DynamicShroudClearingRangeUpdate")
			if (const auto *grid = module.block->Find("GridDecalTemplate"))
				looks.gridDecal = content::ReadRadiusDecal(*grid, 30);
	looks.infantry = object.Is("INFANTRY");
	looks.shrubbery = object.Is("SHRUBBERY");
	looks.boxFootprint = object.geometry.shape == content::GeometryShape::Box;
	looks.structure = object.Is("STRUCTURE");
	// RTS3DScene's potential occludees: things that score (shown through buildings while building occlusion is on).
	looks.scoring = object.Is("SCORE") || object.Is("SCORE_CREATE") || object.Is("SCORE_DESTROY") || object.Is("MP_COUNT_FOR_VICTORY");
	looks.vehicle = object.Is("VEHICLE");
	looks.drone = object.Is("DRONE");
	looks.hugeVehicle = object.Is("HUGE_VEHICLE");
	looks.noHealIcon = object.Is("NO_HEAL_ICON");
	for (const content::ModuleEntry &module : object.modules)
		if (module.block != nullptr && (module.type == "DumbProjectileBehavior" || module.type == "MissileAIUpdate"))
			if (const auto *fx = module.block->Find("GarrisonHitKillFX"); fx != nullptr && fx->Value() != "None")
				looks.garrisonHitFx = std::string(fx->Value());
	for (const content::ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.type == "FirestormDynamicGeometryInfoUpdate")
		{
			for (int system = 1; system <= 16; ++system)
				if (const auto *node = module.block->Find("ParticleSystem" + std::to_string(system)); node != nullptr && node->Value() != "None")
					looks.firestormSystems.push_back(std::string(node->Value()));
			if (const auto *node = module.block->Find("ParticleOffsetZ"))
				looks.firestormOffsetZ = Engine::Math::ToFloat(engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{}));
		}
	if (const auto boneFx = view.Content().boneFx.find(object.name); boneFx != view.Content().boneFx.end())
	{
		looks.boneParticles = boneFx->second.particles;
		looks.boneParticleTypes = boneFx->second.particleTypes;
	}
	looks.uplink = content::ReadUplinkLook(object, [&](std::string_view name) { return view.ObjectNamed(name); });
	if (const auto pulse = content::ReadEmpPulse(object, engine::time::FixedStep{30}))
	{
		// saturateRGB: each channel scaled by the factor, less half the factor.
		const auto saturate = [](const std::array<std::uint8_t, 3> &color, float factor) {
			std::array<float, 3> tint{};
			for (std::size_t channel = 0; channel < 3; ++channel)
				tint[channel] = static_cast<float>(color[channel]) / 255.0f * factor - factor * 0.5f;
			return tint;
		};
		looks.emp = true;
		looks.empStartScale = Engine::Math::ToFloat(pulse->startScale);
		looks.empFadeTicks = pulse->fadeTicks;
		looks.empStartTint = saturate(pulse->startColor, 2.0f);
		looks.empEndTint = saturate(pulse->endColor, 5.0f);
		looks.empSparks = pulse->sparks;
		float density = 0.0f;
		const std::string &text = pulse->sparksPerCubicFoot;
		if (std::from_chars(text.data(), text.data() + text.size(), density).ec == std::errc{})
			looks.empSparksPerCubicFoot = density;
	}
	for (const content::ModuleEntry &module : object.modules)
		if (module.type == "W3DDependencyModelDraw" && module.block != nullptr)
			if (const auto *bone = module.block->Find("AttachToBoneInContainer"); bone != nullptr && !bone->Value().empty())
				looks.attachToBone = std::string(bone->Value());
	looks.animationsRequirePower = looks.states.animationsRequirePower;
	looks.receivesDynamicLights = looks.states.receivesDynamicLights;
	if (!looks.states.trackMarks.empty())
	{
		const auto known = std::find(catalog.trackTextures.begin(), catalog.trackTextures.end(), looks.states.trackMarks);
		looks.trackTexture = static_cast<std::uint32_t>(known - catalog.trackTextures.begin());
		if (known == catalog.trackTextures.end())
			catalog.trackTextures.push_back(looks.states.trackMarks);
		if (const auto width = view.Content().trackWidths.find(object.name); width != view.Content().trackWidths.end())
			looks.trackWidth = Engine::Math::ToFloat(width->second);
	}
	looks.lightRadius = Engine::Math::ToFloat(content::BoundingSphereRadius(object.geometry));
	// A state with several animations has a look for each (W3DModelDraw picks one at random as it takes the state).
	const auto addLook = [&](std::uint32_t state, std::string model, std::uint32_t variants) {
		looks.stateLooks.push_back(static_cast<std::uint32_t>(catalog.looks.size()));
		looks.stateVariants.push_back(std::max(variants, 1u));
		for (std::uint32_t variant = 0; variant < std::max(variants, 1u); ++variant)
		{
			catalog.looks.push_back({definition, state, 0, 0, variant});
			catalog.lookModels.push_back(model);
		}
	};
	looks.resting = content::DefaultModel(object);
	looks.bufferTree = looks.resting.drawType == "W3DTreeDraw";
	{
		const std::string &draw = content::DefaultModel(object).drawType;
		looks.ridersTakeTint = draw == "W3DOverlordAircraftDraw" || draw == "W3DOverlordTankDraw" || draw == "W3DOverlordTruckDraw";
	}
	// A tree-buffer tree casts its shadow by its W3DTreeDraw's DoShadow (W3DTreeBuffer), not by the object's Shadow.
	if (looks.bufferTree)
	{
		looks.castsShadow = content::ReadTreeDraw(object).value_or(content::TreeDrawMotion{}).doShadow;
		looks.shadowKind = 0;
	}
	if (const auto tree = content::ReadTreeDraw(object))
	{
		TreeMotion &motion = looks.treeMotion;
		motion.doTopple = tree->doTopple;
		motion.killWhenToppled = tree->killWhenToppled;
		motion.initialVelocity = Engine::Math::ToFloat(tree->initialVelocity);
		motion.initialAcceleration = Engine::Math::ToFloat(tree->initialAcceleration);
		motion.bounceVelocity = Engine::Math::ToFloat(tree->bounceVelocity);
		motion.minimumToppleSpeed = Engine::Math::ToFloat(tree->minimumToppleSpeed);
		motion.sinkFrames = static_cast<float>(tree->sinkFrames);
		motion.sinkDistance = Engine::Math::ToFloat(tree->sinkDistance);
		motion.framesToMoveOutward = static_cast<float>(tree->framesToMoveOutward);
		motion.framesToMoveInward = static_cast<float>(tree->framesToMoveInward);
		motion.maxOutwardMovement = Engine::Math::ToFloat(tree->maxOutwardMovement);
		motion.darkening = Engine::Math::ToFloat(tree->darkening);
		motion.texture = tree->texture;
		motion.toppleFX = tree->toppleFX;
		motion.bounceFX = tree->bounceFX;
	}
	looks.bendsTrees = (object.Is("INFANTRY") || object.Is("VEHICLE")) && !object.Is("IMMOBILE");
	looks.treeReach = Engine::Math::ToFloat(object.geometry.shape == content::GeometryShape::Box ? std::min(object.geometry.majorRadius, object.geometry.minorRadius)
																												: object.geometry.majorRadius);
	looks.topplesTrees = object.crusherLevel > 1;
	looks.ownLookFirst = static_cast<std::uint32_t>(catalog.looks.size());
	if (looks.states.Empty())
		addLook(0, content::DefaultModel(object).model, 1);
	else
		for (std::uint32_t state = 0; state < looks.states.states.size(); ++state)
			addLook(state, looks.states.states[state].model, static_cast<std::uint32_t>(looks.states.states[state].animations.size()));
	looks.ownLookCount = static_cast<std::uint32_t>(catalog.looks.size()) - looks.ownLookFirst;
	looks.partSets = content::ReadPartOverrideSets(object);
	for (content::ModelStates &states : content::ReadExtraModelStates(object))
	{
		DefinitionLooks::ExtraDraw extra;
		extra.states = std::move(states);
		const auto draw = static_cast<std::uint32_t>(looks.extraDraws.size() + 1);
		for (std::uint32_t state = 0; state < extra.states.states.size(); ++state)
		{
			const auto variants = std::max(static_cast<std::uint32_t>(extra.states.states[state].animations.size()), 1u);
			extra.stateLooks.push_back(static_cast<std::uint32_t>(catalog.looks.size()));
			extra.stateVariants.push_back(variants);
			for (std::uint32_t variant = 0; variant < variants; ++variant)
			{
				catalog.looks.push_back({definition, state, 0, draw, variant});
				catalog.lookModels.push_back(extra.states.states[state].model);
			}
		}
		looks.extraDraws.push_back(std::move(extra));
	}
	const engine::time::FixedStep step{30};
	// Without a StealthUpdate to say, a stealthed friend shows at GameData's StealthFriendlyOpacity.
	looks.stealthMin = Engine::Math::ToFloat(view.Content().gameData.stealthFriendlyOpacity);
	if (const auto stealth = content::ReadObjectStealth(object, step))
	{
		looks.stealth = true;
		looks.stealthMin = Engine::Math::ToFloat(stealth->friendlyOpacityMin);
		looks.stealthMax = Engine::Math::ToFloat(stealth->friendlyOpacityMax);
		looks.stealthPulseTicks = static_cast<float>(stealth->pulseTicks);
	}
	if (const auto fire = content::ReadObjectFire(object, step))
		looks.burningSound = fire->burningSound;
	looks.crashSound = content::ReadCrashLoopSound(object);
	looks.ambientSound = std::string(object.Sound("SoundAmbient"));
	looks.ambientDamaged = std::string(object.Sound("SoundAmbientDamaged"));
	looks.ambientReallyDamaged = std::string(object.Sound("SoundAmbientReallyDamaged"));
	looks.ambientRubble = std::string(object.Sound("SoundAmbientRubble"));
	looks.onDamaged = std::string(object.Sound("SoundOnDamaged"));
	looks.onReallyDamaged = std::string(object.Sound("SoundOnReallyDamaged"));
	looks.moveStart = std::string(object.Sound("SoundMoveStart"));
	looks.moveLoop = std::string(object.Sound("SoundMoveLoop"));
	looks.moveStartDamaged = std::string(object.Sound("SoundMoveStartDamaged"));
	looks.moveLoopDamaged = std::string(object.Sound("SoundMoveLoopDamaged"));
	looks.stealthOn = std::string(object.Sound("SoundStealthOn"));
	looks.afterburnerSound = std::string(object.Sound("Afterburner"));
	looks.lowFuelVoice = std::string(object.Sound("VoiceLowFuel"));
	// OpenContainModuleData EnterSound / ExitSound (its contain module's: the first module whose type ends in Contain).
	for (const content::ModuleEntry &module : object.modules)
		if (module.block != nullptr && std::string_view(module.type).ends_with("Contain"))
		{
			if (const auto *enter = module.block->Find("EnterSound"); enter != nullptr && !enter->values.empty())
				looks.enterSound = std::string(enter->Value());
			if (const auto *exit = module.block->Find("ExitSound"); exit != nullptr && !exit->values.empty())
				looks.exitSound = std::string(exit->Value());
			break;
		}
	looks.rapidFireVoice = std::string(object.Sound("VoiceRapidFire"));
	for (const content::SpecialAbilityContent &ability : content::ReadSpecialAbilities(object, step))
		if (const auto power = view.Content().powers.Template(ability.power))
		{
			if (!ability.prepSoundLoop.empty())
				looks.prepLoops.emplace_back(*power, ability.prepSoundLoop);
			if (!ability.specialObjectAttachToBone.empty())
				looks.laserBones.emplace_back(*power, ability.specialObjectAttachToBone);
			if (!ability.disableFxParticleSystem.empty())
				looks.disableFx.emplace_back(*power, ability.disableFxParticleSystem);
		}
	looks.laser = content::ReadLaserLook(object);
	if (const auto welding = content::ReadSlavedWelding(object))
	{
		looks.weldingSystem = welding->system;
		looks.weldingBone = welding->bone;
	}
	if (const auto neutron = content::ReadNeutronMissile(object, step))
	{
		looks.neutronJitter = Engine::Math::ToFloat(neutron->specialJitter);
		looks.neutronSpecialTicks = neutron->flight.specialSpeedTicks;
	}
	looks.onRadar = object.radarPriority != "NOT_ON_RADAR";
	if (object.radarPriority.empty() || object.radarPriority == "INVALID")
		looks.onRadar = object.Is("CAPTURABLE") ||
			std::any_of(object.modules.begin(), object.modules.end(), [](const content::ModuleEntry &module) { return module.type == "GarrisonContain"; });
	looks.trapLike = object.Is("MINE") || object.Is("BOOBY_TRAP") || object.Is("DEMOTRAP");
	for (const content::ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.type == "StealthUpdate")
		{
			constexpr std::array<std::string_view, 2> keys{"EnemyDetectionEvaEvent", "OwnDetectionEvaEvent"};
			for (std::size_t side = 0; side < keys.size(); ++side)
				if (const auto *node = module.block->Find(keys[side]); node != nullptr && !node->values.empty())
					looks.detectionEva[side] = content::EvaMessageOf(node->Value()).value_or(DefinitionLooks::NoEva);
			break;
		}
	for (const content::ModuleEntry &module : object.modules)
	{
		if (module.block != nullptr && module.type == "ChinookAIUpdate")
			if (const auto *wash = module.block->Find("RotorWashParticleSystem"); wash != nullptr && !wash->values.empty())
				looks.rotorWash = std::string(wash->Value());
		if (module.block != nullptr && module.type == "BattlePlanUpdate")
		{
			constexpr std::array<std::string_view, 3> plans{"Bombardment", "HoldTheLine", "SearchAndDestroy"};
			const auto text = [&](const std::string &key) {
				const auto *node = module.block->Find(key);
				return node != nullptr && !node->values.empty() ? std::string(node->Value()) : std::string{};
			};
			for (std::size_t plan = 0; plan < plans.size(); ++plan)
			{
				looks.planUnpackSounds[plan] = text(std::string(plans[plan]) + "PlanUnpackSoundName");
				looks.planPackSounds[plan] = text(std::string(plans[plan]) + "PlanPackSoundName");
			}
			looks.planIdleLoop = text("SearchAndDestroyPlanIdleLoopSoundName");
		}
		if (module.block != nullptr && module.type == "MissileLauncherBuildingUpdate")
			if (const auto *hiss = module.block->Find("DoorOpenIdleAudio"); hiss != nullptr && !hiss->values.empty())
				looks.doorOpenIdleAudio = std::string(hiss->Value());
		if (module.block != nullptr && module.type == "GrantStealthBehavior")
			if (const auto *radius = module.block->Find("RadiusParticleSystemName"); radius != nullptr && !radius->values.empty())
				looks.grantStealthSystem = std::string(radius->Value());
		if (module.block != nullptr && module.type == "BunkerBusterBehavior")
		{
			if (const auto *crash = module.block->Find("CrashThroughBunkerFX"); crash != nullptr && !crash->values.empty() && crash->Value() != "None")
				looks.crashThroughFx = std::string(crash->Value());
			if (const auto *every = module.block->Find("CrashThroughBunkerFXFrequency"); every != nullptr && !every->values.empty())
				if (const auto ms = engine::config::values::ParseFixed(every->Value()))
					looks.crashThroughTicks = static_cast<std::uint32_t>(std::ceil(Engine::Math::ToFloat(*ms) * 30.0f / 1000.0f));
		}
		if (module.block != nullptr && module.type == "SpectreGunshipUpdate" && looks.gattlingStrafeFx.empty())
			if (const auto *strafe = module.block->Find("GattlingStrafeFXParticleSystem"); strafe != nullptr && !strafe->values.empty())
				looks.gattlingStrafeFx = std::string(strafe->Value());
	}
	if (const auto railroad = content::ReadRailroad(object, engine::time::FixedStep{30}); railroad && railroad->locomotive)
		looks.trainRunningSound = railroad->runningSound;
	if (const auto wave = content::ReadWaveGuide(object, engine::time::FixedStep{30}))
	{
		looks.waveLoopingSound = wave->loopingSound;
		looks.waveSplashSound = wave->randomSplashSound;
		looks.waveBridgeParticle = wave->bridgeParticle;
	}
	looks.mine = object.Is("MINE");
	looks.ghost = object.Is("IMMOBILE") &&
		std::none_of(object.modules.begin(), object.modules.end(), [](const content::ModuleEntry &module) { return module.type == "W3DDefaultDraw"; });
	looks.stealthOff = std::string(object.Sound("SoundStealthOff"));
	looks.promotedSounds = {std::string(object.Sound("SoundPromotedVeteran")), std::string(object.Sound("SoundPromotedElite")),
		std::string(object.Sound("SoundPromotedHero"))};
	looks.turretLoop = std::string(object.Sound("TurretMoveLoop"));
	looks.constructionLoop = std::string(object.Sound("UnderConstruction"));
	looks.damage = ReadDamageEffects(object);
	if (const auto detector = content::ReadDetectorLook(object))
		looks.detector = DefinitionLooks::DetectorLook{detector->ping, detector->brightPing, detector->beacon, detector->grid, detector->bone,
			detector->pingSound, detector->loudPingSound};
	if (const auto topple = content::ReadObjectTopple(object))
	{
		looks.toppleFX = topple->toppleFX;
		looks.bounceFX = topple->bounceFX;
	}
	if (const auto trail = content::ReadCrashTrail(object))
		looks.crashTrail.push_back({trail->bone, trail->system, trail->offset});
	for (const content::ModuleEntry &module : object.modules)
		if (module.block != nullptr && (module.type == "SupplyTruckAIUpdate" || module.type == "ChinookAIUpdate" || module.type == "WorkerAIUpdate"))
		{
			if (const auto *voice = module.block->Find("SuppliesDepletedVoice"); voice != nullptr && voice->Value() != "NoSound")
				looks.suppliesDepletedVoice = std::string(voice->Value());
		}
		else if (module.type == "SwayClientUpdate")
			looks.treeSway = true;
		else if (module.type == "FloatUpdate")
			looks.sways = true;
		else if (module.type == "W3DPoliceCarDraw")
			looks.policeLights = true;
		else if (module.block != nullptr && module.type == "MissileAIUpdate")
			if (const auto *ignition = module.block->Find("IgnitionFX"); ignition != nullptr && ignition->Value() != "None")
				looks.ignitionFX = std::string(ignition->Value());
	if (const std::string_view locomotor = content::ObjectLocomotorName(object, view.Content().locomotors); !locomotor.empty())
		if (const auto chassis = view.Content().chassisLooks.find(locomotor); chassis != view.Content().chassisLooks.end())
			looks.chassis = TuningOf(chassis->second);
	looks.majorRadius = static_cast<float>(static_cast<double>(object.geometry.majorRadius.Raw()) / 65536.0);
	looks.minorRadius = static_cast<float>(static_cast<double>(object.geometry.minorRadius.Raw()) / 65536.0);
	for (const content::ArmorSetDamageFx &set : content::ReadArmorSetDamageFx(object))
	{
		const auto table = view.Content().damageFx.find(set.damageFx);
		looks.hitFx.push_back({set.armor, table != view.Content().damageFx.end() ? &table->second : nullptr});
	}
	return looks;
}

// The looks of objects that have taken on part overrides (SubObjectsUpgrade) in a run not drawn before: a copy of
// their definition's own looks, drawn with the run's sets in order (W3DModelDraw::showSubObject over its states).
void KnowPartLooks(LookCatalog &catalog, ecs::World &world)
{
	ecs::Query<ecs::Read<engine::gameplay::PartOverrides>, ecs::Read<engine::gameplay::DefinitionRef>> query(world);
	query.ForEachChunk([&](auto chunk) {
		const auto overrides = chunk.template Get<engine::gameplay::PartOverrides>();
		const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
		for (std::size_t row = 0; row < overrides.size(); ++row)
		{
			const std::uint32_t definition = definitions[row].index;
			if (definition >= catalog.byDefinition.size() || catalog.known[definition] == 0 || overrides[row].count == 0)
				continue;
			DefinitionLooks &looks = catalog.byDefinition[definition];
			const std::span<const std::uint8_t> applied(overrides[row].applied.data(), overrides[row].count);
			if (std::ranges::any_of(looks.partVariants, [&](const auto &variant) { return std::ranges::equal(variant.applied, applied); }))
				continue;
			std::vector<std::pair<std::string, bool>> combined;
			for (const std::uint8_t set : applied)
				if (set < looks.partSets.size())
					combined.insert(combined.end(), looks.partSets[set].begin(), looks.partSets[set].end());
			const auto parts = static_cast<std::uint32_t>(catalog.partOverrides.size());
			catalog.partOverrides.push_back(std::move(combined));
			const auto first = static_cast<std::uint32_t>(catalog.looks.size());
			for (std::uint32_t look = looks.ownLookFirst; look < looks.ownLookFirst + looks.ownLookCount; ++look)
			{
				LookEntry entry = catalog.looks[look];
				entry.parts = parts;
				catalog.looks.push_back(entry);
				catalog.lookModels.push_back(catalog.lookModels[look]);
			}
			looks.partVariants.push_back({{applied.begin(), applied.end()}, first});
		}
	});
}

// W3DSupplyDraw::updateDrawModuleSupplyStatus for what stores supplies: once its model is loaded, how many supply bones
// (SupplyBonePrefix01, 02, ... to the first gap) it has; then for each count shown in play not made before, a copy of
// its own looks with those above it hidden (doHideShowSubObjs: all show at first; the ones past the count hide).
void KnowSupplyLooks(LookCatalog &catalog, ecs::World &world, const BonePoses &bones)
{
	ecs::Query<ecs::Read<engine::gameplay::ResourceStore>, ecs::Read<engine::gameplay::DefinitionRef>> query(world);
	query.ForEachChunk([&](auto chunk) {
		const auto stores = chunk.template Get<engine::gameplay::ResourceStore>();
		const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
		for (std::size_t row = 0; row < stores.size(); ++row)
		{
			const std::uint32_t definition = definitions[row].index;
			if (definition >= catalog.byDefinition.size() || catalog.known[definition] == 0)
				continue;
			DefinitionLooks &looks = catalog.byDefinition[definition];
			if (looks.supplyBonePrefix.empty() || looks.ownLookCount == 0)
				continue;
			if (looks.supplyBones < 0)
			{
				const std::string &model = catalog.lookModels[looks.ownLookFirst];
				if (!bones.pose || !bones.locate || !bones.pose(model, looks.supplyBonePrefix).ready)
					continue;
				looks.supplyBones = static_cast<std::int32_t>(bones.locate(model, looks.supplyBonePrefix, true).size());
				looks.supplyLooks.assign(static_cast<std::size_t>(looks.supplyBones), DefinitionLooks::NoLook);
			}
			const std::uint32_t shown = looks.SupplyShown(stores[row].boxes, stores[row].startingBoxes);
			if (shown >= static_cast<std::uint32_t>(looks.supplyBones) || looks.supplyLooks[shown] != DefinitionLooks::NoLook)
				continue;
			std::vector<std::pair<std::string, bool>> hidden;
			for (std::int32_t index = static_cast<std::int32_t>(shown) + 1; index <= looks.supplyBones; ++index)
			{
				char number[4];
				std::snprintf(number, sizeof number, "%02d", index);
				hidden.emplace_back(looks.supplyBonePrefix + number, false);
			}
			const auto parts = static_cast<std::uint32_t>(catalog.partOverrides.size());
			catalog.partOverrides.push_back(std::move(hidden));
			looks.supplyLooks[shown] = static_cast<std::uint32_t>(catalog.looks.size());
			for (std::uint32_t look = looks.ownLookFirst; look < looks.ownLookFirst + looks.ownLookCount; ++look)
			{
				LookEntry entry = catalog.looks[look];
				entry.parts = parts;
				catalog.looks.push_back(entry);
				catalog.lookModels.push_back(catalog.lookModels[look]);
			}
		}
	});
}

void KnowLooks(LookCatalog &catalog, const session::SessionView &view)
{
	namespace mc = content::model_condition;
	catalog.bits = {mc::FiringA, mc::StealthedLook, mc::DetectedLook, mc::Dying, mc::Aflame, mc::SpecialDamaged, mc::Damaged, mc::ReallyDamaged, mc::Rubble, mc::Night, mc::Snow, mc::JetAfterburner, mc::Burned, mc::Toppled, mc::ActivelyBeingConstructed};
	const std::size_t count = view.DefinitionCount();
	if (catalog.byDefinition.size() < count)
	{
		catalog.byDefinition.resize(count);
		catalog.known.resize(count, 0);
	}
	for (std::uint32_t definition = 0; definition < count; ++definition)
		if (catalog.known[definition] == 0)
		{
			catalog.byDefinition[definition] = ReadDefinitionLooks(view, definition, catalog);
			catalog.known[definition] = 1;
		}
	const auto &misc = view.Content().miscAudio;
	if (const auto found = misc.find("RepairSparks"); found != misc.end())
		catalog.repairSparksSound = found->second;
	if (const auto found = misc.find("CrateSalvage"); found != misc.end())
		catalog.crateSalvageSound = found->second;
	if (const auto found = misc.find("CrateMoney"); found != misc.end())
		catalog.crateMoneySound = found->second;
	if (const auto found = misc.find("CrateFreeUnit"); found != misc.end())
		catalog.crateFreeUnitSound = found->second;
	if (const auto found = misc.find("CrateHeal"); found != misc.end())
		catalog.crateHealSound = found->second;
	if (const auto found = misc.find("CrateShroud"); found != misc.end())
		catalog.crateShroudSound = found->second;
	if (const auto found = misc.find("UnitPromoted"); found != misc.end())
		catalog.unitPromotedSound = found->second;
	if (const auto found = misc.find("BuildingDisabled"); found != misc.end())
		catalog.buildingDisabledSound = found->second;
	if (const auto found = misc.find("SplatterVehiclePilotsBrain"); found != misc.end())
		catalog.pilotSplatterSound = found->second;
	if (const auto found = misc.find("VehicleDisabled"); found != misc.end())
		catalog.vehicleDisabledSound = found->second;
	if (const auto found = misc.find("BuildingReenabled"); found != misc.end())
		catalog.buildingReenabledSound = found->second;
	if (const auto found = misc.find("VehicleReenabled"); found != misc.end())
		catalog.vehicleReenabledSound = found->second;
	if (const auto found = misc.find("DefectorTimerTickSound"); found != misc.end())
		catalog.defectorTickSound = found->second;
	if (const auto found = misc.find("DefectorTimerDingSound"); found != misc.end())
		catalog.defectorDingSound = found->second;
	if (const auto found = misc.find("AircraftWheelScreech"); found != misc.end())
		catalog.aircraftWheelScreechSound = found->second;
	const content::GameData &data = view.Content().gameData;
	// W3DInGameUI::drawMoveHints: the MoveHintName model with its animation "<name>.<name>", played once.
	if (catalog.moveHintLook == LookCatalog::NoLook && !data.moveHintName.empty())
	{
		LookEntry entry;
		entry.model = LookCatalog::BareModel;
		entry.mode = static_cast<std::uint8_t>(content::ModelAnimationMode::Once);
		catalog.moveHintLook = static_cast<std::uint32_t>(catalog.looks.size());
		catalog.looks.push_back(entry);
		catalog.lookModels.push_back(data.moveHintName);
		catalog.lookAnimations.resize(catalog.looks.size() - 1);
		catalog.lookAnimations.push_back(data.moveHintName + "." + data.moveHintName);
	}
	// W3DWaypointBuffer: the SCMNode model at each waypoint shown, no animation.
	if (catalog.waypointNodeLook == LookCatalog::NoLook)
	{
		LookEntry entry;
		entry.model = LookCatalog::BareModel;
		catalog.waypointNodeLook = static_cast<std::uint32_t>(catalog.looks.size());
		catalog.looks.push_back(entry);
		catalog.lookModels.push_back("SCMNode");
		catalog.lookAnimations.resize(catalog.looks.size());
	}
	catalog.selectionFlashHouseColor = data.selectionFlashHouseColor;
	catalog.selectionFlashSaturation = Engine::Math::ToFloat(data.selectionFlashSaturationFactor);
	catalog.levelGainAnimation = data.levelGainAnimation;
	{
		const content::GameData::BodyParticleSet *sets[] = {&data.fireSmall, &data.fireMedium, &data.fireLarge, &data.smokeSmall, &data.smokeMedium,
			&data.smokeLarge, &data.aflame};
		for (std::size_t index = 0; index < catalog.bodyParticles.size(); ++index)
			catalog.bodyParticles[index] = {sets[index]->prefix, sets[index]->system, sets[index]->max};
	}
	catalog.levelGainSeconds = Engine::Math::ToFloat(data.levelGainSeconds);
	catalog.levelGainRise = Engine::Math::ToFloat(data.levelGainRise);
	catalog.getHealedAnimation = data.getHealedAnimation;
	catalog.getHealedSeconds = Engine::Math::ToFloat(data.getHealedSeconds);
	catalog.getHealedRise = Engine::Math::ToFloat(data.getHealedRise);
	for (std::uint32_t armor = static_cast<std::uint32_t>(catalog.armorNames.size()); armor < view.ArmorCount(); ++armor)
		catalog.armorNames.emplace_back(view.ArmorName(armor));
	// Models shown instead of a definition's (debris pieces), still and with each animation a piece plays (W3DDebrisDraw:
	// its initial animation once, its flying one looping, its final one once or held at its first frame), and the
	// effects pieces play as they land.
	const auto know = [&](const engine::gameplay::VisibleObject &object, std::uint32_t animation, content::ModelAnimationMode mode) {
		const auto code = static_cast<std::uint8_t>(mode);
		if (catalog.LookOfModel(object.model, animation, code) < catalog.looks.size())
			return;
		LookEntry entry{object.definition, 0, object.model};
		entry.animation = animation;
		entry.mode = code;
		catalog.looks.push_back(entry);
		catalog.lookModels.emplace_back(view.ModelName(object.model));
		// lookAnimations runs alongside looks (a definition's own looks leave theirs empty).
		catalog.lookAnimations.resize(catalog.looks.size() - 1);
		catalog.lookAnimations.emplace_back(animation != 0 ? view.ModelName(animation) : std::string_view{});
		const auto key = std::pair{LookCatalog::ModelKey(object.model, animation, code), static_cast<std::uint32_t>(catalog.looks.size() - 1)};
		catalog.modelLooks.insert(std::upper_bound(catalog.modelLooks.begin(), catalog.modelLooks.end(), key), key);
	};
	view.Visible().ForEach([&](const engine::gameplay::VisibleObject &object) {
		if (object.model == 0)
			return;
		know(object, 0, content::ModelAnimationMode::Manual);
		const auto &debris = object.debris;
		for (std::size_t state = 0; state < debris.animations.size(); ++state)
			if (debris.animations[state] != 0)
				know(object, debris.animations[state], DebrisAnimationMode(state, debris.finalStop != 0));
		if (const std::uint32_t system = debris.particleSystem; system != 0)
		{
			if (catalog.particleNames.size() <= system)
				catalog.particleNames.resize(system + 1);
			if (catalog.particleNames[system].empty())
				catalog.particleNames[system] = std::string(view.ModelName(system));
		}
		if (const std::uint32_t effect = debris.finalEffect; effect != engine::gameplay::DebrisLook::NoEffect)
		{
			if (catalog.effectNames.size() <= effect)
				catalog.effectNames.resize(effect + 1);
			if (catalog.effectNames[effect].empty())
				catalog.effectNames[effect] = std::string(view.DeathEffectName(engine::gameplay::DeathEffectKind::Effect, effect));
		}
	});
}
}
