export module games.generalszh.presentation.rendering.object_rendering;
import std;
export import games.generalszh.presentation.effects.light_pulses;

import engine.level.model.level;
export import games.generalszh.presentation.objects.resources.object_instance;
import Graphics.RHI;

// Draws the simulation's objects as their W3D models. Models load through
// the asset cache in the background the first time a definition is seen;
// an object shows once its model is ready. The interface stays light; the
// asset and prop renderer imports live in the implementation.
export namespace generalszh::presentation
{

// How a model's animation plays (W3DModelDraw's AnimationMode).
enum class ObjectAnimationMode : std::uint8_t
{
	Manual,
	Loop,
	Once,
	LoopPingPong,
	LoopBackwards,
	OnceBackwards,
};

// What a look draws: its model and the animation it plays.
struct ObjectModel
{
	std::string model;     // empty: nothing to draw
	std::string animation; // "<skeleton>.<clip>"; empty: the model's rest pose
	ObjectAnimationMode animationMode{ObjectAnimationMode::Once};
	bool repeat{false};    // idle animations start again once they finish
	// Sub-objects (by name) hidden, and shown despite being flashes.
	std::vector<std::string> hidden;
	std::vector<std::string> shown;
	// Name prefixes of muzzle flash sub-objects, shown only while firing.
	std::vector<std::string> muzzleFlashes;
	// Tire bones (W3DTruckDraw), turned by the instance's wheel angle; the
	// steered ones also turn by its steering; the cab and trailer swing.
	std::vector<std::string> tires;
	std::vector<std::string> steeredTires;
	std::string cab;
	std::string trailer;
	// The turret bones (W3DModelDraw Turret / TurretPitch) and their art offsets (radians).
	std::string turret;
	std::string turretPitch;
	float turretArtAngle{0.0f};
	float turretArtPitch{0.0f};
	std::string recoilBone; // the barrels' recoil bones (NAME01...)
	// The second turret's bones (AltTurret / AltTurretPitch) and art offsets (radians).
	std::string altTurret;
	std::string altTurretPitch;
	float altTurretArtAngle{0.0f};
	float altTurretArtPitch{0.0f};
	// Each tire's corner, for its suspension (0 front left .. 3 rear right), as `tires`.
	std::vector<std::uint8_t> tireCorners;
	// Loaded projectiles shown per weapon slot (ProjectileBoneFeedbackEnabledSlots): the slots, and each one's launch
	// bone (its projectiles NAME01, NAME02...) or single hide/show sub-object.
	std::uint8_t projectileSlots{0};
	std::array<std::string, 3> projectileBones;
	std::array<std::string, 3> projectileHideShow;
	std::string texture; // drawn in place of its parts' own base maps (W3DTreeDraw TextureName); empty: as authored
};

class ObjectRendering
{
public:
	// The model a look draws with (empty model: nothing to draw).
	using ModelLookup = std::function<ObjectModel(std::uint32_t look)>;

	ObjectRendering();
	~ObjectRendering();
	ObjectRendering(const ObjectRendering &) = delete;
	ObjectRendering &operator=(const ObjectRendering &) = delete;

	void Draw(Graphics::Device &device, Graphics::CommandList &commands, const std::array<float, 16> &viewProjection,
		const std::array<float, 3> &eye, const engine::level::LightingSet *lighting, std::span<const ObjectInstance> instances,
		const ModelLookup &modelFor, std::span<const ShownLight> lights = {});

	// The instances that cast shadows, as directional shadow casters for this frame (before the shadow maps render).
	void SubmitShadows(std::span<const ObjectInstance> instances, const ModelLookup &modelFor);

	// Loads the models these instances need and waits for them, as the
	// original's load screen does; later definitions load in the background.
	void Preload(Graphics::Device &device, std::span<const ObjectInstance> instances, const ModelLookup &modelFor);

	// Models (and animations) that failed to load, one per line.
	// A look's animation clip once its model has loaded (its frames and their rate; zero frames when it has
	// none or failed); none while it loads or before it was first drawn.
	std::optional<std::pair<float, float>> ClipOf(std::uint32_t look) const;

	const std::string &Failures() const noexcept;
	std::size_t ReadyModelCount() const noexcept;
	std::string Summary() const;

private:
	struct State;
	std::unique_ptr<State> m_state;
};
}
