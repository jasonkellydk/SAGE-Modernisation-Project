export module games.generalszh.presentation.effects.fx_playback;
import std;

export import games.generalszh.presentation.effects.effects_content;
export import games.generalszh.presentation.effects.light_pulses;
export import games.generalszh.presentation.effects.scorch_marks;
export import engine.effects.particles.simulation.particle_world;
export import engine.ecs.core.entity;

// Playing an FX list where something happened (a utility for presentation
// systems): each particle nugget starts its system(s) around the spot (a
// random point within its radius, at its height or on the ground, turned
// with the thing when asked, after its delay); sounds and camera shakes
// become requests for whoever hears or shows them; nested lists play in
// turn; light pulses are lit where it happened (sized by the thing when asked). Its randomness is presentation's own and never touches the simulation.
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
	ecs::Entity object{}; // what it happened to, if anything (attached systems ride on it)
	// A weapon's fire FX: plays at this object's firing barrel's FireFX bone once presentation
	// places it (W3DModelDraw::handleWeaponFireFX), else where `at` says (the drawable's position).
	ecs::Entity firedBy{};
	std::uint8_t firedSlot{0}; // the weapon slot that fired (its FireFX bone)
	// A scorch mark of this radius (SCORCH_1) left at `at` besides the FX (GameClient::addScorch: a blast's).
	float scorchRadius{0.0f};
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
};

struct ShakeRequest
{
	ShakeType type{ShakeType::Normal};
	std::array<float, 3> at{};
};

using GroundHeightAt = std::function<float(float x, float y)>;

namespace fx_playback_detail
{
inline float Uniform(std::mt19937 &random, float low, float high)
{
	return high <= low ? low : std::uniform_real_distribution<float>(low, high)(random);
}

inline void Particles(const EffectsContent &content, engine::effects::ParticleWorld &particles, std::mt19937 &random, const GroundHeightAt &ground,
	const ParticleNugget &n, const FxRequest &request, std::vector<FxAttachment> *attached)
{
	const engine::effects::ParticleSystemDefinition *definition = content.particles.Find(n.system);
	if (definition == nullptr)
		return;
	const float c = std::cos(request.yaw), s = std::sin(request.yaw);
	// The offset turns with the thing, as the original's transformed offset.
	const float ox = n.offset[0] * c - n.offset[1] * s;
	const float oy = n.offset[0] * s + n.offset[1] * c;
	for (int index = 0; index < std::max(1, n.count); ++index)
	{
		const float distance = Uniform(random, n.radius.min, n.radius.max);
		const float angle = Uniform(random, 0.0f, 6.2831853f);
		const float px = request.at[0] + ox + distance * std::cos(angle);
		const float py = request.at[1] + oy + distance * std::sin(angle);
		float pz = request.at[2] + n.offset[2];
		if (n.atGroundHeight && ground)
			pz = ground(px, py);
		else
			pz += Uniform(random, n.height.min, n.height.max);
		const float facing = (n.orientToObject ? request.yaw : 0.0f) + n.rotate[2];
		const auto delayFrames = static_cast<std::uint32_t>(std::ceil(std::max(0.0f, Uniform(random, n.delayMs.min, n.delayMs.max)) * 30.0f / 1000.0f));
		const auto id = particles.Create(*definition, engine::effects::EmitterTransform::At(px, py, pz, facing), delayFrames, n.useCallersRadius ? request.radius : 0.0f);
		if (n.attachToObject && attached != nullptr && request.object.IsValid())
		{
			// Back into the object's frame, so it follows as the object moves and turns.
			const float dx = px - request.at[0], dy = py - request.at[1];
			attached->push_back({id, {dx * c + dy * s, -dx * s + dy * c, pz - request.at[2]}, facing - request.yaw});
		}
	}
}
}

// Plays `request`; false when there is no such list.
inline bool PlayFx(const EffectsContent &content, engine::effects::ParticleWorld &particles, std::mt19937 &random, const GroundHeightAt &ground,
	const FxRequest &request, std::vector<SoundRequest> &sounds, std::vector<ShakeRequest> &shakes, std::vector<FxAttachment> *attached = nullptr,
	std::vector<LightPulse> *lights = nullptr, int depth = 0, ScorchMarks *scorches = nullptr)
{
	const FxList *list = request.fx.empty() ? nullptr : content.fx.Find(request.fx);
	if (list == nullptr || depth > 4)
		return false;
	for (const FxNugget &nugget : list->nuggets)
		std::visit(
			[&](const auto &n) {
				using N = std::decay_t<decltype(n)>;
				if constexpr (std::is_same_v<N, SoundNugget>)
				{
					if (!n.sound.empty())
						sounds.push_back({n.sound, request.at});
				}
				else if constexpr (std::is_same_v<N, ParticleNugget>)
					fx_playback_detail::Particles(content, particles, random, ground, n, request, attached);
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
				else if constexpr (std::is_same_v<N, NestedFxNugget>)
				{
					FxRequest nested = request;
					nested.fx = n.fx;
					PlayFx(content, particles, random, ground, nested, sounds, shakes, attached, lights, depth + 1, scorches);
				}
			},
			nugget);
	return true;
}
}
