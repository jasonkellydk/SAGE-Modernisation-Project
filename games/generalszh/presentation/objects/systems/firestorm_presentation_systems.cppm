export module games.generalszh.presentation.objects.systems.firestorm_presentation_systems;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.combat.components.firestorm;
export import engine.gameplay.common.spatial.components.dynamic_geometry;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.definition_ref;
export import games.generalszh.presentation.objects.components.effect_attachments;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import Engine.Core.Math.FixedPresentation;

// FirestormDynamicGeometryInfoUpdate's particle systems, each frame: once its effects have fired (the simulation's
// Firestorm), its ParticleSystem1-16 start where it stands, ParticleOffsetZ over the ground (not riding on it); then every
// frame each still running has its sphere or cylinder emission radius set to its radius (setEmissionVolume*Radius from
// its geometry's major radius).
export namespace generalszh::presentation
{
struct FirestormPresentationSystem
{
	using Query = ecs::Query<ecs::Read<generalszh::gameplay::Firestorm>, ecs::Read<engine::gameplay::DynamicGeometry>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<FirestormEmission>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<TerrainHeightHandle>, ecs::Write<ParticleWorldHandle>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		if (particles.world == nullptr || particles.content == nullptr)
			return;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const auto &ground = context.Read<TerrainHeightHandle>().at;
		auto &emissions = context.Side<SideTables, FirestormEmission>();
		auto &commands = context.Commands();
		query.ForEachChunk([&](auto chunk) {
			const auto storms = chunk.template Get<generalszh::gameplay::Firestorm>();
			const auto geometries = chunk.template Get<engine::gameplay::DynamicGeometry>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto refs = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < storms.size(); ++row)
			{
				const float radius = Engine::Math::ToFloat(geometries[row].major);
				if (FirestormEmission *emission = emissions.Get(entities[row]))
				{
					for (const std::uint64_t system : emission->systems)
						particles.world->Resize(system, radius);
					continue;
				}
				if (storms[row].effectsFired == 0)
					continue;
				const DefinitionLooks *looks = catalog.Of(refs[row].index);
				if (looks == nullptr)
					continue;
				FirestormEmission started;
				const float x = Engine::Math::ToFloat(transforms[row].position.x), y = Engine::Math::ToFloat(transforms[row].position.y);
				const float z = looks->firestormOffsetZ + (ground ? ground(x, y) : 0.0f);
				for (const std::string &name : looks->firestormSystems)
					if (const auto *definition = particles.content->particles.Find(name))
						started.systems.push_back(particles.world->Create(*definition, engine::effects::EmitterTransform::At(x, y, z, 0.0f), 0, radius));
				commands.Add<FirestormEmission>(entities[row], std::move(started));
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::FirestormPresentationSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.firestorms";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
