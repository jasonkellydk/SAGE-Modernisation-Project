export module games.generalszh.content.loading.game_content;
export import games.generalszh.content.sciences.science_content;
export import games.generalszh.content.ai.ai_data;
import std;
export import games.generalszh.content.terrain.bridge_content;
import engine.gameplay.common.spatial.components.targetable;
export import games.generalszh.content.control_bar.command_catalog;

export import games.generalszh.content.loading.content_loader;
export import games.generalszh.content.objects.object_catalog;
export import games.generalszh.content.locomotors.locomotor_catalog;
export import games.generalszh.content.combat.combat_catalog;
export import games.generalszh.content.combat.damage_fx_content;
export import games.generalszh.content.locomotors.chassis_look;
export import games.generalszh.content.crates.crate_content;
export import games.generalszh.content.images.animation_2d;
export import games.generalszh.content.containment.garrison_points;
export import games.generalszh.content.containment.parachute_content;
export import games.generalszh.content.powers.special_powers;
export import games.generalszh.content.creation.creation_lists;
export import games.generalszh.content.production.production_content;
export import games.generalszh.content.aircraft.aircraft_content;
export import games.generalszh.content.harvesting.harvest_content;
export import games.generalszh.content.global.game_data;
export import games.generalszh.content.global.player_templates;
export import games.generalszh.content.objects.model_states;
export import games.generalszh.content.upgrades.upgrade_content;
import games.generalszh.content.models.model_rigs;
import games.generalszh.content.objects.model_conditions;
export import engine.gameplay.rts.combat.resources.launch_layouts;
export import engine.gameplay.common.weapons.definitions.weapon;

