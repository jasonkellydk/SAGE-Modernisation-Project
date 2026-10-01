export module games.generalszh.presentation.objects.resources.look_catalog;
import std;
export import games.generalszh.content.global.radius_decal;

export import games.generalszh.content.objects.model_states;
export import games.generalszh.content.objects.model_draw;
export import games.generalszh.content.combat.damage_fx_content;
export import games.generalszh.content.effects.bone_fx_content;
export import games.generalszh.presentation.objects.algorithms.chassis_motion;
export import games.generalszh.presentation.effects.damage_effects;
export import games.generalszh.presentation.effects.uplink_looks;
export import games.generalszh.presentation.objects.components.object_presentation;
import engine.ecs.system.system;

// Presentation's catalog of looks (a singleton component, read only while
// frames run; filled between ticks): per definition its model states,
// scale, stealth look and the sounds and trails of its conditions; every
// look (a definition's model in one of its states, or a model shown instead
// of it, such as debris), numbered as first seen, with its model's name;
// each player's colour; and the condition bits presentation reads.
export namespace generalszh::presentation
{
// A map tree's falling and bending (W3DTreeDraw, as the tree buffer uses it): floats and logic frames.
struct TreeMotion
{
	bool doTopple{false};
	bool killWhenToppled{true};
	float initialVelocity{0.2f};
	float initialAcceleration{0.01f};
	float bounceVelocity{0.3f};
	float minimumToppleSpeed{0.5f};
	float sinkFrames{300.0f};
	float sinkDistance{20.0f};
	float framesToMoveOutward{1.0f};
	float framesToMoveInward{1.0f};
	float maxOutwardMovement{1.0f};
	float darkening{0.0f}; // DarkeningFactor: pushed aside, its colour scaled by 1 - this x how far
	std::string texture;   // TextureName: drawn with it in place of its model's own (the tree buffer's)
	std::string toppleFX;
	std::string bounceFX;
};

struct DefinitionLooks
{
	content::ModelStates states;
	// DynamicShroudClearingRangeUpdate's GridDecalTemplate (its grid's pieces; none: no texture).
	content::RadiusDecalLook gridDecal;
	std::vector<std::uint32_t> stateLooks; // look of each state (one when it has none): its first animation's
	std::vector<std::uint32_t> stateVariants; // how many animations each state picks among (its looks follow its first)
	// Its other model draw modules, drawn with it (a bike's rider, a structure's construction scaffold): each in its
	// own model states (W3DModelDraw per module), a look per state's animation as its own draw's.
	struct ExtraDraw
	{
		content::ModelStates states;
		std::vector<std::uint32_t> stateLooks;
		std::vector<std::uint32_t> stateVariants;
		bool policeLights{false};
	};
	std::vector<ExtraDraw> extraDraws;
	// Its own draw's looks (every state's, one after another) and its part override sets (SubObjectsUpgrade: (name,
	// shown) in order); each run of sets an object of it has taken on has a copy of its own looks drawn with them.
	std::uint32_t ownLookFirst{0};
	std::uint32_t ownLookCount{0};
	std::vector<std::vector<std::pair<std::string, bool>>> partSets;
	struct PartVariant
	{
		std::vector<std::uint8_t> applied;
		std::uint32_t firstLook{0};
	};
	std::vector<PartVariant> partVariants;
	// W3DSupplyDraw: its SupplyBonePrefix, how many bones of that numbered family its model has (-1: not known yet), and
	// by how many of them it shows (fewer than all), the first of its own looks copied showing only those (NoLook: not
	// made yet).
	static constexpr std::uint32_t NoLook = 0xFFFFFFFFu;
	std::string supplyBonePrefix;
	// AnimatedParticleSysBoneClientUpdate or W3DModelDraw's ParticlesAttachedToAnimatedBones: its bones' particle
	// systems follow its animation.
	bool animatedParticleBones{false};
	// BeaconClientUpdate: a beacon, its radar pulse at most every `beaconPulseEvery` ticks (RadarPulseFrequency),
	// lasting `beaconPulseFor` (RadarPulseDuration).
	bool beacon{false};
	std::uint64_t beaconPulseEvery{30};
	std::uint64_t beaconPulseFor{15};
	// W3DScienceModelDraw: drawn only for a viewer with its RequiredScience (a known science: its index), or one no longer
	// playing; an unknown science: never drawn.
	bool needsScience{false};
	std::uint32_t requiredScience{0xFFFFFFFFu};
	std::int32_t supplyBones{-1};
	std::vector<std::uint32_t> supplyLooks;
	// updateDrawModuleSupplyStatus: how many of its supply bones show with `current` of `maximum` (ceil in floats, as the
	// original: m_totalBones * (current / (float)max)).
	std::uint32_t SupplyShown(std::uint32_t current, std::uint32_t maximum) const noexcept
	{
		if (supplyBones <= 0)
			return 0;
		if (maximum == 0)
			return static_cast<std::uint32_t>(supplyBones);
		const float share = static_cast<float>(current) / static_cast<float>(maximum);
		const auto shown = static_cast<std::int64_t>(std::ceil(static_cast<float>(supplyBones) * share));
		return static_cast<std::uint32_t>(std::clamp<std::int64_t>(shown, 0, supplyBones));
	}
	// The look it shows for `look` (one of its own) with `shown` of its supply bones (all: as it is).
	std::uint32_t WithSupply(std::uint32_t look, std::uint32_t shown) const noexcept
	{
		if (supplyBones <= 0 || shown >= static_cast<std::uint32_t>(supplyBones) || shown >= supplyLooks.size() || supplyLooks[shown] == NoLook ||
			look < ownLookFirst || look >= ownLookFirst + ownLookCount)
			return look;
		return look - ownLookFirst + supplyLooks[shown];
	}
	// The look an object taken on `applied` shows for `look` (one of its own: that look's copy; others as they are).
	std::uint32_t WithParts(std::uint32_t look, std::span<const std::uint8_t> applied) const noexcept
	{
		if (applied.empty() || look < ownLookFirst || look >= ownLookFirst + ownLookCount)
			return look;
		for (const PartVariant &variant : partVariants)
			if (std::ranges::equal(variant.applied, applied))
				return look - ownLookFirst + variant.firstLook;
		return look;
	}
	RecoilMotion recoil; // its barrels' recoil (W3DModelDraw)
	bool sways{false};   // FloatUpdate: rocks like a buoy on the water
	bool treeSway{false}; // SwayClientUpdate: sways in the breeze
	bool bufferTree{false}; // W3DTreeDraw: a map tree, bent by the tree buffer's breeze
	content::RestingModel resting; // its default draw's model (a definition without condition states draws it)
	// W3DOverlordAircraftDraw / W3DOverlordTankDraw / W3DOverlordTruckDraw: its mounted rider is drawn with its tint.
	bool ridersTakeTint{false};
	TreeMotion treeMotion;    // its falling and bending, when a map tree
	// As it moves (W3DTreeBuffer::unitMoved, for infantry and vehicles that are not immobile): how near it reaches trees
	// (its major radius, or the lesser of a box's two), and whether it topples them (crusher level above 1).
	bool bendsTrees{false};
	float treeReach{0.0f};
	bool topplesTrees{false};
	bool policeLights{false};
	bool mine{false}; // KINDOF_MINE: no heat vision
	// PartitionData::attachToObject: IMMOBILE and not drawn by W3DDefaultDraw, it leaves a ghost object: fogged, it still
	// shows where it is neutral to the viewer or was seen before (not a mine).
	bool ghost{false};
	bool castsShadow{false};
	// Its Shadow: 1 SHADOW_DECAL, 2 SHADOW_VOLUME, 4 SHADOW_PROJECTION (the managers EA's W3DShadowManager::addShadow
	// hands it to draw it only while UseShadowVolumes, resp. UseShadowDecals, is on); 0 a tree buffer's own.
	std::uint8_t shadowKind{0}; // W3DShadowManager::addShadow: its Shadow is exactly SHADOW_DECAL, SHADOW_VOLUME or SHADOW_PROJECTION // W3DPoliceCarDraw: its light bar's clip runs at a quarter frame per 1/30 s
	bool ignoredInGui{false}; // KINDOF_IGNORED_IN_GUI: no promotion feedback
	bool infantry{false};     // KINDOF_INFANTRY: a sinking body casts no shadow (SlowDeathBehavior::beginSlowDeath)
	// W3DDependencyModelDraw AttachToBoneInContainer: mounted on a carrier, it is drawn on this bone of the carrier's model.
	std::string attachToBone;
	bool shrubbery{false};    // KINDOF_SHRUBBERY: burned by a blast's scorch wave, it casts no shadow (doScorchBlast)
	float scale{1.0f};
	// EMPUpdate: an EMP pulse's look. It grows from `empStartScale` toward its rolled target by 5% of the gap each tick,
	// tinted with its start colour saturated (saturateRGB by 2) until it pulses `empFadeTicks` after it was made, then
	// fading to its end colour saturated by 5 until it dies (TintEnvelope attack), held there.
	bool emp{false};
	float empStartScale{1.0f};
	std::uint64_t empFadeTicks{0};
	std::array<float, 3> empStartTint{};
	std::array<float, 3> empEndTint{};
	std::string empSparks;              // its DisableFXParticleSystem: sparks on what it disables
	float empSparksPerCubicFoot{0.001f};
	bool boxFootprint{false}; // its geometry is a box (majorRadius by minorRadius), else a circle of majorRadius
	bool structure{false};    // KINDOF_STRUCTURE
	bool vehicle{false};      // KINDOF_VEHICLE
	bool drone{false};        // KINDOF_DRONE
	bool hugeVehicle{false};  // KINDOF_HUGE_VEHICLE
	bool noHealIcon{false};   // KINDOF_NO_HEAL_ICON
	// A firestorm's particle systems (FirestormDynamicGeometryInfoUpdate ParticleSystem1-16) and ParticleOffsetZ.
	std::vector<std::string> firestormSystems;
	float firestormOffsetZ{0.0f};
	// BoneFXUpdate's particle systems by damage state and slot, and DamageParticleTypes (none: no BoneFXUpdate).
	std::optional<content::BoneFxTable> boneParticles;
	std::uint64_t boneParticleTypes{~std::uint64_t{0}};
	std::string garrisonHitFx; // a projectile's DumbProjectileBehavior GarrisonHitKillFX (on the building it cleared)
	std::optional<content::UplinkLook> uplink; // a Particle Cannon uplink's client effects (ParticleUplinkCannonUpdate)
	float constructionHeight{0.0f};
	bool receivesDynamicLights{true}; // Drawable::getReceivesDynamicLights (ReceivesDynamicLights: No turns it off)
	float lightRadius{0.0f};          // the sphere dynamic lights must reach (its geometry's bounding sphere)
	static constexpr std::uint32_t NoTrack = 0xFFFFFFFFu;
	std::uint32_t trackTexture{NoTrack}; // TrackMarks: an index of the catalog's track textures (none: it leaves no tracks)
	float trackWidth{0.0f};              // computeTrackSpacing // its geometry's top (getMaxHeightAbovePosition): how far it sinks unbuilt
	// Drawable::getShouldAnimate: its animations pause while hacked, paralyzed, EMPed, subdued or unmanned
	// (and underpowered when its draw module requires power), unless it is PRODUCED_AT_HELIPAD.
	bool animatesWhileDisabled{false};
	bool animationsRequirePower{true};
	// Stealthed, as friends see it: opacity pulsing between these.
	bool stealth{false};
	float stealthMin{0.5f};
	float stealthMax{1.0f};
	float stealthPulseTicks{30.0f};
	std::string ambientSound; // SoundAmbient
	// By damage state (Drawable::getAmbientSoundByDamage): SoundAmbientDamaged, SoundAmbientReallyDamaged (either
	// falling back to SoundAmbient), SoundAmbientRubble; and a state's one-shot (SoundOnDamaged, SoundOnReallyDamaged).
	std::string ambientDamaged, ambientReallyDamaged, ambientRubble;
	std::string onDamaged, onReallyDamaged;
	std::string moveStart;    // SoundMoveStart
	std::string moveLoop;     // SoundMoveLoop
	std::string moveStartDamaged; // SoundMoveStartDamaged
	std::string moveLoopDamaged;  // SoundMoveLoopDamaged
	std::string stealthOn;  // SoundStealthOn
	std::string afterburnerSound; // UnitSpecificSounds Afterburner (JetAIUpdate's, while its afterburners burn)
	std::string lowFuelVoice;     // UnitSpecificSounds VoiceLowFuel (circling a dead airfield)
	std::string rapidFireVoice;   // UnitSpecificSounds VoiceRapidFire (FiringTracker::speedUp to CONTINUOUS_FIRE_FAST)
	std::string trainRunningSound; // a locomotive's RailroadBehavior RunningSound
	std::string stealthOff; // SoundStealthOff
	std::array<std::string, 3> promotedSounds; // SoundPromotedVeteran, SoundPromotedElite, SoundPromotedHero
	std::string turretLoop;   // TurretMoveLoop (UnitSpecificSounds), while its turret turns
	std::string burningSound; // FlammableUpdate, while aflame
	std::string crashSound;   // a crash death's loop, while falling
	std::vector<content::ModelState::ParticleBone> crashTrail; // a crashing helicopter's smoke
	DamageEffects damage; // TransitionDamageFX
	std::string ignitionFX; // MissileAIUpdate: its motor lighting
	std::string suppliesDepletedVoice; // SupplyTruckAIUpdate: a warehouse emptied
	// Its body rocking (its SET_NORMAL locomotor's calcPhysicsXform values) and its geometry's radii.
	std::optional<ChassisTuning> chassis;
	float majorRadius{0.0f};
	float minorRadius{0.0f};
	std::string toppleFX; // ToppleUpdate: falling over
	std::string bounceFX; // and bouncing where it lands
	// A stealth detector's scans (StealthDetectorUpdate), shown and heard.
	struct DetectorLook
	{
		std::string ping, brightPing, beacon, grid, bone, pingSound, loudPingSound;
	};
	std::optional<DetectorLook> detector;
	// Its ArmorSets' DamageFX (ActiveBody::doDamageFX), by armor; the set with no conditions first.
	struct HitFx
	{
		std::string armor;
		const content::DamageFxTable *table{nullptr};
	};
	std::vector<HitFx> hitFx;

