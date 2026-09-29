export module games.generalszh.presentation.objects.algorithms.look_setup;
import std;
import games.generalszh.presentation.objects.components.tree_bend;
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
	world.RegisterComponent<TreeSway>();
	world.RegisterComponent<HeatVision>();
	world.RegisterComponent<DisableHeard>();
	world.RegisterComponent<ObjectIcons>();
	world.RegisterComponent<TintEnvelope>();
	world.RegisterComponent<SelectionFlash>();
	world.RegisterComponent<ScriptFlash>();
	world.RegisterComponent<CaptureFlash>();
	world.RegisterComponent<DefectorFlash>();
	world.RegisterComponent<ShroudSight>();
	world.RegisterComponent<DebrisMotion>();
	world.RegisterComponent<WeaponPose>();
	world.RegisterComponent<BarrelRecoil>();
	world.RegisterComponent<ConditionEmission>();
	world.RegisterComponent<CrashTrailEmission>();
	world.RegisterComponent<FxEmission>();
	world.RegisterComponent<DamageEmission>();
	world.RegisterComponent<ExhaustState>();
	world.RegisterComponent<HitFxThrottle>();
	world.RegisterComponent<ExhaustEmission>();
	world.RegisterComponent<TrackMarks>();
	world.RegisterComponent<TreeBend>();
	world.RegisterComponent<UplinkEffects>();
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
	looks.constructionHeight = Engine::Math::ToFloat(object.geometry.shape == content::GeometryShape::Sphere ? object.geometry.majorRadius : object.geometry.height);
	looks.animatesWhileDisabled = object.Is("PRODUCED_AT_HELIPAD");
	looks.ignoredInGui = object.Is("IGNORED_IN_GUI");
	looks.infantry = object.Is("INFANTRY");
	looks.shrubbery = object.Is("SHRUBBERY");
	looks.boxFootprint = object.geometry.shape == content::GeometryShape::Box;
	looks.structure = object.Is("STRUCTURE");
	looks.vehicle = object.Is("VEHICLE");
	looks.drone = object.Is("DRONE");
	looks.hugeVehicle = object.Is("HUGE_VEHICLE");
	looks.noHealIcon = object.Is("NO_HEAL_ICON");
	for (const content::ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.type == "DumbProjectileBehavior")
			if (const auto *fx = module.block->Find("GarrisonHitKillFX"); fx != nullptr && fx->Value() != "None")
				looks.garrisonHitFx = std::string(fx->Value());
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
	looks.bufferTree = content::DefaultModel(object).drawType == "W3DTreeDraw";
	{
		const std::string &draw = content::DefaultModel(object).drawType;
		looks.ridersTakeTint = draw == "W3DOverlordAircraftDraw" || draw == "W3DOverlordTankDraw" || draw == "W3DOverlordTruckDraw";
	}
	// A tree-buffer tree casts its shadow by its W3DTreeDraw's DoShadow (W3DTreeBuffer), not by the object's Shadow.
	if (looks.bufferTree)
		looks.castsShadow = content::ReadTreeDraw(object).value_or(content::TreeDrawMotion{}).doShadow;
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
			catalog.looks.push_back({definition, state, 0, draw});
			catalog.lookModels.push_back(extra.states.states[state].model);
			extra.stateLooks.push_back(static_cast<std::uint32_t>(catalog.looks.size() - 1));
		}
		looks.extraDraws.push_back(std::move(extra));
	}
	const engine::time::FixedStep step{30};
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
	looks.rapidFireVoice = std::string(object.Sound("VoiceRapidFire"));
	looks.mine = object.Is("MINE");
	looks.ghost = object.Is("IMMOBILE") &&
		std::none_of(object.modules.begin(), object.modules.end(), [](const content::ModuleEntry &module) { return module.type == "W3DDefaultDraw"; });
	looks.stealthOff = std::string(object.Sound("SoundStealthOff"));
	looks.promotedSounds = {std::string(object.Sound("SoundPromotedVeteran")), std::string(object.Sound("SoundPromotedElite")),
		std::string(object.Sound("SoundPromotedHero"))};
	looks.turretLoop = std::string(object.Sound("TurretMoveLoop"));
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

void KnowLooks(LookCatalog &catalog, const session::SessionView &view)
{
	namespace mc = content::model_condition;
	catalog.bits = {mc::FiringA, mc::StealthedLook, mc::DetectedLook, mc::Dying, mc::Aflame, mc::SpecialDamaged, mc::Damaged, mc::ReallyDamaged, mc::Rubble, mc::Night, mc::JetAfterburner, mc::Burned, mc::Toppled};
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
	if (const auto found = misc.find("CrateSalvage"); found != misc.end())
		catalog.crateSalvageSound = found->second;
	if (const auto found = misc.find("CrateMoney"); found != misc.end())
		catalog.crateMoneySound = found->second;
	if (const auto found = misc.find("CrateFreeUnit"); found != misc.end())
		catalog.crateFreeUnitSound = found->second;
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
	const content::GameData &data = view.Content().gameData;
	catalog.selectionFlashHouseColor = data.selectionFlashHouseColor;
	catalog.selectionFlashSaturation = Engine::Math::ToFloat(data.selectionFlashSaturationFactor);
	catalog.levelGainAnimation = data.levelGainAnimation;
	catalog.levelGainSeconds = Engine::Math::ToFloat(data.levelGainSeconds);
	catalog.levelGainRise = Engine::Math::ToFloat(data.levelGainRise);
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
