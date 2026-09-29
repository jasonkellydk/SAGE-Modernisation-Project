export module games.generalszh.presentation.objects.resources.object_instance;
import std;

// Plain data the renderer draws from (no engine imports, so every renderer
// interface can take it cheaply).
export namespace generalszh::presentation
{
// One object to draw: which look (a model in one of its states, numbered by
// the look catalog), where, in whose colour, and how far into its animation.
struct ObjectInstance
{
	std::uint32_t look{0};
	std::array<float, 16> world{}; // row-major, scale included
	std::array<float, 4> teamColor{1, 1, 1, 0}; // alpha 0: no team colour
	float animationSeconds{0};
	bool muzzleFlash{false}; // its weapon is firing: show the flashes
	std::uint8_t flashBarrels{0}; // which barrels flash (bit per barrel; none: all while muzzleFlash)
	std::array<float, 8> recoil{}; // how far each barrel has recoiled (model units, back along x)
	float opacity{1.0f};     // below 1: seen through (stealthed, fading)
	std::array<float, 3> tint{0.0f, 0.0f, 0.0f}; // a colour added over it (Drawable::colorTint: an illegal placement's red); none: 0
	// Its lit colour scaled (W3DTreeBuffer: a tree pushed aside darkens, 1 - DarkeningFactor x how far); 1: as lit.
	float shade{1.0f};
	bool night{false};       // its NIGHT condition: its headlights show
	// Loaded projectiles hidden per weapon slot (fired: W3DModelDraw::doHideShowProjectileObjects hides the first ones);
	// 255: the slot's single hide/show sub-object hidden.
	std::array<std::uint8_t, 3> projectilesHidden{};
	// Its turret's turn and pitch relative to the body (radians).
	float turret{0.0f};
	float turretPitch{0.0f};
	// Its second turret's (AltTurret), the same way.
	float altTurret{0.0f};
	float altTurretPitch{0.0f};
	// How far its treads' textures have rolled (W3DTankDraw: parts named
	// TREADSL..., TREADSR...; others TREADS... roll with the left).
	std::array<float, 2> treads{0.0f, 0.0f};
	// How far its tires have turned (W3DTruckDraw), radians about their axles.
	float wheels{0.0f};
	float rearWheels{0.0f}; // the rear (unsteered) tires'
	// Its tires' suspension (height offsets: front left, front right, rear left, rear right).
	std::array<float, 4> suspension{};
	// Its heat vision (the orange glow of a detected stealthy thing): how strong, and whether only it shows (an enemy's
	// view of it: the model's own pass skipped).
	float heatVision{0.0f};
	bool heatOnly{false};
	// The front tires' steering, and the cab's and trailer's swing (radians about their z).
	float steer{0.0f};
	float cab{0.0f};
	float trailer{0.0f};
	float animationStart{0}; // where it started, as a share of the animation (0 its first frame, 1 its last)
	bool castsShadow{false}; // it casts a shadow on the terrain (a directional shadow caster)
	bool receivesDynamicLights{true};
	// The sphere dynamic lights must reach to light it (centre, radius): its geometry's bounding sphere, centred half its
	// height up (the original uses its render object's sphere; see the ledger).
	std::array<float, 4> lightSphere{};
};
}
