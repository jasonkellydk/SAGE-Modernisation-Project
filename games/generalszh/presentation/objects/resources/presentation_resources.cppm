export module games.generalszh.presentation.objects.resources.presentation_resources;
import std;

export import engine.ecs.core.entity;
export import engine.effects.particles.simulation.particle_world;
export import games.generalszh.presentation.effects.effects_content;
export import games.generalszh.presentation.effects.fx_playback;
export import games.generalszh.presentation.effects.laser_looks;
import engine.ecs.system.system;

// Presentation's singleton components (world resources, never hashed or
// saved): this frame's game time; what each definition looks like in motion
// (read from the content once, when the definition is first seen); and the
// particle world the emitters run in.
export namespace generalszh::presentation
{
struct PresentationFrame
{
	float seconds{0.0f};     // game time this frame (real time scaled by the game speed)
	float realSeconds{0.0f}; // real time this frame (what the original's per-render-frame updates count: dynamic lights)
	double clock{0.0};       // the presentation clock: game time so far
	float alpha{0.0f};       // how far this frame is between the last two ticks
	std::uint32_t frame{0};  // counts presentation frames
	std::uint64_t tick{0};   // the simulation's last tick (alpha runs from the one before it to it)
	// GameLogic::getDrawIconUI (scripts: OPTIONS_SET_DRAWICON_UI_MODE): promotions and crate pickups show their
	// world animations and play their feedback sounds only while it is on.
	bool drawIconUi{true};
	// A script's screen fade runs (ScriptEngine::getFade not FADE_NONE): Drawable::drawIconUI draws nothing then.
	bool scriptFade{false};
	// Whose eyes the world is seen through (the local player; NoViewer: nobody's in particular).
	static constexpr std::uint32_t NoViewer = 0xFFFFFFFFu;
	std::uint32_t viewer{NoViewer};
};

// Terrain tracks (GameData MakeTrackMarks / MaxTerrainTracks; the detail level's GameLOD MaxTankTrackEdges,
// MaxTankTrackOpaqueEdges and MaxTankTrackFadeDelay).
struct TrackSettings
{
	bool make{false};
	std::uint32_t maxTracks{0};
	std::uint32_t maxEdges{100};
	std::uint32_t maxOpaqueEdges{25};
	std::uint32_t fadeMilliseconds{300000};
};

// How one definition moves visibly (W3DTankDraw treads, W3DTruckDraw tires,
// the debris and dust it kicks up).
struct MotionLook
{
	float treadRate{0.0f}; // texture units a second; 0: no treads
	float treadPivot{0.0f};
	float treadDrive{0.0f};
	float maxSpeed{0.0f}; // top speed per tick (its normal locomotor)
	float wheelMultiplier{0.0f};
	std::vector<std::string> wheelBones; // empty: no tires to turn
	std::vector<std::string> steeredBones; // the front tires, which also steer
	std::vector<std::uint8_t> wheelCorners; // each tire's corner (0 front left .. 3 rear right)
	float wheelTurn{0.0f};               // how far the front wheels steer (radians; the locomotor's FrontWheelTurnAngle)
	std::string cabBone;
	std::string trailerBone;
	float cabFactor{0.0f};
	float trailerFactor{0.0f};
	float swingDamping{0.0f}; // share of the way the cab and trailer swing to their angle each 30th of a second
	std::vector<std::string> motionSystems;
	std::vector<std::uint8_t> motionRoles; // each system's content::MotionEmitterRole
	bool truck{false};             // W3DTruckDraw: its wheel emitters follow its motion as a truck's
	float powerslideAddition{0.0f}; // PowerslideRotationAddition
	std::string landingSound;      // TruckLandingSound
	std::string powerslideSound;   // TruckPowerslideSound

	bool Moves() const noexcept { return treadRate > 0.0f || !wheelBones.empty() || !motionSystems.empty(); }
};

struct MotionLooks
{
	std::vector<MotionLook> byDefinition;
	std::vector<std::uint8_t> known; // read from the content yet
	std::uint32_t dyingBit{0};       // the appearance bit of the dying (the game's DYING condition)

	const MotionLook *Of(std::uint32_t definition) const noexcept
	{
		return definition < byDefinition.size() && known[definition] != 0 ? &byDefinition[definition] : nullptr;
	}
};


// Each weapon's looping FireSound (by weapon index; empty: none, or its sound plays a shot at a time), kept up with the
// weapons in play (FiringTracker's looping fire sounds).
struct WeaponFireLoops
{
	std::vector<std::string> byWeapon;