// Everything a game session needs from the install's INI, bound once and
// shared by every map played with it (shell map included).
export namespace generalszh::content
{
// validateWeaponBarrelInfo / validateTurretInfo on the model's first state, for the simulation: each
// PRIMARY barrel's launch bone (NAME01, NAME02... while any of its barrel bones is there, else the
// unadorned names; a barrel without a launch bone launches from the origin), the turret's pivots.
inline engine::gameplay::LaunchLayout ReadLaunchLayout(const ModelState &rest, ModelRigs &rigs)
{
	engine::gameplay::LaunchLayout layout;
	const auto has = [&](const std::string &bone) { return !bone.empty() && rigs.Bone(rest.model, bone).has_value(); };
	for (std::size_t slot = 0; slot < 3; ++slot)
	{
		const std::string names[] = {rest.slotFireFxBones[slot], rest.slotRecoilBones[slot], rest.slotMuzzleBones[slot], rest.slotLaunchBones[slot]};
		const std::string &launchBone = rest.slotLaunchBones[slot];
		auto &barrels = layout.barrels[slot];
		for (int index = 1; index <= 99; ++index)
		{
			char suffix[4];
			std::snprintf(suffix, sizeof suffix, "%02d", index);
			bool any = false;
			for (const std::string &name : names)
				any = any || has(name.empty() ? name : name + suffix);
			if (!any)
				break;
			const auto launch = launchBone.empty() ? std::nullopt : rigs.Bone(rest.model, launchBone + suffix);
			barrels.push_back(launch ? launch->position : Engine::Math::FixedVector3{});
		}
		if (barrels.empty())
			if (const auto launch = launchBone.empty() ? std::nullopt : rigs.Bone(rest.model, launchBone))
				barrels.push_back(launch->position);
	}
	if (const auto turret = rest.altTurretBone.empty() ? std::nullopt : rigs.Bone(rest.model, rest.altTurretBone))
		layout.altTurretPivot = turret->position;
	if (const auto pitch = rest.altTurretPitchBone.empty() ? std::nullopt : rigs.Bone(rest.model, rest.altTurretPitchBone))
		layout.altPitchPivot = pitch->position;
	else
		layout.altPitchPivot = layout.altTurretPivot;
	if (const auto turret = rest.turretBone.empty() ? std::nullopt : rigs.Bone(rest.model, rest.turretBone))
		layout.turretPivot = turret->position;
	if (const auto pitch = rest.turretPitchBone.empty() ? std::nullopt : rigs.Bone(rest.model, rest.turretPitchBone))
		layout.pitchPivot = pitch->position;
	else
		layout.pitchPivot = layout.turretPivot;
	return layout;
}

// MissileAIUpdate with the object's SET_NORMAL locomotor and geometry: the missile's flight (the
// original's defaults: follows its target, locks at 75, holds 3 frames once done).
inline std::optional<engine::gameplay::MissileFlightDefinition> ReadMissileFlight(const ObjectDefinition &object,
	const engine::config::DefinitionTable<engine::gameplay::LocomotorDefinition> &locomotors, engine::config::BindContext &context)
{
	const ModuleEntry *ai = nullptr;
	for (const ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.type == "MissileAIUpdate" && (ai == nullptr || (ai->copied && !module.copied)))
			ai = &module;
	if (ai == nullptr)
		return std::nullopt;
	using engine::config::Node;
	engine::gameplay::MissileFlightDefinition flight;
	const auto fixed = [&](std::string_view key, Engine::Math::Fixed fallback) {
		const Node *node = ai->block->Find(key);
		return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(fallback) : fallback;
	};
	const auto yes = [&](std::string_view key, bool fallback) {
		const Node *node = ai->block->Find(key);
		return node != nullptr ? engine::config::values::ParseBool(node->Value()).value_or(fallback) : fallback;
	};
	const auto ticks = [&](std::string_view key, std::uint64_t fallback) {
		const Node *node = ai->block->Find(key);
		return node != nullptr ? engine::config::ReadDurationTicks(*node, context).value_or(fallback) : fallback;
	};
	flight.followsTarget = yes("TryToFollowTarget", true);
	flight.fuelTicks = ticks("FuelLifetime", 0);
	flight.ignitionTicks = ticks("IgnitionDelay", 0);
	flight.initialSpeed = context.step.PerTick(fixed("InitialVelocity", {}));
	flight.noTurnDistance = fixed("DistanceToTravelBeforeTurning", {});
	flight.diveDistance = fixed("DistanceToTargetBeforeDiving", {});
	flight.lockDistance = fixed("DistanceToTargetForLock", Engine::Math::Fixed::FromInt(75));
	flight.useWeaponSpeed = yes("UseWeaponSpeed", false);
	flight.detonateOnNoFuel = yes("DetonateOnNoFuel", false);
	flight.killSelfTicks = ticks("KillSelfDelay", 3);
	flight.jamScatter = fixed("DistanceScatterWhenJammed", Engine::Math::Fixed::FromInt(75));
	if (const engine::gameplay::LocomotorDefinition *locomotor = ObjectLocomotor(object, locomotors))
	{
		flight.maxSpeed = locomotor->maxSpeed;
		flight.minSpeed = locomotor->minSpeed;
		flight.acceleration = locomotor->acceleration;
		flight.braking = locomotor->braking;
		flight.turnRate = locomotor->turnRate;
		flight.maxThrustAngle = locomotor->maxThrustAngle;
		flight.preferredHeight = locomotor->preferredHeight;
		flight.preferredHeightDamping = locomotor->preferredHeightDamping;
	}
	if (object.geometry.majorRadius > Engine::Math::Fixed{})
		flight.radius = object.geometry.majorRadius;
	return flight;
}

// DumbProjectileBehavior's flight path: FirstHeight, SecondHeight, First/SecondPercentIndent, OrientToFlightPath,
// FlightPathAdjustDistPerSecond, MaxLifespan, TumbleRandomly.
inline std::optional<engine::gameplay::ProjectileArc> ReadProjectileArc(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	const ModuleEntry *dumb = nullptr;
	for (const ModuleEntry &module : object.modules)
		if (module.slot == ModuleSlot::Behavior && module.block != nullptr && module.type == "DumbProjectileBehavior" &&
			(dumb == nullptr || (dumb->copied && !module.copied)))
			dumb = &module;
	if (dumb == nullptr)
		return std::nullopt;
	const auto fixed = [&](std::string_view key) {
		const engine::config::Node *node = dumb->block->Find(key);
		if (node == nullptr)
			return Engine::Math::Fixed{};
		std::string_view text = node->Value();
		const bool percent = !text.empty() && text.back() == '%';
		if (percent)
			text.remove_suffix(1);
		const Engine::Math::Fixed value = engine::config::values::ParseFixed(text).value_or(Engine::Math::Fixed{});
		return percent ? value / Engine::Math::Fixed::FromInt(100) : value;
	};
	engine::gameplay::ProjectileArc arc;
	arc.firstHeight = fixed("FirstHeight");
	arc.secondHeight = fixed("SecondHeight");
	arc.firstIndent = fixed("FirstPercentIndent");
	arc.secondIndent = fixed("SecondPercentIndent");
	arc.followPerTick = step.PerTick(fixed("FlightPathAdjustDistPerSecond"));
	if (const engine::config::Node *node = dumb->block->Find("OrientToFlightPath"))
		arc.orientToPath = engine::config::values::ParseBool(node->Value()).value_or(false);
	if (const engine::config::Node *node = dumb->block->Find("TumbleRandomly"); node != nullptr && engine::config::values::ParseBool(node->Value()).value_or(false))
	{
		arc.tumble = true;
		arc.orientToPath = false; // tumbling ones spin instead
	}
	// GarrisonHitKillCount with its kinds (as the target classes they name; NONE: none).
	if (const engine::config::Node *node = dumb->block->Find("GarrisonHitKillCount"))
		arc.garrisonHitKill = static_cast<std::uint8_t>(std::clamp<std::int64_t>(engine::config::values::ParseInt(node->Value()).value_or(0), 0, 255));
	const auto classes = [&](std::string_view key) {
		namespace tc = engine::gameplay::target_class;
		std::uint32_t mask = 0;
		if (const engine::config::Node *node = dumb->block->Find(key))
			for (const std::string_view kind : node->values)
				mask |= kind == "INFANTRY" ? tc::Infantry : kind == "VEHICLE" ? tc::Vehicle : kind == "STRUCTURE" ? tc::Structure : kind == "AIRCRAFT" ? tc::Aircraft : 0u;
		return mask;
	};
	arc.garrisonHitRequired = classes("GarrisonHitKillRequiredKindOf");
	arc.garrisonHitForbidden = classes("GarrisonHitKillForbiddenKindOf");
	// MaxLifespan (parseDurationUnsignedInt: milliseconds, up to whole frames); DEFAULT_MAX_LIFESPAN 10 s.
	const std::uint64_t perSecond = step.TicksPerSecond();
	arc.maxLifespan = 10 * perSecond;
	if (const engine::config::Node *node = dumb->block->Find("MaxLifespan"))
		if (const auto ms = engine::config::values::ParseFixed(node->Value()); ms && *ms >= Engine::Math::Fixed{})
			arc.maxLifespan = static_cast<std::uint64_t>((ms->Floor() * static_cast<std::int64_t>(perSecond) + 999) / 1000);
	return arc;
}

struct GameContent
{
	engine::config::DefinitionTable<ObjectDefinition> objects;
	engine::config::DefinitionTable<engine::gameplay::LocomotorDefinition> locomotors;
	ChassisLooks chassisLooks; // each locomotor's body rocking (the drawable's physics transform), by locomotor
	std::map<std::string, Engine::Math::Fixed, std::less<>> wheelTurnAngles; // FrontWheelTurnAngle (degrees), by locomotor
	engine::config::DefinitionTable<WeaponContent> weapons;
	engine::config::DefinitionTable<engine::gameplay::ArmorDefinition> armors;
	DamageFxTables damageFx; // DamageFX.ini, by name
	SpecialPowerContent powers;
	CreationLists creation;
	BuildLists buildLists; // command set -> what it builds
	BuildLists researchLists; // command set -> the upgrades it researches
	CommandCatalog commands;  // the control bar's buttons and sets (by slot)
	// Airfields' parking places and runways (from their models' bones), by object.
	std::map<std::string, ParkingLayout, std::less<>> parking;
	// Supply docks' points (from their models' bones) and what they are, by object.
	std::map<std::string, DockLayout, std::less<>> docks;
	std::map<std::string, GarrisonPointSets, std::less<>> garrisonPoints; // FIREPOINT bones by damage state, by object
	std::map<std::string, TransportFirePointSet, std::less<>> transportFirePoints; // a transport's FIREPOINT bones, by object
	// computeTrackSpacing: the width of the tracks an object that leaves them makes (its TREADFX01 to TREADFX02 bones
	// plus a track's width of 4; without them 1.4 cells), by object.
	std::map<std::string, Engine::Math::Fixed, std::less<>> trackWidths;
	// GameLOD.ini's StaticGameLOD presets (Low, Medium, High ...): how long and how lasting track marks are.
	struct GameLod
	{
		std::string name;
		std::uint32_t maxTankTrackEdges{100};
		std::uint32_t maxTankTrackOpaqueEdges{25};
		std::uint32_t maxTankTrackFadeDelay{300000}; // milliseconds
	};
	std::vector<GameLod> staticLods;
	// Armed objects' PRIMARY barrel counts (from their default model's bones), by object.
	std::map<std::string, std::uint32_t, std::less<>> barrels;
	// Armed objects' PRIMARY launch points and turret pivots (from their default model's bones), by object.
	std::map<std::string, engine::gameplay::LaunchLayout, std::less<>> launchLayouts;
	// HelicopterSlowDeathBehavior BladeBoneName: where on its model (its first state, at rest) its blades come off.
	std::map<std::string, Engine::Math::FixedVector3, std::less<>> bladeBones;
	// HelicopterSlowDeathBehavior: the helicopter's NORMAL locomotor (as a hover locomotor), which flies its death spiral.
	std::map<std::string, engine::gameplay::HoverLocomotor, std::less<>> helicopterLocomotors;
	// Parachutes (ParachuteContain with their locomotors and model bones), by object.
	std::map<std::string, ParachuteContent, std::less<>> parachutes;
	// Where things hang from a parachute (PARA_MAN at rest in their FREEFALL and PARACHUTING models; without the bone,
	// the top of their geometry), by object, for everything a parachute may hold (INFANTRY or PARACHUTABLE).
	std::map<std::string, std::array<Engine::Math::FixedVector3, 2>, std::less<>> parachuteRiders;
	// Lobbed projectiles' arcs (their DumbProjectileBehavior), by object.
	std::map<std::string, engine::gameplay::ProjectileArc, std::less<>> projectileArcs;
	// Guided missiles' flights (their MissileAIUpdate, THRUST locomotor and geometry), by object.
	std::map<std::string, engine::gameplay::MissileFlightDefinition, std::less<>> missiles;
	GameData gameData; // GameData.ini's globals
	AiData aiData;     // AIData.ini: the computer players' settings, side information and skirmish base plans
	FactionColors factionColors; // PlayerTemplate.ini's preferred colours
	PlayerTemplates playerTemplates; // PlayerTemplate.ini in store order (ChallengeMode.ini's locks)
	UpgradeCatalog upgrades;     // Upgrade.ini, by bit
	std::vector<std::string> sciences; // Science.ini's sciences, in order (their bits)
	std::vector<ScienceInfo> scienceInfo; // Science.ini, by bit
	std::vector<RankInfo> ranks;          // Rank.ini: rank 1 first
	CrateTemplates crates; // Crate.ini's CrateData, by name
	BridgeCatalog bridges; // Roads.ini's Bridge blocks, by name (with their pristine models' widths)
	// BridgeBehavior's BridgeDieFX / BridgeDieOCL (bones at rest from the object's first model state), by object.
	std::map<std::string, std::vector<BridgeDieEffect>, std::less<>> bridgeDieEffects;
	Anim2DTemplates animations2d; // Animation2D.ini: world and UI flip-book animations, by name
	std::map<std::string, std::string, std::less<>> miscAudio; // MiscAudio.ini: the game's own sounds (CrateMoney = ...), by field

