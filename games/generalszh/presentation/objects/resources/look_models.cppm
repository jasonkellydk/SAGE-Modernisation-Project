export module games.generalszh.presentation.objects.resources.look_models;
import std;

// What each look draws: its model, the animation it plays and how, the parts it hides or shows, and the bones its
// object turns (tires, cab, trailer, turrets, barrels). Plain data the renderer and the model poses read.
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
	// Sub-objects (by name) hidden, and shown (ShowSubObject: a flash among them still shows only while firing).
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

}