	std::string_view Of(std::uint32_t weapon) const noexcept
	{
		return weapon < byWeapon.size() ? std::string_view(byWeapon[weapon]) : std::string_view{};
	}
};

// Each weapon's WeaponRecoil in radians (by weapon index; 0: none), kept up with the weapons in play.
struct WeaponRecoils
{
	std::vector<float> byWeapon;

	float Of(std::uint32_t weapon) const noexcept { return weapon < byWeapon.size() ? byWeapon[weapon] : 0.0f; }
};

// Each weapon's ProjectileExhaust by its firer's veterancy level (by weapon index; empty: none), kept up with the
// weapons in play.
struct WeaponExhausts
{
	std::vector<std::array<std::string, 4>> byWeapon;

	std::string_view Of(std::uint32_t weapon, std::uint8_t level) const noexcept
	{
		return weapon < byWeapon.size() ? std::string_view(byWeapon[weapon][std::min<std::size_t>(level, 3)]) : std::string_view{};
	}
};

// Each weapon's laser (LaserName's beam and its LaserBoneName; by weapon index), kept up with the weapons in play.
struct WeaponLaser
{
	bool valid{false};
	content::LaserLook look;
	std::string bone;
};

struct WeaponLasers
{
	std::vector<WeaponLaser> byWeapon;
	// Laser objects drawn without a weapon (AssistedTargetingUpdate's data streams), by name, with ids from ObjectBase.
	static constexpr std::uint32_t ObjectBase = 0x80000000u;
	std::vector<std::pair<std::string, WeaponLaser>> byObject;

	const WeaponLaser *Of(std::uint32_t weapon) const noexcept
	{
		if (weapon >= ObjectBase)
			return weapon - ObjectBase < byObject.size() && byObject[weapon - ObjectBase].second.valid ? &byObject[weapon - ObjectBase].second : nullptr;
		return weapon < byWeapon.size() && byWeapon[weapon].valid ? &byWeapon[weapon] : nullptr;
	}
};

// The weapons' projectile streams (ProjectileStreamName's W3DProjectileStreamDraw), by weapon index; none: no stream.
struct WeaponStreams
{
	std::vector<std::optional<content::StreamLook>> byWeapon;

