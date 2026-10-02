export module games.generalszh.presentation.objects.systems.rope_view_systems;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.combat_drop.components.combat_drop;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import engine.gameplay.common.identity.components.definition_ref;
import Engine.Core.Math.FixedPresentation;

// Combat drop ropes as W3DRopeDraw draws them, each frame on the presentation clock (its per-draw steps as per-second
// rates: 30 draws a second):
//   a rope hangs from its top (RopeStart) curLen down in ceil(maxLen / RopeWobbleLen) segments (maxLen its full length,
//   at least 1); each joint (not the top) is pushed off the rope's line by sin(phase) x RopeWobbleAmplitude along its own
//   random direction (the client's random: here a hash of the rope and joint), the phase turning RopeWobbleRate a draw;
//   each segment is two lines of RopeColor: a core half RopeWidth wide and opaque, and a soft one RopeWidth wide at half
//   opacity;
//   let go (the drop over) the whole rope drops away: it starts 30 x gravity a second down (64) and speeds up by
//   gravity a draw (1920 a second a second), with no floor, until its entity goes 150 ticks on.
// RopeViews: this frame's rope lines, as columns.
export namespace generalszh::presentation
{
struct RopeViews
{
	std::vector<std::array<float, 3>> starts;
	std::vector<std::array<float, 3>> ends;
	std::vector<float> widths;
	std::vector<std::array<float, 4>> colors;

	std::size_t Size() const noexcept { return starts.size(); }
	void Clear()
	{
		starts.clear();
		ends.clear();
		widths.clear();
		colors.clear();
	}
	void Add(const std::array<float, 3> &from, const std::array<float, 3> &to, float width, const std::array<float, 4> &color)
	{
		starts.push_back(from);
		ends.push_back(to);
		widths.push_back(width);
		colors.push_back(color);
	}
};

}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::RopeViews>
{
	static constexpr std::string_view StableName = "generalszh.presentation.rope_views";
};
}

export namespace generalszh::presentation
{
namespace rope_view_detail
{
// A joint's direction off the rope's line (buildSegments: GameClientRandomValueReal(0, 2PI) per segment).
inline std::array<float, 2> Axis(std::uint64_t key, std::uint32_t joint)
{
	std::uint64_t mixed = key * 0x9E3779B97F4A7C15ull + joint * 0xBF58476D1CE4E5B9ull;
	mixed ^= mixed >> 31;
	mixed *= 0x94D049BB133111EBull;
	mixed ^= mixed >> 29;
	const float angle = static_cast<float>(mixed & 0xFFFFFF) / static_cast<float>(0x1000000) * 2.0f * std::numbers::pi_v<float>;
	return {std::cos(angle), std::sin(angle)};
}

// A rope's look (its transport's RopeWidth, RopeColor and wobble), in floats.
struct RopeLook
{
	float width{0.5f};
	std::array<float, 3> color{};
	float wobbleLen{10.0f};
	float wobbleAmplitude{1.0f};
	float wobbleRate{0.1f}; // radians a draw
};

inline RopeLook LookOf(const content::CombatDropContent &content)
{
	using Engine::Math::ToFloat;
	return {ToFloat(content.ropeWidth), {ToFloat(content.ropeColor[0]), ToFloat(content.ropeColor[1]), ToFloat(content.ropeColor[2])},
		ToFloat(content.ropeWobbleLen), ToFloat(content.ropeWobbleAmplitude), ToFloat(content.ropeWobbleRate)};
}

inline void DrawRope(RopeViews &views, const RopeLook &look, const std::array<float, 3> &top, float length, float lengthMax,
	float zOffset, float seconds, std::uint64_t key)
{
	const float maxLen = std::max(1.0f, lengthMax);
	const float wobbleLen = std::min(maxLen, look.wobbleLen);
	const auto segments = static_cast<std::uint32_t>(std::ceil(maxLen / std::max(wobbleLen, 0.001f)));
	if (segments == 0)
		return;
	const float phase = look.wobbleRate * 30.0f * seconds;
	const float deflection = std::sin(phase) * look.wobbleAmplitude;
	const float each = length / static_cast<float>(segments);
	std::array<float, 3> start{top[0], top[1], top[2] + zOffset};
	for (std::uint32_t joint = 0; joint < segments; ++joint)
	{
		const auto axis = Axis(key, joint);
		const std::array<float, 3> end{top[0] + deflection * axis[0], top[1] + deflection * axis[1], start[2] - each};
		views.Add(start, end, look.width * 0.5f, {look.color[0], look.color[1], look.color[2], 1.0f});
		views.Add(start, end, look.width, {look.color[0], look.color[1], look.color[2], 0.5f});
		start = end;
	}
}

inline std::array<float, 3> ToFloats(const Engine::Math::FixedVector3 &at)
{
	return {Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z)};
}
}

