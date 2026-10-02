export module games.generalszh.presentation.objects.systems.track_mark_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.physics.resources.physics_settings;
export import engine.gameplay.rts.stealth.components.stealth;
export import games.generalszh.presentation.objects.components.track_marks;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.presentation_resources;
import games.generalszh.presentation.objects.algorithms.track_laying;
import Engine.Core.Math.FixedPresentation;

// Terrain tracks (W3DModelDraw's track render object and the tracks system):
//   TrackLayingSystem, once a tick (as the drawable's transform changes): a
//   vehicle whose draw leaves TrackMarks takes a track while MakeTrackMarks
//   is on and fewer than MaxTerrainTracks are out (bindTrack); hidden
//   (inside something) or stealthed its track is capped; else, noted
//   airborne when significantly above the ground, it lays an edge where it
//   stands on the ground;
//   TrackFadeSystem, each frame: its edges fade (the tracks system's update),
//   and with MakeTrackMarks off every track loses its anchor (restarts).
export namespace generalszh::presentation
{
struct TrackLayingSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Optional<engine::gameplay::OffMap>,
		ecs::Optional<engine::gameplay::Stealth>>;
	using SideTables = ecs::SideTables<ecs::Write<TrackMarks>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<TrackSettings>, ecs::Read<PresentationFrame>, ecs::Read<engine::gameplay::GroundHeight>,
		ecs::Read<engine::gameplay::PhysicsSettings>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const TrackSettings &settings = context.Read<TrackSettings>();
		const double now = context.Read<PresentationFrame>().clock;
		const engine::gameplay::GroundHeight &ground = context.Read<engine::gameplay::GroundHeight>();
		const Engine::Math::Fixed significant = context.Read<engine::gameplay::PhysicsSettings>().SignificantHeight();
		auto &tracks = context.Side<SideTables, TrackMarks>();
		std::size_t bound = tracks.Size();
		const std::uint32_t maxEdges = std::clamp<std::uint32_t>(settings.maxEdges, 1u, static_cast<std::uint32_t>(MaxTrackEdges));
		query.ForEachChunk([&](auto chunk) {
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto away = chunk.template Get<engine::gameplay::OffMap>();
			const auto stealths = chunk.template Get<engine::gameplay::Stealth>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < transforms.size(); ++row)
			{
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				if (looks == nullptr || looks->trackTexture == DefinitionLooks::NoTrack)
					continue;
				TrackMarks *track = tracks.Get(entities[row]);
				if (track == nullptr)
				{
					if (!settings.make || bound >= settings.maxTracks)
						continue;
					TrackMarks fresh;
					fresh.width = looks->trackWidth;
					fresh.texture = looks->trackTexture;
					context.Commands().Add<TrackMarks>(entities[row], fresh);
					++bound;
					continue;
				}
				const auto xy = transforms[row].position.XY();
				const Engine::Math::Fixed floor = ground.At(xy);
				const auto height = [&](Engine::Math::Fixed dx, Engine::Math::Fixed dy) {
					return Engine::Math::ToFloat(ground.At({xy.x + dx, xy.y + dy}));
				};
				// The ground's normal where it stands (from the heights either side).
				const Engine::Math::Fixed one = Engine::Math::Fixed::One();
				std::array<float, 3> normal{(height(Engine::Math::Fixed{} - one, {}) - height(one, {})) / 2.0f,
					(height({}, Engine::Math::Fixed{} - one) - height({}, one)) / 2.0f, 1.0f};
				const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + 1.0f);
				for (float &axis : normal)
					axis /= length;
				const std::array<float, 3> at{Engine::Math::ToFloat(xy.x), Engine::Math::ToFloat(xy.y), Engine::Math::ToFloat(floor)};
				const bool stealthed = !stealths.empty() && stealths[row].Has(engine::gameplay::stealth_flag::Stealthed);
				if (!away.empty() || stealthed)
				{
					CapTrack(*track, at, normal, maxEdges, now);
					continue;
				}
				if (transforms[row].position.z - floor > significant)
					track->airborne = 1;
				AddTrackEdge(*track, at, normal, maxEdges, now);
			}
		});
	}
};

struct TrackFadeSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<TrackMarks>>;
	using Resources = ecs::Resources<ecs::Read<TrackSettings>, ecs::Read<PresentationFrame>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const TrackSettings &settings = context.Read<TrackSettings>();
		const double now = context.Read<PresentationFrame>().clock;
		const std::uint32_t maxEdges = std::clamp<std::uint32_t>(settings.maxEdges, 1u, static_cast<std::uint32_t>(MaxTrackEdges));
		context.Side<SideTables, TrackMarks>().ForEach([&](ecs::Entity, TrackMarks &track) {
			if (!settings.make)
				track.haveAnchor = 0;
			FadeTrack(track, maxEdges, static_cast<double>(settings.fadeMilliseconds) / 1000.0, now);
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::TrackLayingSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.track_laying";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<generalszh::presentation::TrackFadeSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.track_fade";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
