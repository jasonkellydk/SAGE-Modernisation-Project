export module games.generalszh.presentation.effects.fx_playback;
export import games.generalszh.presentation.effects.tracers;
import std;

export import games.generalszh.presentation.effects.effects_content;
export import games.generalszh.presentation.effects.light_pulses;
export import games.generalszh.presentation.effects.scorch_marks;
export import engine.effects.particles.simulation.particle_world;
export import engine.ecs.core.entity;

// Playing an FX list where something happened (a utility for presentation
// systems; FXList::doFXObj on an object, FXList::doFXPos at a spot): each
// particle nugget starts its system(s) around the spot (a random point within
// its radius, at its height or on the ground, turned with the thing when
// asked, after its delay); sounds and camera shakes become requests for
// whoever hears or shows them; FXListAtBonePos lists play at the object's
// bones; light pulses are lit where it happened (sized by the thing when
// asked). Its randomness is presentation's own and never touches the simulation.
// Systems a nugget attaches to the object (AttachToObject) come back to the
// caller, which keeps them riding on it until it goes (as the original).
export namespace generalszh::presentation
{
struct FxRequest
{
	std::string fx;
	std::array<float, 3> at{};
	float yaw{0.0f};    // radians
	float radius{0.0f}; // sizes effects that take their caller's radius
	ecs::Entity object{}; // what it happened to, if anything (doFXObj; attached systems ride on it)
	// A weapon's fire FX: plays at this object's firing barrel's FireFX bone once presentation
	// places it (W3DModelDraw::handleWeaponFireFX), else where `at` says (the drawable's position).
	ecs::Entity firedBy{};
	std::uint8_t firedSlot{0}; // the weapon slot that fired (its FireFX bone)
	// A scorch mark of this radius (SCORCH_1) left at `at` besides the FX (GameClient::addScorch: a blast's).
	float scorchRadius{0.0f};
	// A particle system started by name riding on `object` instead of an FX list (createParticleSystem, attachToObject).
	std::string particleSystem;
	// doFXPos's second point and its caller's speed a frame (a weapon's fire FX: where it aims, its WeaponSpeed); on an
	// object (doFXObj), where its second object is (a hit's dealer: DamageFX::doDamageFX).
	bool hasSecondary{false};
	std::array<float, 3> secondary{};
	float speed{0.0f};
	// doFXPos's whole transform (row-major 3x4) when it is more than a turn by `yaw` (an FX at a bone); none: `yaw`'s.
	std::optional<std::array<float, 12>> transform;
	// The object's controlling player (doFXObj: SoundFXNugget's setPlayerIndex); none: 0xFFFFFFFF.
	std::uint32_t owner{0xFFFFFFFFu};
};

// A system started riding on the request's object: where, in its frame (unscaled, facing the request's yaw).
struct FxAttachment
{
	std::uint64_t id{0};
	std::array<float, 3> local{};
	float yaw{0.0f};
};

struct SoundRequest
{
	static constexpr std::uint32_t NoOwner = 0xFFFFFFFFu;
	std::string sound;
	std::array<float, 3> at{};
	std::uint32_t owner{NoOwner}; // the player it is for (AudioEventRTS::setPlayerIndex); none: nobody's
	bool positioned{true};        // false: heard without a position (a script's PLAY_SOUND_EFFECT)
	std::optional<float> volume;  // AudioEventRTS::setVolume (none: its own)
	ecs::Entity object;           // AudioEventRTS::setObjectID: whose voice it is (a voice event: one at a time)
};

struct ShakeRequest
{
	ShakeType type{ShakeType::Normal};
	std::array<float, 3> at{};
};

using GroundHeightAt = std::function<float(float x, float y)>;

// What playing an FX list asks of the world besides the ground under a spot:
// - bones: Drawable::getCurrentClientBoneTransforms then Thing::transformBoneToWorld on `object`: the world transforms
//   (row-major 3x4) of its bones as drawn now, `bone` itself (start 0) or bone01, bone02, ... to the first missing
//   (start 1), at most 40 (FXListAtBonePosFXNugget's MAX_BONE_POINTS);
// - layerHeight: TerrainLogic::getLayerHeight on getLayerForDestination: the height of the ground or bridge deck
//   nearest the spot's own height (CreateAtGroundHeight); none: the ground.
struct FxSurroundings
{
	std::function<std::vector<std::array<float, 12>>(ecs::Entity object, std::string_view bone, int start)> bones;
	std::function<float(float x, float y, float z)> layerHeight;
};

// A transform's turning part, row-major 3x3.
using FxBasis = std::array<float, 9>;

// One system a ParticleSystemFXNugget starts (reallyDoFX): placed and turned (its local transform; riding on the object:
// the object's times it), the frames it waits in place of its own InitialDelay (setInitialDelay; none: its own), and
// whether it rides on the request's object (attachToObject: at the object's origin, turned `attachedYaw` from it).
struct ParticleStart
{
	engine::effects::EmitterTransform transform;
	std::optional<std::uint32_t> delayFrames;
	bool attached{false};
	float attachedYaw{0.0f};
};

namespace fx_playback_detail
{
// GameClientRandomValueReal: `high` itself when low >= high.
inline float Uniform(std::mt19937 &random, float low, float high)
{
	return low >= high ? high : std::uniform_real_distribution<float>(low, high)(random);
}

inline FxBasis TurnZ(float yaw)
{
	const float c = std::cos(yaw), s = std::sin(yaw);
	return {c, -s, 0, s, c, 0, 0, 0, 1};
}

inline FxBasis BasisOf(const FxRequest &request)
{
	if (!request.transform)
		return TurnZ(request.yaw);
	const auto &t = *request.transform;
	return {t[0], t[1], t[2], t[4], t[5], t[6], t[8], t[9], t[10]};
}

// Legacy_Rotate_X / Y / Z (ParticleSystem::rotateLocalTransformX / Y / Z): the basis turned about its own axis.
inline void RotateX(FxBasis &m, float angle)
{
	const float c = std::cos(angle), s = std::sin(angle);
	for (std::size_t row = 0; row < 3; ++row)
	{
		const float t1 = m[row * 3 + 1], t2 = m[row * 3 + 2];
		m[row * 3 + 1] = c * t1 + s * t2;
		m[row * 3 + 2] = -s * t1 + c * t2;
	}
}
inline void RotateY(FxBasis &m, float angle)
{
	const float c = std::cos(angle), s = std::sin(angle);
	for (std::size_t row = 0; row < 3; ++row)
	{
		const float t0 = m[row * 3], t2 = m[row * 3 + 2];
		m[row * 3] = c * t0 - s * t2;
		m[row * 3 + 2] = s * t0 + c * t2;
	}
}
inline void RotateZ(FxBasis &m, float angle)
{
	const float c = std::cos(angle), s = std::sin(angle);
	for (std::size_t row = 0; row < 3; ++row)
	{
		const float t0 = m[row * 3], t1 = m[row * 3 + 1];
		m[row * 3] = c * t0 + s * t1;
		m[row * 3 + 1] = -s * t0 + c * t1;
	}
}

inline FxBasis Times(const FxBasis &a, const FxBasis &b)
{
	FxBasis result{};
	for (std::size_t row = 0; row < 3; ++row)
		for (std::size_t column = 0; column < 3; ++column)
			result[row * 3 + column] = a[row * 3] * b[column] + a[row * 3 + 1] * b[3 + column] + a[row * 3 + 2] * b[6 + column];
	return result;
}

inline engine::effects::EmitterTransform Placed(const FxBasis &m, float x, float y, float z)
{
	return {{m[0], m[1], m[2], x, m[3], m[4], m[5], y, m[6], m[7], m[8], z}};
}
}

// ParticleSystemFXNugget::reallyDoFX for one of its Count systems. The transform it is played with is the request's
// (doFXObj: the object's; doFXPos: the one given, none being no turn), or with Ricochet on an object that has a second
// one (doFXObj) a turn about z facing away from the second. Its Offset turns with that transform (adjustVector); the
// system sits a random distance within Radius at a random angle from the spot plus the offset, at the offset's height
// plus Height, or with CreateAtGroundHeight on the ground or bridge it is nearest (getLayerForDestination,
// getLayerHeight). With OrientToObject its local transform starts as that transform; then turned by RotateX, RotateY and
// RotateZ (each when not 0). Attached (AttachToObject on an object) it rides on the object's origin, its offset, radius
// and height unused; else it is placed at the spot. InitialDelay (milliseconds; none below 0) replaces the system's own
// delay with ceil(ms x LOGICFRAMES_PER_MSEC_REAL) frames. The random draws: radius, angle, height, delay.
inline ParticleStart PlaceParticles(const ParticleNugget &n, const FxRequest &request, std::mt19937 &random, const GroundHeightAt &ground,
	const FxSurroundings *around = nullptr)
{
	using namespace fx_playback_detail;
	const bool onObject = request.object.IsValid();
	const FxBasis own = BasisOf(request);
	const FxBasis turn = n.ricochet && onObject && request.hasSecondary
		? TurnZ(std::atan2(request.at[1] - request.secondary[1], request.at[0] - request.secondary[0]))
		: own;
	const std::array<float, 3> offset{turn[0] * n.offset[0] + turn[1] * n.offset[1] + turn[2] * n.offset[2],
		turn[3] * n.offset[0] + turn[4] * n.offset[1] + turn[5] * n.offset[2], turn[6] * n.offset[0] + turn[7] * n.offset[1] + turn[8] * n.offset[2]};
	const float distance = Uniform(random, n.radius.min, n.radius.max);
	const float angle = Uniform(random, 0.0f, 6.2831853f);
	const float x = request.at[0] + offset[0] + distance * std::cos(angle);
	const float y = request.at[1] + offset[1] + distance * std::sin(angle);
	float z = request.at[2] + offset[2];
	const bool layered = around != nullptr && static_cast<bool>(around->layerHeight);
	if (n.atGroundHeight && (layered || ground))
		z = layered ? around->layerHeight(x, y, z) : ground(x, y);
	else
		z += Uniform(random, n.height.min, n.height.max);
	FxBasis local = n.orientToObject ? turn : FxBasis{1, 0, 0, 0, 1, 0, 0, 0, 1};
	if (n.rotate[0] != 0.0f)
		RotateX(local, n.rotate[0]);
	if (n.rotate[1] != 0.0f)
		RotateY(local, n.rotate[1]);
	if (n.rotate[2] != 0.0f)
		RotateZ(local, n.rotate[2]);
	ParticleStart start;
	start.attached = n.attachToObject && onObject;
	if (start.attached)
	{
		// ParticleSystem::update: the object's transform times its local one, at the object's origin. Only the turn about
		// z rides along here (no shipped attached nugget is turned otherwise or oriented to its object).
		start.transform = Placed(Times(own, local), request.at[0], request.at[1], request.at[2]);
		start.attachedYaw = std::atan2(local[3], local[0]);
	}
	else
		start.transform = Placed(local, x, y, z);
	if (const float delay = Uniform(random, n.delayMs.min, n.delayMs.max); delay >= 0.0f)
		start.delayFrames = static_cast<std::uint32_t>(std::ceil(delay * (30.0f / 1000.0f)));
	return start;
}

namespace fx_playback_detail
{
inline void Particles(const EffectsContent &content, engine::effects::ParticleWorld &particles, std::mt19937 &random, const GroundHeightAt &ground,
	const ParticleNugget &n, const FxRequest &request, std::vector<FxAttachment> *attached, const FxSurroundings *around)
{
	const engine::effects::ParticleSystemDefinition *definition = content.particles.Find(n.system);
	if (definition == nullptr)
		return;
	// UseCallersRadius: only doFXPos has a caller's radius (doFXObj passes 0).
	const float radius = n.useCallersRadius && !request.object.IsValid() ? request.radius : 0.0f;
	for (int index = 0; index < n.count; ++index)
	{
		const ParticleStart start = PlaceParticles(n, request, random, ground, around);
		const auto id = particles.Create(*definition, start.transform, 0, radius);
		if (start.delayFrames)
			particles.SetInitialDelay(id, *start.delayFrames);
		if (start.attached && attached != nullptr)
			attached->push_back({id, {0.0f, 0.0f, 0.0f}, start.attachedYaw});
	}
}
}

// Plays `request`; false when there is no such list.
inline bool PlayFx(const EffectsContent &content, engine::effects::ParticleWorld &particles, std::mt19937 &random, const GroundHeightAt &ground,
	const FxRequest &request, std::vector<SoundRequest> &sounds, std::vector<ShakeRequest> &shakes, std::vector<FxAttachment> *attached = nullptr,
	std::vector<LightPulse> *lights = nullptr, int depth = 0, ScorchMarks *scorches = nullptr, Tracers *tracers = nullptr,
	const FxSurroundings *around = nullptr)
{
	if (!request.particleSystem.empty())
	{
		ParticleNugget system;
		system.system = request.particleSystem;
		system.attachToObject = true;
		fx_playback_detail::Particles(content, particles, random, ground, system, request, attached, around);
		return true;
	}
	const FxList *list = request.fx.empty() ? nullptr : content.fx.Find(request.fx);
	if (list == nullptr || depth > 4)
		return false;
	for (const FxNugget &nugget : list->nuggets)
		std::visit(
			[&](const auto &n) {
				using N = std::decay_t<decltype(n)>;
				if constexpr (std::is_same_v<N, SoundNugget>)
				{
					// SoundFXNugget: at the spot; on an object, for its controlling player (doFXObj's setPlayerIndex).
					if (!n.sound.empty())
						sounds.push_back({n.sound, request.at, request.object.IsValid() ? request.owner : SoundRequest::NoOwner});
				}
				else if constexpr (std::is_same_v<N, ParticleNugget>)
					fx_playback_detail::Particles(content, particles, random, ground, n, request, attached, around);
				else if constexpr (std::is_same_v<N, ViewShakeNugget>)
					shakes.push_back({n.type, request.at});
				else if constexpr (std::is_same_v<N, ScorchNugget>)
				{
					// TerrainScorchFXNugget::doFXPos: its type, or one of the first four at random (the client's random).
					if (scorches != nullptr)
					{
						const std::uint32_t type = n.type >= 0 ? static_cast<std::uint32_t>(n.type)
							: static_cast<std::uint32_t>(std::uniform_int_distribution<int>(0, 3)(random));
						AddScorch(*scorches, {request.at, n.radius, type});
					}
				}
				else if constexpr (std::is_same_v<N, LightPulseNugget>)
				{
					// LightPulseFXNugget: its radius, or a share of the thing's size (doFXObj); lit from 1 out to 1 + that.
					const float radius = n.radiusOfObjectSize > 0.0f && request.object.IsValid() ? request.radius * n.radiusOfObjectSize : n.radius;
					if (lights != nullptr && 1.0f + radius >= SmallestPulse)
						lights->push_back({request.at, n.color, 1.0f, 1.0f + radius, std::ceil(static_cast<float>(n.increaseMs) * 30.0f / 1000.0f),
							std::ceil(static_cast<float>(n.decreaseMs) * 30.0f / 1000.0f), 0.0f});
				}
				else if constexpr (std::is_same_v<N, TracerNugget>)
				{
					// TracerFXNugget::doFXPos: at its Probability (the client's random), from where it played toward the second
					// point (none: nothing); only the GenericTracer drawable (W3DTracerDraw) is drawn.
					if (n.probability <= fx_playback_detail::Uniform(random, 0.0f, 1.0f))
						return;
					if (tracers != nullptr && request.hasSecondary && n.name == "GenericTracer")
						MakeTracer(*tracers, request.at, request.secondary, n.speed, request.speed, n.decayAt, n.length, n.width, n.color);
				}
				else if constexpr (std::is_same_v<N, NestedFxNugget>)
				{
					// FXListAtBonePosFXNugget::doFXObj: on an object only (doFXPos plays nothing), its list at each of the
					// object's bones as drawn now, the bare name first, then name01, name02, ... (doFxAtBones(0), (1)), each
					// played at the bone (FXList::doFXPos with the bone's world transform: no object, no second point, no
					// speed, no radius).
					if (!request.object.IsValid() || around == nullptr || !around->bones)
						return;
					for (const int start : {0, 1})
						for (const std::array<float, 12> &bone : around->bones(request.object, n.bone, start))
						{
							FxRequest at{n.fx, {bone[3], bone[7], bone[11]}, std::atan2(bone[4], bone[0])};
							at.transform = bone;
							PlayFx(content, particles, random, ground, at, sounds, shakes, attached, lights, depth + 1, scorches, tracers, around);
						}
				}
			},
			nugget);
	return true;
}
}
