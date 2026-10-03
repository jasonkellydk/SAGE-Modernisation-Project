export module games.generalszh.content.loading.game_content;
export import games.generalszh.content.effects.bone_fx_content;
export import games.generalszh.content.effects.transition_damage_content;
export import games.generalszh.content.sciences.science_content;
export import games.generalszh.content.ai.ai_data;
export import games.generalszh.content.global.multiplayer_settings;
import std;
export import games.generalszh.content.terrain.bridge_content;
export import games.generalszh.content.terrain.road_content;
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
import games.generalszh.content.containment.transport_content;
export import games.generalszh.content.powers.special_powers;
export import games.generalszh.content.creation.creation_lists;
export import games.generalszh.content.production.production_content;
export import games.generalszh.content.aircraft.aircraft_content;
export import games.generalszh.content.harvesting.harvest_content;
export import games.generalszh.content.global.game_data;
export import games.generalszh.content.global.player_templates;
export import games.generalszh.content.objects.model_states;
export import games.generalszh.content.upgrades.upgrade_content;
export import games.generalszh.content.models.model_rigs;
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
	flight.detonateCallsKill = yes("DetonateCallsKill", false);
	if (const Node *node = ai->block->Find("GarrisonHitKillCount"))
		flight.garrisonHitKill = static_cast<std::uint8_t>(std::clamp<std::int64_t>(engine::config::values::ParseInt(node->Value()).value_or(0), 0, 255));
	const auto kinds = [&](std::string_view key) {
		namespace tc = engine::gameplay::target_class;
		std::uint32_t mask = 0;
		if (const Node *node = ai->block->Find(key))
			for (const std::string_view kind : node->values)
				mask |= kind == "INFANTRY" ? tc::Infantry : kind == "VEHICLE" ? tc::Vehicle : kind == "STRUCTURE" ? tc::Structure : kind == "AIRCRAFT" ? tc::Aircraft : 0u;
		return mask;
	};
	flight.garrisonHitRequired = kinds("GarrisonHitKillRequiredKindOf");
	flight.garrisonHitForbidden = kinds("GarrisonHitKillForbiddenKindOf");
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
		flight.closeEnough = locomotor->closeEnough;
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
	// OrientToFlightPath: TRUE unless the module says otherwise (DumbProjectileBehaviorModuleData's default).
	arc.orientToPath = true;
	if (const engine::config::Node *node = dumb->block->Find("OrientToFlightPath"))
		arc.orientToPath = engine::config::values::ParseBool(node->Value()).value_or(true);
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
	if (const engine::config::Node *node = dumb->block->Find("DetonateCallsKill"))
		arc.callsKill = engine::config::values::ParseBool(node->Value()).value_or(false) ? 1u : 0u;
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
	std::map<std::string, std::vector<Engine::Math::FixedVector3>, std::less<>> garrisonStations; // a fire base's STATION bones, by object
	std::map<std::string, TransportFirePointSet, std::less<>> transportFirePoints; // a transport's FIREPOINT bones, by object
	// TransportContain ExitBone (onRemoving: getPristineBonePositions(name, 0): that bone exactly, in its default model), by object.
	std::map<std::string, RestBone, std::less<>> transportExitBones;
	// OpenContain NumberOfExitPaths and their ExitStart / ExitEnd bones at rest in the default model (a missing bone: the
	// origin, the container's position), per container.
	struct ExitPaths
	{
		std::vector<Engine::Math::FixedVector3> starts;
		std::vector<Engine::Math::FixedVector3> ends;
	};
	std::map<std::string, ExitPaths, std::less<>> transportExitPaths;
	// SpawnPointProductionExitUpdate: its SpawnPointBoneName bones (getPristineBoneTransforms from 1: <name>01, 02, ... while
	// there, at most MAX_SPAWN_POINTS 10), by object; none found: it never has a place free.
	std::map<std::string, std::vector<RestBone>, std::less<>> spawnPoints;
	// BoneFXUpdate: its FX lists, creation lists and particle systems by damage state, at their bones in its default
	// model, by object.
	std::map<std::string, BoneFxContent, std::less<>> boneFx;
	std::map<std::string, TransitionCreations, std::less<>> transitionCreations; // TransitionDamageFX's creation lists
	// computeTrackSpacing: the width of the tracks an object that leaves them makes (its TREADFX01 to TREADFX02 bones
	// plus a track's width of 4; without them 1.4 cells), by object.
	std::map<std::string, Engine::Math::Fixed, std::less<>> trackWidths;
	// GameLOD.ini's StaticGameLOD presets (Low, Medium, High, VeryHigh): StaticGameLODInfo's fields, its defaults where
	// a preset leaves one out (StaticGameLODInfo::StaticGameLODInfo).
	struct GameLod
	{
		std::string name;
		std::uint32_t maxParticleCount{2500};
		bool useShadowVolumes{true};
		bool useShadowDecals{true};
		bool useCloudMap{true};       // cloud shadows over the terrain
		bool useLightMap{true};       // the noise pattern over the terrain
		bool showSoftWaterEdge{true}; // the water's feathered edge
		std::uint32_t maxTankTrackEdges{100};
		std::uint32_t maxTankTrackOpaqueEdges{25};
		std::uint32_t maxTankTrackFadeDelay{300000}; // milliseconds
		bool useBuildupScaffolds{true}; // draw modules below the level skipped when off (m_useDrawModuleLOD)
		bool useTreeSway{true};
		bool useEmissiveNightMaterials{true};
		bool useHeatEffects{true};
		std::int32_t textureReduction{0};
		bool useFpsLimit{true};
		bool enableDynamicLod{true};
		bool useTrees{true};
	};
	std::vector<GameLod> staticLods;
	// Armed objects' PRIMARY barrel counts (from their default model's bones), by object.
	std::map<std::string, std::uint32_t, std::less<>> barrels;
	// Armed objects' PRIMARY launch points and turret pivots (from their default model's bones), by object.
	std::map<std::string, engine::gameplay::LaunchLayout, std::less<>> launchLayouts;
	// HelicopterSlowDeathBehavior BladeBoneName: where on its model (its first state, at rest) its blades come off.
	std::map<std::string, Engine::Math::FixedVector3, std::less<>> bladeBones;
	// ChinookAIUpdate's combat drop ropes (ChinookCombatDropState::onEnter: getPristineBonePositions("RopeStart", 1, ...) and
	// getPristineBoneTransforms("RopeEnd", 1, ...): RopeStart01, 02 ... and RopeEnd01, 02 ... to the first gap, at most
	// 32, on its first state's model at rest), by object.
	struct RopeBones
	{
		std::vector<Engine::Math::FixedVector3> starts;
		std::vector<RestBone> ends;
	};
	std::map<std::string, RopeBones, std::less<>> ropeBones;
	// HelicopterSlowDeathBehavior: the helicopter's NORMAL locomotor (as a hover locomotor), which flies its death spiral.
	std::map<std::string, engine::gameplay::HoverLocomotor, std::less<>> helicopterLocomotors;
	// Locomotor.ini's hover locomotors (ReadHoverLocomotors), for what is derived from objects again (a map's overrides).
	HoverLocomotors hoverLocomotors;
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
	MultiplayerSettings multiplayer; // Multiplayer.ini
	UpgradeCatalog upgrades;     // Upgrade.ini, by bit
	std::vector<std::string> sciences; // Science.ini's sciences, in order (their bits)
	std::vector<ScienceInfo> scienceInfo; // Science.ini, by bit
	std::vector<RankInfo> ranks;          // Rank.ini: rank 1 first
	CrateTemplates crates; // Crate.ini's CrateData, by name
	BridgeCatalog bridges; // Roads.ini's Bridge blocks, by name (with their pristine models' widths)
	RoadCatalog roads;     // Roads.ini's Road blocks, in the order read, with their ids
	// BridgeBehavior's BridgeDieFX / BridgeDieOCL (bones at rest from the object's first model state), by object.
	std::map<std::string, std::vector<BridgeDieEffect>, std::less<>> bridgeDieEffects;
	Anim2DTemplates animations2d; // Animation2D.ini: world and UI flip-book animations, by name
	std::map<std::string, std::string, std::less<>> miscAudio; // MiscAudio.ini: the game's own sounds (CrateMoney = ...), by field
	// The documents the tables above were bound from (kept by the loader), for a map's map.ini overrides to be laid over
	// (WithMapOverrides), and the documents a match's overrides made (they hold the nodes its definitions point into).
	struct Sources
	{
		const engine::config::Document *locomotors{nullptr};
		const engine::config::Document *weapons{nullptr};
		const engine::config::Document *creationLists{nullptr};
		const engine::config::Document *commandButtons{nullptr};
		const engine::config::Document *commandSets{nullptr};
		const engine::config::Document *specialPowers{nullptr};
		const engine::config::Document *sciences{nullptr};
		const engine::config::Document *upgrades{nullptr};
		const engine::config::Document *aiData{nullptr};
		const engine::config::Document *crates{nullptr};
	};
	Sources sources;
	std::vector<std::shared_ptr<const engine::config::Document>> overlays;

	std::optional<std::uint32_t> Science(std::string_view name) const
	{
		const auto found = std::find(sciences.begin(), sciences.end(), name);
		return found != sciences.end() ? std::optional(static_cast<std::uint32_t>(found - sciences.begin())) : std::nullopt;
	}
};