	const content::StreamLook *Of(std::uint32_t weapon) const noexcept
	{
		return weapon < byWeapon.size() && byWeapon[weapon] ? &*byWeapon[weapon] : nullptr;
	}
};

// Lasers asked for this tick: a laser weapon fired by `source` at `target` (or `end`).
struct LaserRequest
{
	std::uint32_t weapon{0};
	ecs::Entity source;
	ecs::Entity target;
	std::array<float, 3> end{};
	bool atTarget{false}; // ends where its target is as it starts (an assisted targeting stream), not at `end`
};

struct LaserRequests
{
	std::vector<LaserRequest> pending;
};

// A laser beam showing (LaserUpdate): from its source's laser bone to its target, for its lifetime.
struct ActiveLaser
{
	std::uint32_t weapon{0};
	ecs::Entity source;
	ecs::Entity target;
	std::array<float, 3> start{};
	std::array<float, 3> end{};
	float age{0.0f};
	float lifetime{0.0f}; // seconds (its LifetimeUpdate's pick)
	std::uint64_t muzzle{0};
	std::uint64_t impact{0};
};

struct ActiveLasers
{
	std::vector<ActiveLaser> lasers;
};

// This frame's laser beams as W3DLaserDraw draws them (one per beam per segment), for the renderer.
struct LaserBeamDraw
{
	std::array<float, 3> start{};
	std::array<float, 3> end{};
	float width{0.0f};
	std::array<float, 4> color{};
	std::string_view texture;
	float uvScale{1.0f};
	float uvOffset{0.0f};
};

struct LaserFrame
{
	std::vector<LaserBeamDraw> beams;
};

// Where a model's bone sits at rest (from the renderer's loaded models).
struct BoneLookup
{
	bool ready{false}; // the model has loaded (else: ask again later)
	bool found{false};
	std::array<float, 3> position{};
	float yaw{0.0f};
};

struct BonePoses
{
	std::function<BoneLookup(std::string_view model, std::string_view bone)> pose;
	// Where a bone (or each of its numbered family: Smoke01, Smoke02, ...) sits; empty when not known (yet).
	std::function<std::vector<std::array<float, 3>>(std::string_view model, std::string_view bone, bool family)> locate;
	// Whether `bone` hangs below `ancestor` in the model's hierarchy (false when either is unknown).
	std::function<bool(std::string_view model, std::string_view bone, std::string_view ancestor)> descends;
	// Where a bone sits in a look's model `seconds` into its animation (started `start` of the way in): the model's
	// animated pose (W3DModelDraw::updateBonesForClientParticleSystems: Get_Bone_Transform). Not found: its rest pose.
	std::function<BoneLookup(std::uint32_t look, float seconds, float start, std::string_view bone)> animated;
	// A bone's whole transform in its model (row-major 3x4) `seconds` into the look's animation; none while the model
	// loads or when it has no such bone.
	std::function<std::optional<std::array<float, 12>>(std::uint32_t look, float seconds, float start, std::string_view bone)> transform;
};

// Effects asked for this frame (FX lists to play; the sounds and camera
// shakes they call for), drained by the systems that carry them out.
struct FxRequests
{
	std::vector<FxRequest> pending;
};

struct SoundRequests
{
	std::vector<SoundRequest> pending;
};

struct ShakeRequests
{
	std::vector<ShakeRequest> pending;
};

// Presentation's own randomness (never the simulation's).
struct PresentationRandom
{
	std::mt19937 engine{0xF0F0u};
	std::uint32_t pick{0x9E3779B9u};
};

struct TerrainHeightHandle
{
	GroundHeightAt at;
};

struct EffectStats
{
	std::uint64_t fxPlayed{0};
	std::uint64_t hurt{0};             // objects seen getting worse than pristine
	std::uint64_t hurtWithEffects{0};  // of those, ones whose definition has effects for the state
};

struct ParticleWorldHandle
{
	engine::effects::ParticleWorld *world{nullptr};
	const EffectsContent *content{nullptr};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::TrackSettings>
{
	static constexpr std::string_view StableName = "generalszh.presentation.track_settings";
};
template<>
struct ResourceTraits<generalszh::presentation::PresentationFrame>
{
	static constexpr std::string_view StableName = "generalszh.presentation.frame";
};
template<>
struct ResourceTraits<generalszh::presentation::MotionLooks>
{
	static constexpr std::string_view StableName = "generalszh.presentation.motion_looks";
};
template<>
struct ResourceTraits<generalszh::presentation::FxRequests>
{
	static constexpr std::string_view StableName = "generalszh.presentation.fx_requests";
};
template<>
struct ResourceTraits<generalszh::presentation::SoundRequests>
{
	static constexpr std::string_view StableName = "generalszh.presentation.sound_requests";
};
template<>
struct ResourceTraits<generalszh::presentation::ShakeRequests>
{
	static constexpr std::string_view StableName = "generalszh.presentation.shake_requests";
};
template<>
struct ResourceTraits<generalszh::presentation::PresentationRandom>
{
	static constexpr std::string_view StableName = "generalszh.presentation.random";
};
template<>
struct ResourceTraits<generalszh::presentation::TerrainHeightHandle>
{
	static constexpr std::string_view StableName = "generalszh.presentation.terrain_height";
};
template<>
struct ResourceTraits<generalszh::presentation::EffectStats>
{
	static constexpr std::string_view StableName = "generalszh.presentation.effect_stats";
};
template<>
struct ResourceTraits<generalszh::presentation::WeaponLasers>
{
	static constexpr std::string_view StableName = "generalszh.presentation.weapon_lasers";
};
template<>
struct ResourceTraits<generalszh::presentation::WeaponStreams>
{
	static constexpr std::string_view StableName = "generalszh.presentation.weapon_streams";
};
template<>
struct ResourceTraits<generalszh::presentation::LaserRequests>
{
	static constexpr std::string_view StableName = "generalszh.presentation.laser_requests";
};
template<>
struct ResourceTraits<generalszh::presentation::ActiveLasers>
{
	static constexpr std::string_view StableName = "generalszh.presentation.active_lasers";
};
template<>
struct ResourceTraits<generalszh::presentation::LaserFrame>
{
	static constexpr std::string_view StableName = "generalszh.presentation.laser_frame";
};
template<>
struct ResourceTraits<generalszh::presentation::WeaponRecoils>
{
	static constexpr std::string_view StableName = "generalszh.presentation.weapon_recoils";
};
template<>
struct ResourceTraits<generalszh::presentation::WeaponExhausts>
{
	static constexpr std::string_view StableName = "generalszh.presentation.weapon_exhausts";
};
template<>
struct ResourceTraits<generalszh::presentation::WeaponFireLoops>
{
	static constexpr std::string_view StableName = "generalszh.presentation.weapon_fire_loops";
};
template<>
struct ResourceTraits<generalszh::presentation::BonePoses>
{
	static constexpr std::string_view StableName = "generalszh.presentation.bone_poses";
};
template<>
struct ResourceTraits<generalszh::presentation::ParticleWorldHandle>
{
	static constexpr std::string_view StableName = "generalszh.presentation.particle_world";
};
}