// The ropes hanging from the transports dropping now (the first of the frame's rope systems: the views start afresh).
struct HangingRopeViewSystem
{
	using Query = ecs::Query<ecs::Read<generalszh::gameplay::CombatDrop>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<generalszh::gameplay::ObjectTemplates>, ecs::Read<PresentationFrame>, ecs::Write<RopeViews>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		RopeViews &views = context.Write<RopeViews>();
		views.Clear();
		const auto &templates = context.Read<generalszh::gameplay::ObjectTemplates>();
		const auto seconds = static_cast<float>(context.Read<PresentationFrame>().clock);
		query.ForEachChunk([&](auto chunk) {
			const auto drops = chunk.template Get<generalszh::gameplay::CombatDrop>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < drops.size(); ++row)
			{
				const auto &drop = drops[row];
				const auto *config = templates.CombatDropOf(definitions[row].index);
				if (config == nullptr || drop.stage != generalszh::gameplay::CombatDropStage::Dropping)
					continue;
				for (std::uint32_t index = 0; index < drop.ropeCount; ++index)
				{
					const auto &rope = drop.ropes[index];
					const std::uint64_t key = (std::uint64_t{entities[row].index} << 40) ^ (std::uint64_t{entities[row].generation} << 8) ^ index;
					rope_view_detail::DrawRope(views, rope_view_detail::LookOf(config->content), rope_view_detail::ToFloats(rope.top), Engine::Math::ToFloat(rope.length),
						Engine::Math::ToFloat(rope.lengthMax), 0.0f, seconds, key);
				}
			}
		});
	}
};

// The ropes let go, falling away.
struct FallingRopeViewSystem
{
	using Query = ecs::Query<ecs::Read<generalszh::gameplay::FallingRope>>;
	using Resources = ecs::Resources<ecs::Read<generalszh::gameplay::ObjectTemplates>, ecs::Read<PresentationFrame>, ecs::Write<RopeViews>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		RopeViews &views = context.Write<RopeViews>();
		const auto &templates = context.Read<generalszh::gameplay::ObjectTemplates>();
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		const auto seconds = static_cast<float>(frame.clock);
		query.ForEachChunk([&](auto chunk) {
			const auto ropes = chunk.template Get<generalszh::gameplay::FallingRope>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < ropes.size(); ++row)
			{
				const auto &rope = ropes[row];
				const auto *config = templates.CombatDropOf(rope.definition);
				if (config == nullptr)
					continue;
				// setRopeSpeed(g x 30, RopeDropSpeed, g): 64 a second down at first, 1920 a second faster each second.
				const float since = std::max(0.0f, (static_cast<float>(frame.tick) - static_cast<float>(rope.fallFrom) + frame.alpha) / 30.0f);
				const float zOffset = -64.0f * since - 960.0f * since * since;
				const std::uint64_t key = (std::uint64_t{entities[row].index} << 40) ^ (std::uint64_t{entities[row].generation} << 8);
				rope_view_detail::DrawRope(views, rope_view_detail::LookOf(config->content), rope_view_detail::ToFloats(rope.top), Engine::Math::ToFloat(rope.length),
					Engine::Math::ToFloat(rope.lengthMax), zOffset, seconds, key);
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::HangingRopeViewSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.hanging_rope_views";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::FallingRopeViewSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.falling_rope_views";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