	// The DamageFX of the armor set in use: the one of that armor, else the one with no conditions.
	const content::DamageFxTable *HitFxFor(std::string_view armor) const noexcept
	{
		for (const HitFx &set : hitFx)
			if (set.armor == armor)
				return set.table;
		return hitFx.empty() ? nullptr : hitFx.front().table;
	}
};

// Each look's animation clip as the renderer loaded it (its frames and their rate), for telling when a clip
// played once has finished (W3D's Is_Animation_Complete): frames below zero while not known yet, zero for a
// look without an animation (never finishing). Filled in by the host as models load.
struct LookClip
{
	float frames{-1.0f};
	float rate{0.0f};

	bool Known() const noexcept { return frames >= 0.0f; }
};

struct LookClips
{
	std::vector<LookClip> byLook;

	LookClip At(std::uint32_t look) const noexcept { return look < byLook.size() ? byLook[look] : LookClip{}; }
};

struct LookEntry
{
	std::uint32_t definition{0};
	std::uint32_t state{0};
	std::uint32_t model{0}; // a model shown instead of the definition's (0: its own)
	std::uint32_t draw{0};  // its draw module: 0 the definition's own, n its n-th extra draw
	std::uint32_t variant{0}; // which of its state's animations (a state with several has a look for each)
	std::uint32_t parts{0};   // its part overrides (LookCatalog::partOverrides; 0: none)
	// A model shown instead of a definition's: the animation it plays (a model-name id, 0 none) and how (an
	// ObjectAnimationMode).
	std::uint32_t animation{0};
	std::uint8_t mode{0};
};

struct LookBits
{
	std::uint32_t firing{0};
	std::uint32_t stealthed{0};
	std::uint32_t detected{0};
	std::uint32_t dying{0};
	std::uint32_t aflame{0};
	std::uint32_t specialDamaged{0};
	std::uint32_t damaged{0};
	std::uint32_t reallyDamaged{0};
	std::uint32_t rubble{0};
	std::uint32_t night{0};
	std::uint32_t afterburner{0}; // JETAFTERBURNER
	std::uint32_t burned{0};
	std::uint32_t toppled{0};
};

struct LookCatalog
{
	std::vector<std::string> trackTextures; // the TrackMarks textures, by index
	std::vector<DefinitionLooks> byDefinition;
	std::vector<std::uint8_t> known;
	std::vector<LookEntry> looks;
	std::vector<std::string> lookModels; // each look's model name
	std::vector<std::string> lookAnimations; // each look's animation name, for a model shown instead of its own
	std::vector<std::pair<std::uint64_t, std::uint32_t>> modelLooks; // (ModelKey of a model override, look), sorted
	std::vector<std::string> effectNames; // FX lists by death effect id, as debris pieces play them as they land
	std::vector<std::string> particleNames; // particle systems riding on debris pieces, by name id
	std::vector<std::array<float, 4>> playerColors;
	std::vector<std::string> armorNames; // by Health::armor (plain: empty)
	std::vector<std::vector<std::pair<std::string, bool>>> partOverrides{{}}; // LookEntry::parts: (name, shown) in order
	std::string crateSalvageSound; // MiscAudio CrateSalvage
	std::string crateMoneySound;   // MiscAudio CrateMoney
	std::string crateFreeUnitSound; // MiscAudio CrateFreeUnit
	std::string crateHealSound;     // MiscAudio CrateHeal
	std::string crateShroudSound;   // MiscAudio CrateShroud
	std::string unitPromotedSound; // MiscAudio UnitPromoted
	std::string buildingDisabledSound; // MiscAudio BuildingDisabled
	std::string vehicleDisabledSound;  // MiscAudio VehicleDisabled
	std::string buildingReenabledSound; // MiscAudio BuildingReenabled
	std::string vehicleReenabledSound;  // MiscAudio VehicleReenabled
	std::string pilotSplatterSound;    // MiscAudio SplatterVehiclePilotsBrain
	std::string defectorTickSound;     // MiscAudio DefectorTimerTickSound
	std::string defectorDingSound;     // MiscAudio DefectorTimerDingSound
	// GameData's SelectionFlashHouseColor and SelectionFlashSaturationFactor (flashAsSelected without a colour).
	bool selectionFlashHouseColor{false};
	float selectionFlashSaturation{0.5f};
	// GameData's promotion animation: LevelGainAnimationName, LevelGainAnimationTime (s), LevelGainAnimationZRise (a second).
	std::string levelGainAnimation;
	float levelGainSeconds{0.0f};
	float levelGainRise{0.0f};
	LookBits bits;
	// The time of day is night and models follow it (GameData's
	// ForceModelsToFollowTimeOfDay): every object shows its NIGHT condition.
	bool night{false};