// The tables GameContent derives from one object (its models' bones at rest, its modules), under its name.
void DeriveObjectTables(GameContent &content, const std::string &name, const ObjectDefinition &object, ModelRigs &rigs,
	const engine::time::FixedStep &step, engine::config::BindContext &objectContext)
{
	if (auto layout = ReadParkingLayout(object, rigs))
		content.parking.emplace(name, std::move(*layout));
	else if (auto deck = ReadFlightDeckLayout(object, rigs))
		content.parking.emplace(name, std::move(*deck));
	if (auto layout = ReadDockLayout(object, rigs))
		content.docks.emplace(name, std::move(*layout));
	if (auto points = ReadGarrisonPoints(object, rigs); !points[0].empty() || !points[1].empty() || !points[2].empty())
		content.garrisonPoints.emplace(name, std::move(points));
	if (auto stations = ReadGarrisonStations(object, rigs); !stations.empty())
		content.garrisonStations.emplace(name, std::move(stations));
	if (auto points = ReadTransportFirePoints(object, rigs))
		content.transportFirePoints.emplace(name, std::move(*points));
	if (const ModuleEntry *contain = TransportModule(object); contain != nullptr && contain->type == "TransportContain")
		if (const auto *node = contain->block->Find("ExitBone"); node != nullptr && !node->Value().empty())
			if (const std::string model = DefaultModel(object).model; !model.empty())
				if (const auto bone = rigs.Bone(model, std::string(node->Value())))
					content.transportExitBones.emplace(name, *bone);
	if (const ModuleEntry *contain = ExitPathContain(object); contain != nullptr)
	{
		std::int64_t count = 1; // m_numberOfExitPaths' default
		if (const auto *node = contain->block->Find("NumberOfExitPaths"); node != nullptr && !node->Value().empty())
			count = engine::config::values::ParseInt(node->Value()).value_or(1);
		if (count > 0)
		{
			const std::string model = DefaultModel(object).model;
			const auto at = [&](const std::string &bone) {
				const auto found = model.empty() ? std::nullopt : rigs.Bone(model, bone);
				return found ? found->position : Engine::Math::FixedVector3{};
			};
			GameContent::ExitPaths paths;
			for (std::int64_t path = 1; path <= count; ++path)
			{
				// ExitStart / ExitEnd, or (more than one) ExitStartNN / ExitEndNN: the number two digits under 10.
				const std::string suffix = count == 1 ? std::string{} : (path < 10 ? "0" : "") + std::to_string(path);
				paths.starts.push_back(at("ExitStart" + suffix));
				paths.ends.push_back(at("ExitEnd" + suffix));
			}
			content.transportExitPaths.emplace(name, std::move(paths));
		}
	}
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "SpawnPointProductionExitUpdate")
			continue;
		std::vector<RestBone> bones;
		const auto *node = module.block->Find("SpawnPointBoneName");
		const std::string model = DefaultModel(object).model;
		if (node != nullptr && !model.empty())
			for (int index = 1; index <= 10; ++index)
			{
				char suffix[4];
				std::snprintf(suffix, sizeof(suffix), "%02d", index);
				const auto bone = rigs.Bone(model, std::string(node->Value()) + suffix);
				if (!bone)
					break;
				bones.push_back(*bone);
			}
		content.spawnPoints.emplace(name, std::move(bones));
		break;
	}
	if (auto boneFx = ReadBoneFx(object, rigs, step.TicksPerSecond()))
		content.boneFx.emplace(name, std::move(*boneFx));
	if (auto creations = ReadTransitionCreations(object, rigs))
		content.transitionCreations.emplace(name, std::move(*creations));
	{
		const ModelStates states = ReadModelStates(object);
		if (!states.trackMarks.empty() && !states.Empty())
		{
			Engine::Math::Fixed width = Engine::Math::Fixed::FromRatio(14, 1); // DEFAULT_TRACK_SPACING: MAP_XY_FACTOR * 1.4
			const std::string &model = states.states.front().model;
			const auto left = rigs.Bone(model, "TREADFX01"), right = rigs.Bone(model, "TREADFX02");
			if (left && right)
				width = Engine::Math::Length(right->position - left->position) + Engine::Math::Fixed::FromInt(4);
			content.trackWidths.emplace(name, width);
		}
	}
	if (!object.weaponSets.empty())
	{
		const ModelStates states = ReadModelStates(object);
		if (!states.Empty())
		{
			const ModelState &rest = states.states.front();
			const std::string names[] = {rest.fireFxBone, rest.recoilBone, rest.muzzleBone, rest.launchBone};
			if (!rest.model.empty() && !(names[0].empty() && names[1].empty() && names[2].empty() && names[3].empty()))
			{
				if (const std::uint32_t count = rigs.BarrelCount(rest.model, names); count > 1)
					content.barrels.emplace(name, count);
				content.launchLayouts.emplace(name, ReadLaunchLayout(rest, rigs));
			}
		}
	}
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
	for (const ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.type == "ChinookAIUpdate")
		{
			const ModelStates states = ReadModelStates(object);
			if (states.Empty() || states.states.front().model.empty())
				continue;
			const std::string &model = states.states.front().model;
			GameContent::RopeBones ropes;
			char bone[16];
			for (int index = 1; index <= 32; ++index)
			{
				std::snprintf(bone, sizeof bone, "RopeStart%02d", index);
				const auto found = rigs.Bone(model, bone);
				if (!found)
					break;
				ropes.starts.push_back(found->position);
			}
			for (int index = 1; index <= 32; ++index)
			{
				std::snprintf(bone, sizeof bone, "RopeEnd%02d", index);
				const auto found = rigs.Bone(model, bone);
				if (!found)
					break;
				ropes.ends.push_back(*found);
			}
			content.ropeBones.emplace(name, std::move(ropes));
		}
	if (auto arc = ReadProjectileArc(object, step))
		content.projectileArcs.emplace(name, *arc);
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
	if (auto missile = ReadMissileFlight(object, content.locomotors, objectContext))
		content.missiles.emplace(name, *missile);
	// ParachuteContain::updateBonePositions: the chute's bones in its PARACHUTING model, the rider's PARA_MAN in its
	// FREEFALL and PARACHUTING models (else its geometry's top: getMaxHeightAbovePosition).
	const auto stateModel = [&object](std::string_view condition) {
		const ModelStates states = ReadModelStates(object);
		if (states.Empty())
			return std::string{};
		ConditionBits bits{};
		if (const std::uint32_t bit = ModelConditionBit(condition); bit != NoCondition)
			bits[bit / 64] |= std::uint64_t{1} << (bit % 64);
		return states.states[SelectModelState(states, bits)].model;
	};
	const std::string chuteModel = stateModel("PARACHUTING");
	if (auto parachute = ReadParachute(object, content.hoverLocomotors, step, [&](std::string_view bone) -> std::optional<Engine::Math::FixedVector3> {
			if (const auto found = chuteModel.empty() ? std::nullopt : rigs.Bone(chuteModel, bone))
				return found->position;
			return std::nullopt;
		}))
		content.parachutes.emplace(name, std::move(*parachute));
	if (object.Is("INFANTRY") || object.Is("PARACHUTABLE"))
	{
		const Engine::Math::Fixed top = object.geometry.shape == GeometryShape::Sphere ? object.geometry.majorRadius : object.geometry.height;
		std::array<Engine::Math::FixedVector3, 2> bones{};
		const char *conditions[] = {"FREEFALL", "PARACHUTING"};
		for (std::size_t index = 0; index < bones.size(); ++index)
		{
			const std::string model = stateModel(conditions[index]);
			const auto bone = model.empty() ? std::nullopt : rigs.Bone(model, "PARA_MAN");
			bones[index] = bone ? bone->position : Engine::Math::FixedVector3{Engine::Math::Fixed{}, Engine::Math::Fixed{}, top};
		}
		content.parachuteRiders.emplace(name, bones);
	}
	if (std::ranges::any_of(object.modules, [](const ModuleEntry &module) { return module.type == "HelicopterSlowDeathBehavior"; }))
		for (const engine::config::Node *node : object.locomotorSets)
			if (node != nullptr && node->values.size() >= 2 && node->Value(0) == "SET_NORMAL")
				if (const auto found = content.hoverLocomotors.find(node->Value(1)); found != content.hoverLocomotors.end())
				{
					content.helicopterLocomotors.emplace(name, found->second);
					break;
				}
}

