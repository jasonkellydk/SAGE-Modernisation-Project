export module games.generalszh.presentation.objects.systems.spectre_strafe_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.object_shroud;
export import games.generalszh.gameplay.powers.components.spectre_gunship;
export import games.generalszh.presentation.objects.components.object_presentation;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import Engine.Core.Math.FixedPresentation;

// SpectreGunshipUpdate::update's gattling smoke, once a tick: each tick a gunship's gattling fire walks
// (SpectreGunship::strafedTick), while the viewer sees the gunship (partly clear or better), its
// GattlingStrafeFXParticleSystem goes on the ground within 5 of where the fire now falls (GameClientRandomValueReal
// on x and y; the terrain's height there).
export namespace generalszh::presentation
{
struct SpectreStrafeSystem
{
	using Query = ecs::Query<ecs::Read<gameplay::SpectreGunship>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Optional<engine::gameplay::ObjectShroud>>;
	using SideTables = ecs::SideTables<ecs::Write<SpectreStrafeSeen>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<LookCatalog>, ecs::Read<TerrainHeightHandle>, ecs::Write<ParticleWorldHandle>,
		ecs::Write<PresentationRandom>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		const GroundHeightAt &ground = context.Read<TerrainHeightHandle>().at;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		auto &random = context.Write<PresentationRandom>().engine;
		auto &seen = context.Side<SideTables, SpectreStrafeSeen>();
		query.ForEachChunk([&](auto chunk) {
			const auto ships = chunk.template Get<gameplay::SpectreGunship>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto shrouds = chunk.template Get<engine::gameplay::ObjectShroud>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < ships.size(); ++row)
			{
				const gameplay::SpectreGunship &ship = ships[row];
				if (ship.strafedTick == 0)
					continue;
				SpectreStrafeSeen *known = seen.Get(entities[row]);
				if (known == nullptr)
					known = seen.Emplace(entities[row]);
				else if (known->last == ship.strafedTick)
					continue;
				known->last = ship.strafedTick;
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				if (looks == nullptr || looks->gattlingStrafeFx.empty() || particles.world == nullptr || particles.content == nullptr || !ground)
					continue;
				if (viewer != PresentationFrame::NoViewer && !shrouds.empty() && !shrouds[row].SeenBy(viewer))
					continue;
				const auto *definition = particles.content->particles.Find(looks->gattlingStrafeFx);
				if (definition == nullptr)
					continue;
				std::uniform_real_distribution<float> scatter(-5.0f, 5.0f);
				const float x = Engine::Math::ToFloat(ship.gattlingTarget.x) + scatter(random);
				const float y = Engine::Math::ToFloat(ship.gattlingTarget.y) + scatter(random);
				particles.world->Create(*definition, engine::effects::EmitterTransform::At(x, y, ground(x, y), 0.0f));
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::SpectreStrafeSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.spectre_strafes";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