	std::optional<std::uint32_t> Science(std::string_view name) const
	{
		const auto found = std::find(sciences.begin(), sciences.end(), name);
		return found != sciences.end() ? std::optional(static_cast<std::uint32_t>(found - sciences.begin())) : std::nullopt;
	}
};

// Loads and binds the content sets (errors land in the loader's diagnostics).
GameContent LoadGameContent(ContentLoader &loader, const engine::time::FixedStep &step)
{
	GameContent content;
	const engine::config::Document &gameDataSet = loader.Load({"Data/INI/Default/GameData", "Data/INI/GameData"});
	engine::config::BindContext gameDataContext{loader.DiagnosticsFor(gameDataSet), step};
	content.gameData = BindGameData(gameDataSet, gameDataContext);
	const engine::config::Document &aiDataSet = loader.Load({"Data/INI/Default/AIData", "Data/INI/AIData"});
	engine::config::BindContext aiDataContext{loader.DiagnosticsFor(aiDataSet), step};
	content.aiData = BindAiData(aiDataSet, aiDataContext);
	const engine::config::Document &playerTemplateSet = loader.Load({"Data/INI/Default/PlayerTemplate", "Data/INI/PlayerTemplate"});
	engine::config::BindContext playerTemplateContext{loader.DiagnosticsFor(playerTemplateSet), step};
	content.factionColors = BindFactionColors(playerTemplateSet, playerTemplateContext);
	content.playerTemplates = BindPlayerTemplates(playerTemplateSet, &loader.Load({"Data/INI/ChallengeMode"}), playerTemplateContext);
	const engine::config::Document &objectSet = loader.Load({"Data/INI/Default/Object", "Data/INI/Object"});
	const engine::config::Document &crateSet = loader.Load({"Data/INI/Default/Crate", "Data/INI/Crate"});
	engine::config::BindContext objectContext{loader.DiagnosticsFor(objectSet), step};
	const engine::config::Document *documents[] = {&objectSet, &crateSet};
	content.objects = BuildObjectCatalog(documents, objectContext);
	content.crates = BindCrateTemplates(crateSet);
	content.animations2d = BindAnim2DTemplates(loader.Load({"Data/INI/Animation2D"}), step.TicksPerSecond());
	for (const engine::config::Node &root : loader.Load({"Data/INI/MiscAudio"}).Roots())
		if (root.key == "MiscAudio")
			for (const engine::config::Node &field : root.children)
				if (!field.values.empty())
					content.miscAudio[std::string(field.key)] = std::string(field.Value());
	ModelRigs rigs(loader.Files());
	for (const auto &[name, object] : content.objects)
		if (auto layout = ReadParkingLayout(object, rigs))
			content.parking.emplace(name, std::move(*layout));
	for (const auto &[name, object] : content.objects)
		if (auto layout = ReadDockLayout(object, rigs))
			content.docks.emplace(name, std::move(*layout));
	for (const auto &[name, object] : content.objects)
		if (auto points = ReadGarrisonPoints(object, rigs); !points[0].empty() || !points[1].empty() || !points[2].empty())
			content.garrisonPoints.emplace(name, std::move(points));
	for (const auto &[name, object] : content.objects)
		if (auto points = ReadTransportFirePoints(object, rigs))
			content.transportFirePoints.emplace(name, std::move(*points));
	for (const auto &[name, object] : content.objects)
	{
		const ModelStates states = ReadModelStates(object);
		if (states.trackMarks.empty() || states.Empty())
			continue;
		Engine::Math::Fixed width = Engine::Math::Fixed::FromRatio(14, 1); // DEFAULT_TRACK_SPACING: MAP_XY_FACTOR * 1.4
		const std::string &model = states.states.front().model;
		const auto left = rigs.Bone(model, "TREADFX01"), right = rigs.Bone(model, "TREADFX02");
		if (left && right)
			width = Engine::Math::Length(right->position - left->position) + Engine::Math::Fixed::FromInt(4);
		content.trackWidths.emplace(name, width);
	}
	for (const engine::config::Node &root : loader.Load({"Data/INI/GameLOD"}).Roots())
		if (root.key == "StaticGameLOD" && !root.values.empty())
		{
			GameContent::GameLod lod;
			lod.name = std::string(root.Value());
			for (const engine::config::Node &field : root.children)
			{
				const auto number = [&] { return static_cast<std::uint32_t>(std::max<std::int64_t>(engine::config::values::ParseInt(field.Value()).value_or(0), 0)); };
				if (field.key == "MaxTankTrackEdges")
					lod.maxTankTrackEdges = number();
				else if (field.key == "MaxTankTrackOpaqueEdges")
					lod.maxTankTrackOpaqueEdges = number();
				else if (field.key == "MaxTankTrackFadeDelay")
					lod.maxTankTrackFadeDelay = number();
			}
			content.staticLods.push_back(std::move(lod));
		}
	for (const auto &[name, object] : content.objects)
		if (!object.weaponSets.empty())
		{
			const ModelStates states = ReadModelStates(object);
			if (states.Empty())
				continue;
			const ModelState &rest = states.states.front();
			const std::string names[] = {rest.fireFxBone, rest.recoilBone, rest.muzzleBone, rest.launchBone};
			if (rest.model.empty() || (names[0].empty() && names[1].empty() && names[2].empty() && names[3].empty()))
				continue;
			if (const std::uint32_t count = rigs.BarrelCount(rest.model, names); count > 1)
				content.barrels.emplace(name, count);
			content.launchLayouts.emplace(name, ReadLaunchLayout(rest, rigs));
		}
	for (const auto &[name, object] : content.objects)
		for (const ModuleEntry &module : object.modules)
			if (module.block != nullptr && module.type == "HelicopterSlowDeathBehavior")
				if (const auto *bone = module.block->Find("BladeBoneName"); bone != nullptr && !bone->Value().empty())
				{
					const ModelStates states = ReadModelStates(object);
					if (states.Empty() || states.states.front().model.empty())
						continue;
					if (const auto found = rigs.Bone(states.states.front().model, std::string(bone->Value())))
						content.bladeBones.emplace(name, found->position);
				}
	for (const auto &[name, object] : content.objects)
		if (auto arc = ReadProjectileArc(object, step))
			content.projectileArcs.emplace(name, *arc);
	// Roads.ini's bridges, each with its pristine model's BRIDGE_LEFT width (W3DBridge::load).
	content.bridges = BindBridges(loader.Load({"Data/INI/Roads"}));
	for (auto &[name, bridge] : content.bridges)
		if (!bridge.models[0].empty())
			bridge.extentY = rigs.MeshExtentY(bridge.models[0], "BRIDGE_LEFT");
	for (const auto &[name, object] : content.objects)
		for (const ModuleEntry &module : object.modules)
		{
			if (module.block == nullptr || module.type != "BridgeBehavior")
				continue;
			std::vector<BridgeDieEffect> effects;
			for (const engine::config::Node &field : module.block->children)
			{
				if (field.key != "BridgeDieFX" && field.key != "BridgeDieOCL")
					continue;
				const std::vector<std::string_view> tokens(field.values.begin(), field.values.end());
				BridgeDieEffect effect;
				effect.creationList = field.key == "BridgeDieOCL";
				const auto what = SubToken(tokens, effect.creationList ? "OCL" : "FX");
				const auto delay = SubToken(tokens, "Delay");
				if (!what || !delay)
					continue;
				effect.name = *what;
				// Milliseconds to ticks, rounded up (INI::parseDurationUnsignedInt).
				const Engine::Math::Fixed ms = engine::config::values::ParseFixed(*delay).value_or(Engine::Math::Fixed{});
				const auto numerator = static_cast<std::uint64_t>(std::max<std::int64_t>(ms.Raw(), 0)) * step.TicksPerSecond();
				const std::uint64_t denominator = std::uint64_t{1000} << Engine::Math::Fixed::FractionBits;
				effect.delayTicks = (numerator + denominator - 1) / denominator;
				if (const auto bone = SubToken(tokens, "Bone"); bone && !bone->empty())
				{
					if (*bone == "ParentObject" && effect.creationList)
						effect.where = BridgeDieEffect::Where::Parent;
					else
					{
						effect.where = BridgeDieEffect::Where::Bone;
						const ModelStates states = ReadModelStates(object);
						if (!states.Empty() && !states.states.front().model.empty())
							if (const auto found = rigs.Bone(states.states.front().model, *bone))
								effect.bone = found->position;
					}
				}
				effects.push_back(std::move(effect));
			}
			if (!effects.empty())
				content.bridgeDieEffects.emplace(name, std::move(effects));
		}

	const engine::config::Document &locomotorSet = loader.Load({"Data/INI/Locomotor"});
	engine::config::BindContext locomotorContext{loader.DiagnosticsFor(locomotorSet), step};
	content.locomotors = BuildLocomotorCatalog(locomotorSet, locomotorContext);
	for (const auto &[name, object] : content.objects)
		if (auto missile = ReadMissileFlight(object, content.locomotors, objectContext))
			content.missiles.emplace(name, *missile);
	content.wheelTurnAngles = ReadWheelTurnAngles(locomotorSet);
	content.chassisLooks = ReadChassisLooks(locomotorSet);
	// ParachuteContain::updateBonePositions: the chute's bones in its PARACHUTING model, the rider's PARA_MAN in its
	// FREEFALL and PARACHUTING models (else its geometry's top: getMaxHeightAbovePosition).
	{
		const HoverLocomotors hovers = ReadHoverLocomotors(locomotorSet, step);
		const auto stateModel = [](const ObjectDefinition &object, std::string_view condition) {
			const ModelStates states = ReadModelStates(object);
			if (states.Empty())
				return std::string{};
			ConditionBits bits{};
			if (const std::uint32_t bit = ModelConditionBit(condition); bit != NoCondition)
				bits[bit / 64] |= std::uint64_t{1} << (bit % 64);
			return states.states[SelectModelState(states, bits)].model;
		};
		for (const auto &[name, object] : content.objects)
		{
			const std::string chuteModel = stateModel(object, "PARACHUTING");
			if (auto parachute = ReadParachute(object, hovers, step, [&](std::string_view bone) -> std::optional<Engine::Math::FixedVector3> {
					if (const auto found = chuteModel.empty() ? std::nullopt : rigs.Bone(chuteModel, bone))
						return found->position;
					return std::nullopt;
				}))
				content.parachutes.emplace(name, std::move(*parachute));
			if (!object.Is("INFANTRY") && !object.Is("PARACHUTABLE"))
				continue;
			const Engine::Math::Fixed top = object.geometry.shape == GeometryShape::Sphere ? object.geometry.majorRadius : object.geometry.height;
			std::array<Engine::Math::FixedVector3, 2> bones{};
			const char *conditions[] = {"FREEFALL", "PARACHUTING"};
			for (std::size_t index = 0; index < bones.size(); ++index)
			{
				const std::string model = stateModel(object, conditions[index]);
				const auto bone = model.empty() ? std::nullopt : rigs.Bone(model, "PARA_MAN");
				bones[index] = bone ? bone->position : Engine::Math::FixedVector3{Engine::Math::Fixed{}, Engine::Math::Fixed{}, top};
			}
			content.parachuteRiders.emplace(name, bones);
		}
		for (const auto &[name, object] : content.objects)
			if (std::ranges::any_of(object.modules, [](const ModuleEntry &module) { return module.type == "HelicopterSlowDeathBehavior"; }))
				for (const engine::config::Node *node : object.locomotorSets)
					if (node != nullptr && node->values.size() >= 2 && node->Value(0) == "SET_NORMAL")
						if (const auto found = hovers.find(node->Value(1)); found != hovers.end())
						{
							content.helicopterLocomotors.emplace(name, found->second);
							break;
						}
	}

	const engine::config::Document &weaponSet = loader.Load({"Data/INI/Weapon"});
	engine::config::BindContext weaponContext{loader.DiagnosticsFor(weaponSet), step};
	content.weapons = BuildWeaponCatalog(weaponSet, weaponContext);

	const engine::config::Document &armorSet = loader.Load({"Data/INI/Armor"});
	engine::config::BindContext armorContext{loader.DiagnosticsFor(armorSet), step};
	content.armors = BuildArmorCatalog(armorSet, armorContext);
	content.damageFx = BindDamageFx(loader.Load({"Data/INI/DamageFX"}), step);

	const engine::config::Document &creationLists = loader.Load({"Data/INI/ObjectCreationList"});
	const engine::config::Document &commandButtons = loader.Load({"Data/INI/CommandButton"});
	content.powers = BindSpecialPowers(commandButtons, creationLists, step);
	content.powers.templates = BindSpecialPowerTemplates(loader.Load({"Data/INI/Default/SpecialPower", "Data/INI/SpecialPower"}), step);
	content.buildLists = BindBuildLists(loader.Load({"Data/INI/CommandSet"}), commandButtons);
	content.researchLists = BindResearchLists(loader.Load({"Data/INI/CommandSet"}), commandButtons);
	content.commands = BindCommandCatalog(loader.Load({"Data/INI/CommandSet"}), commandButtons);
	content.creation = BindCreationLists(creationLists, step);
	content.scienceInfo = BindSciences(loader.Load({"Data/INI/Default/Science", "Data/INI/Science"}));
	for (const ScienceInfo &science : content.scienceInfo)
		content.sciences.push_back(science.name);
	content.ranks = BindRanks(loader.Load({"Data/INI/Rank"}));
	content.upgrades = BuildUpgradeCatalog(loader.Load({"Data/INI/Default/Upgrade", "Data/INI/Upgrade"}), step.TicksPerSecond());
	return content;
}
}