// What DeriveObjectTables put down for the object, taken away (before it is derived again).
void ForgetObjectTables(GameContent &content, const std::string &name)
{
	content.parking.erase(name);
	content.docks.erase(name);
	content.garrisonPoints.erase(name);
	content.garrisonStations.erase(name);
	content.transportFirePoints.erase(name);
	content.transportExitBones.erase(name);
	content.transportExitPaths.erase(name);
	content.spawnPoints.erase(name);
	content.boneFx.erase(name);
	content.transitionCreations.erase(name);
	content.trackWidths.erase(name);
	content.barrels.erase(name);
	content.launchLayouts.erase(name);
	content.bladeBones.erase(name);
	content.ropeBones.erase(name);
	content.projectileArcs.erase(name);
	content.bridgeDieEffects.erase(name);
	content.missiles.erase(name);
	content.parachutes.erase(name);
	content.parachuteRiders.erase(name);
	content.helicopterLocomotors.erase(name);
}

// The special powers' visible payload bones on their transports' models at rest (Drawable::getPristineBonePositions:
// NAME01, NAME02, ...; one missing: the carrier's own position).
void DeriveVisibleRunBones(GameContent &content, ModelRigs &rigs)
{
	for (VisibleRun &run : content.powers.visibleRuns)
	{
		run.bones.clear();
		const ObjectDefinition *transport = content.objects.Find(run.transport);
		const std::string model = transport != nullptr ? DefaultModel(*transport).model : std::string{};
		for (std::int32_t index = 1; index <= run.count; ++index)
		{
			std::optional<Engine::Math::FixedVector3> at;
			if (!model.empty() && !run.dropBone.empty())
			{
				std::string name = run.dropBone;
				name.push_back(static_cast<char>('0' + index / 10 % 10));
				name.push_back(static_cast<char>('0' + index % 10));
				if (const auto bone = rigs.Bone(model, name))
					at = bone->position;
			}
			run.bones.push_back(at);
		}
	}
}

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
	content.sources.aiData = &aiDataSet;
	const engine::config::Document &playerTemplateSet = loader.Load({"Data/INI/Default/PlayerTemplate", "Data/INI/PlayerTemplate"});
	engine::config::BindContext playerTemplateContext{loader.DiagnosticsFor(playerTemplateSet), step};
	content.factionColors = BindFactionColors(playerTemplateSet, playerTemplateContext);
	content.playerTemplates = BindPlayerTemplates(playerTemplateSet, &loader.Load({"Data/INI/ChallengeMode"}), playerTemplateContext);
	const engine::config::Document &multiplayerSet = loader.Load({"Data/INI/Default/Multiplayer", "Data/INI/Multiplayer"});
	engine::config::BindContext multiplayerContext{loader.DiagnosticsFor(multiplayerSet), step};
	content.multiplayer = BindMultiplayerSettings(multiplayerSet, multiplayerContext);
	const engine::config::Document &objectSet = loader.Load({"Data/INI/Default/Object", "Data/INI/Object"});
	const engine::config::Document &crateSet = loader.Load({"Data/INI/Default/Crate", "Data/INI/Crate"});
	engine::config::BindContext objectContext{loader.DiagnosticsFor(objectSet), step};
	const engine::config::Document *documents[] = {&objectSet, &crateSet};
	content.objects = BuildObjectCatalog(documents, objectContext);
	content.crates = BindCrateTemplates(crateSet);
	content.sources.crates = &crateSet;
	content.animations2d = BindAnim2DTemplates(loader.Load({"Data/INI/Animation2D"}), step.TicksPerSecond());
	for (const engine::config::Node &root : loader.Load({"Data/INI/MiscAudio"}).Roots())
		if (root.key == "MiscAudio")
			for (const engine::config::Node &field : root.children)
				if (!field.values.empty())
					content.miscAudio[std::string(field.key)] = std::string(field.Value());
	ModelRigs rigs(loader.Files());
	for (const engine::config::Node &root : loader.Load({"Data/INI/GameLOD"}).Roots())
		if (root.key == "StaticGameLOD" && !root.values.empty())
		{
			GameContent::GameLod lod;
			lod.name = std::string(root.Value());
			for (const engine::config::Node &field : root.children)
			{
				const auto number = [&] { return static_cast<std::uint32_t>(std::max<std::int64_t>(engine::config::values::ParseInt(field.Value()).value_or(0), 0)); };
				const auto flag = [&](bool &target) { target = engine::config::values::ParseBool(field.Value()).value_or(target); };
				if (field.key == "MaxParticleCount")
					lod.maxParticleCount = number();
				else if (field.key == "UseShadowVolumes")
					flag(lod.useShadowVolumes);
				else if (field.key == "UseShadowDecals")
					flag(lod.useShadowDecals);
				else if (field.key == "UseCloudMap")
					flag(lod.useCloudMap);
				else if (field.key == "UseLightMap")
					flag(lod.useLightMap);
				else if (field.key == "ShowSoftWaterEdge")
					flag(lod.showSoftWaterEdge);
				else if (field.key == "MaxTankTrackEdges")
					lod.maxTankTrackEdges = number();
				else if (field.key == "MaxTankTrackOpaqueEdges")
					lod.maxTankTrackOpaqueEdges = number();
				else if (field.key == "MaxTankTrackFadeDelay")
					lod.maxTankTrackFadeDelay = number();
				else if (field.key == "UseBuildupScaffolds")
					flag(lod.useBuildupScaffolds);
				else if (field.key == "UseTreeSway")
					flag(lod.useTreeSway);
				else if (field.key == "UseEmissiveNightMaterials")
					flag(lod.useEmissiveNightMaterials);
				else if (field.key == "UseHeatEffects")
					flag(lod.useHeatEffects);
				else if (field.key == "TextureReductionFactor")
					lod.textureReduction = static_cast<std::int32_t>(engine::config::values::ParseInt(field.Value()).value_or(0));
			}
			content.staticLods.push_back(std::move(lod));
		}
	// Roads.ini's bridges, each with its pristine model's BRIDGE_LEFT width (W3DBridge::load).
	content.bridges = BindBridges(loader.Load({"Data/INI/Roads"}));
	// Roads.ini's roads (TheTerrainRoads reads Data\INI\Default\Roads, then Data\INI\Roads).
	content.roads = BindRoads(loader.Load({"Data/INI/Default/Roads", "Data/INI/Roads"}));
	for (auto &[name, bridge] : content.bridges)
		if (!bridge.models[0].empty())
			bridge.extentY = rigs.MeshExtentY(bridge.models[0], "BRIDGE_LEFT");
	const engine::config::Document &locomotorSet = loader.Load({"Data/INI/Locomotor"});
	engine::config::BindContext locomotorContext{loader.DiagnosticsFor(locomotorSet), step};
	content.locomotors = BuildLocomotorCatalog(locomotorSet, locomotorContext);
	content.wheelTurnAngles = ReadWheelTurnAngles(locomotorSet);
	content.chassisLooks = ReadChassisLooks(locomotorSet);
	content.hoverLocomotors = ReadHoverLocomotors(locomotorSet, step);
	content.sources.locomotors = &locomotorSet;
	// What each object's models and modules give (DeriveObjectTables).
	for (const auto &[name, object] : content.objects)
		DeriveObjectTables(content, name, object, rigs, step, objectContext);

	const engine::config::Document &weaponSet = loader.Load({"Data/INI/Weapon"});
	engine::config::BindContext weaponContext{loader.DiagnosticsFor(weaponSet), step};
	content.weapons = BuildWeaponCatalog(weaponSet, weaponContext);
	content.sources.weapons = &weaponSet;

	const engine::config::Document &armorSet = loader.Load({"Data/INI/Armor"});
	engine::config::BindContext armorContext{loader.DiagnosticsFor(armorSet), step};
	content.armors = BuildArmorCatalog(armorSet, armorContext);
	content.damageFx = BindDamageFx(loader.Load({"Data/INI/DamageFX"}), step);

	const engine::config::Document &creationLists = loader.Load({"Data/INI/ObjectCreationList"});
	const engine::config::Document &commandButtons = loader.Load({"Data/INI/CommandButton"});
	content.powers = BindSpecialPowers(commandButtons, creationLists, step);
	DeriveVisibleRunBones(content, rigs);
	const engine::config::Document &specialPowerSet = loader.Load({"Data/INI/Default/SpecialPower", "Data/INI/SpecialPower"});
	content.powers.templates = BindSpecialPowerTemplates(specialPowerSet, step);
	const engine::config::Document &commandSets = loader.Load({"Data/INI/CommandSet"});
	content.buildLists = BindBuildLists(commandSets, commandButtons);
	content.researchLists = BindResearchLists(commandSets, commandButtons);
	content.commands = BindCommandCatalog(commandSets, commandButtons);
	content.creation = BindCreationLists(creationLists, step);
	const engine::config::Document &scienceSet = loader.Load({"Data/INI/Default/Science", "Data/INI/Science"});
	content.scienceInfo = BindSciences(scienceSet);
	for (const ScienceInfo &science : content.scienceInfo)
		content.sciences.push_back(science.name);
	content.ranks = BindRanks(loader.Load({"Data/INI/Rank"}));
	const engine::config::Document &upgradeSet = loader.Load({"Data/INI/Default/Upgrade", "Data/INI/Upgrade"});
	content.upgrades = BuildUpgradeCatalog(upgradeSet, step.TicksPerSecond());
	content.sources.creationLists = &creationLists;
	content.sources.commandButtons = &commandButtons;
	content.sources.commandSets = &commandSets;
	content.sources.specialPowers = &specialPowerSet;
	content.sources.sciences = &scienceSet;
	content.sources.upgrades = &upgradeSet;
	return content;
}
}
