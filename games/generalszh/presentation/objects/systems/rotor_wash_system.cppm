export module games.generalszh.presentation.objects.systems.rotor_wash_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.object_shroud;
export import engine.gameplay.rts.containment.components.transport;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import Engine.Core.Math.FixedPresentation;

// ChinookAIUpdate::update's rotor wash, once a tick: a Chinook clear of the viewer's shroud while it lands, stands landed
// or takes off (its Transport landing or taking off) puts its RotorWashParticleSystem on the ground under it (3 above
// the terrain) whenever a presentation random pick between 0 and its height over that spot comes under 5: every tick
// near the ground, ever more rarely higher up.
export namespace generalszh::presentation
{
// GameClientRandomValueReal(0, elevation) < 5 (a pick below 0 when it is under that spot).
inline bool RotorWashes(float elevation, std::mt19937 &random)
{
	const float pick = elevation <= 0.0f ? elevation * std::uniform_real_distribution<float>(0.0f, 1.0f)(random)
										 : std::uniform_real_distribution<float>(0.0f, elevation)(random);
	return pick < 5.0f;
}

struct RotorWashSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transport>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Optional<engine::gameplay::ObjectShroud>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<LookCatalog>, ecs::Read<TerrainHeightHandle>, ecs::Write<ParticleWorldHandle>,
		ecs::Write<PresentationRandom>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		const GroundHeightAt &ground = context.Read<TerrainHeightHandle>().at;
		if (particles.world == nullptr || particles.content == nullptr || !ground)
			return;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		auto &random = context.Write<PresentationRandom>().engine;
		query.ForEachChunk([&](auto chunk) {
			const auto transports = chunk.template Get<engine::gameplay::Transport>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto shrouds = chunk.template Get<engine::gameplay::ObjectShroud>();
			for (std::size_t row = 0; row < transports.size(); ++row)
			{
				const engine::gameplay::Transport &transport = transports[row];
				if (!transport.landing && !transport.takingOff)
					continue;
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				if (looks == nullptr || looks->rotorWash.empty())
					continue;
				if (viewer != PresentationFrame::NoViewer && !shrouds.empty() && !shrouds[row].ClearTo(viewer))
					continue;
				const auto &at = transforms[row].position;
				const float x = Engine::Math::ToFloat(at.x), y = Engine::Math::ToFloat(at.y);
				const float under = ground(x, y) + 3.0f;
				if (!RotorWashes(Engine::Math::ToFloat(at.z) - under, random))
					continue;
				if (const auto *definition = particles.content->particles.Find(looks->rotorWash))
					particles.world->Create(*definition, engine::effects::EmitterTransform::At(x, y, under, 0.0f));
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::RotorWashSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.rotor_wash";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