	const DefinitionLooks *Of(std::uint32_t definition) const noexcept
	{
		return definition < byDefinition.size() && known[definition] != 0 ? &byDefinition[definition] : nullptr;
	}

	// Looks of a model alone, not any definition's (the move hint: InGameUI's MoveHintName render object): LookEntry::model
	// is BareModel, its model and animation the look's lookModels / lookAnimations.
	static constexpr std::uint32_t BareModel = 0xFFFFFFFFu;
	static constexpr std::uint32_t NoLook = 0xFFFFFFFFu;
	std::uint32_t moveHintLook{NoLook};

	// A model shown instead of a definition's, playing an animation (0 none) in a mode.
	static constexpr std::uint64_t ModelKey(std::uint32_t model, std::uint32_t animation = 0, std::uint8_t mode = 0) noexcept
	{
		return (static_cast<std::uint64_t>(animation) << 36) | (static_cast<std::uint64_t>(mode & 0xFu) << 32) | model;
	}
	// The look of a model shown instead of a definition's; the look count when unknown.
	std::uint32_t LookOfModel(std::uint32_t model, std::uint32_t animation = 0, std::uint8_t mode = 0) const noexcept
	{
		const std::uint64_t key = ModelKey(model, animation, mode);
		const auto found = std::lower_bound(modelLooks.begin(), modelLooks.end(), std::pair{key, 0u},
			[](const auto &a, const auto &b) { return a.first < b.first; });
		return found != modelLooks.end() && found->first == key ? found->second : static_cast<std::uint32_t>(looks.size());
	}
	std::string_view ParticleName(std::uint32_t id) const noexcept { return id < particleNames.size() ? std::string_view(particleNames[id]) : std::string_view{}; }
	std::string_view EffectName(std::uint32_t id) const noexcept { return id < effectNames.size() ? std::string_view(effectNames[id]) : std::string_view{}; }

	std::string_view ArmorName(std::uint32_t armor) const noexcept { return armor < armorNames.size() ? std::string_view(armorNames[armor]) : std::string_view{}; }

	std::array<float, 4> ColorOf(std::uint32_t player) const noexcept
	{
		return player < playerColors.size() ? playerColors[player] : std::array<float, 4>{1, 1, 1, 0};
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::LookClips>
{
	static constexpr std::string_view StableName = "generalszh.presentation.look_clips";
};
template<>
struct ResourceTraits<generalszh::presentation::LookCatalog>
{
	static constexpr std::string_view StableName = "generalszh.presentation.look_catalog";
};
}
