export module engine.effects.particles.definitions.particle_system_definition;
import std;

// A particle system's recipe: how often and how many particles it emits,
// from which volume with which velocity, and how each particle ages (size,
// spin, damping, alpha and colour keyframes over its lifetime in frames).
// Time is in presentation frames (30 per second of game time), as the data
// is authored. Games bind their content onto this; the particle world runs it.
export namespace engine::effects
{
// A value picked uniformly between min and max per use (GameClientRandomVariable: max itself when min >= max).
struct Range
{
	float min{0.0f};
	float max{0.0f};
};

enum class ParticleShader : std::uint8_t
{
	None,
	Additive,
	Alpha,
	AlphaTest,
	Multiply,
};

enum class ParticleKind : std::uint8_t
{
	Particle,  // a camera-facing textured quad
	Drawable,  // an object drawn per particle
	Streak,    // a ribbon through the particles
	Volume,    // stacked quads
	Smudge,    // screen-space distortion
};

enum class EmissionVelocity : std::uint8_t
{
	None,
	Ortho,
	Spherical,
	Hemispherical,
	Cylindrical,
	Outward,
};

enum class EmissionVolume : std::uint8_t
{
	Point,
	Line,
	Box,
	Sphere,
	Cylinder,
};

enum class WindMotion : std::uint8_t
{
	None,
	PingPong,
	Circular,
};

enum class ParticlePriority : std::uint8_t
{
	WeaponExplosion,
	Scorchmark,
	DustTrail,
	Buildup,
	DebrisTrail,
	UnitDamageFx,
	DeathExplosion,
	SemiConstant,
	Constant,
	WeaponTrail,
	AreaEffect,
	Critical,
	AlwaysRender,
};

struct AlphaKey
{
	Range value;
	std::uint32_t frame{0};
};

struct ColorKey
{
	std::array<float, 3> color{}; // 0..1
	std::uint32_t frame{0};
};

inline constexpr std::size_t KeyframeCount = 8;

struct ParticleSystemDefinition
{
	std::string name;
	ParticlePriority priority{ParticlePriority::WeaponExplosion};
	bool oneShot{false};
	ParticleShader shader{ParticleShader::Additive};
	ParticleKind kind{ParticleKind::Particle};
	std::string texture; // ParticleName: the texture (or drawable) each particle shows

	Range angleZ;
	Range angularRateZ;
	Range angularDamping{1.0f, 1.0f};
	Range velocityDamping{1.0f, 1.0f};
	float gravity{0.0f}; // per frame², along z

	std::string slaveSystem;
	std::array<float, 3> slaveOffset{};
	std::string attachedSystem; // a system following every particle

	Range lifetime;              // frames
	std::uint32_t systemLifetime{0}; // frames; 0: until stopped
	Range size;
	Range startSizeRate;         // each particle starts this much larger than the last
	Range sizeRate;
	Range sizeRateDamping{1.0f, 1.0f};

	std::array<AlphaKey, KeyframeCount> alpha{};
	std::array<ColorKey, KeyframeCount> color{};
	Range colorScale; // added to each colour channel every frame (0..1 units: the original's INI value / 255)

	Range burstDelay;  // frames between bursts
	Range burstCount;
	Range initialDelay;
	std::array<float, 3> drift{};

	EmissionVelocity velocityType{EmissionVelocity::None};
	Range velocityOrtho[3];
	Range velocitySpherical;
	Range velocityHemispherical;
	Range velocityRadial;
	Range velocityNormal;
	Range velocityOutward;
	Range velocityOutwardOther;

	EmissionVolume volumeType{EmissionVolume::Point};
	std::array<float, 3> lineStart{};
	std::array<float, 3> lineEnd{};
	std::array<float, 3> boxHalfSize{};
	float sphereRadius{0.0f};
	float cylinderRadius{0.0f};
	float cylinderLength{0.0f};
	bool hollow{false};
	bool groundAligned{false};
	bool emitAboveGroundOnly{false};
	bool upTowardsEmitter{false};

	// Wind (ParticleSystemInfo's defaults): radians a frame the wind turns by (PingPong picks a new rate between these
	// at each end of its swing), and the ranges its swing's start and end angles are picked from.
	WindMotion wind{WindMotion::None};
	float windAngleChangeMin{0.15f};
	float windAngleChangeMax{0.45f};
	Range windStartAngle{0.0f, 0.785398163f};
	Range windEndAngle{5.497787144f, 6.283185307f};
};
}
