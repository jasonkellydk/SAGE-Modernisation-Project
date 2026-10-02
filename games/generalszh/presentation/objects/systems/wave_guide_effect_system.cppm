export module games.generalszh.presentation.objects.systems.wave_guide_effect_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.object_shroud;
export import games.generalszh.gameplay.waveguide.components.wave_guide;
export import games.generalszh.gameplay.waveguide.resources.wave_guide_events;
export import games.generalszh.presentation.objects.algorithms.wave_sprays;
export import games.generalszh.presentation.objects.components.wave_guide_effects;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import Engine.Core.Math.FixedPresentation;

// A flood wave's particle systems (WaveGuideUpdate, GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/
// WaveGuideUpdate.cpp), each frame after the frame's FX played:
//   - initWaveGuide: once the wave has set off, its sprays (WaveSprayLayout) riding on it;
//   - doShapeEffects: each spray's local height follows the ground under its point while that is below PreferredHeight
//     (SprayHeight);
//   - the tick's cues, once however many frames show the tick (WaveGuideCuesPlayed): WaveSplashRight01 /
//     WaveSplashLeft01 at the shore points (doShoreEffects: setPosition, unturned); WaveHit01 riding on the wave at its
//     point's place in its frame, at the victim's height (doDamage); the wave's BridgeParticle at the bridge, turned
//     (doDamage's bridge); WaveSplash01 where it ended, turned as it was (none ships: nothing).
// Systems ride with the wave's transform as the simulation left it (WavePlace); a wave that goes stops them (what is
// out lives on: ParticleSystem::update destroys a system whose object is gone; the side table's release).
export namespace generalszh::presentation
{
struct WaveGuideEffectSystem
{
	using Query = ecs::Query<ecs::Read<gameplay::WaveGuide>, ecs::Read<engine::gameplay::Transform>>;
	using SideTables = ecs::SideTables<ecs::Write<WaveGuideEffects>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::ObjectShroud>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<TerrainHeightHandle>, ecs::Read<gameplay::WaveGuideCues>,
		ecs::Read<LookCatalog>, ecs::Write<WaveGuideCuesPlayed>, ecs::Write<ParticleWorldHandle>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		if (particles.world == nullptr || particles.content == nullptr)
			return;
		auto &table = context.Side<SideTables, WaveGuideEffects>();
		// The sprays and hits ride the wave (attachToObject): nothing emitted while the viewer sees it fogged or shrouded
		// (ParticleSystem::update's isShrouded). The shore splashes and the bridge's are only placed: never held back.
		const auto lookup = context.Lookup<Lookup>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const auto shrouded = [&](ecs::Entity wave) {
			const auto *shroud = lookup.IsAlive(wave) ? lookup.Get<engine::gameplay::ObjectShroud>(wave) : nullptr;
			return shroud != nullptr && viewer != PresentationFrame::NoViewer && viewer < 64 && !shroud->SeenBy(viewer);
		};
		const auto &ground = context.Read<TerrainHeightHandle>().at;
		const auto create = [&](std::string_view name, const std::array<float, 3> &at, float yaw) -> std::uint64_t {
			const auto *definition = name.empty() ? nullptr : particles.content->particles.Find(name);
			return definition != nullptr ? particles.world->Create(*definition, engine::effects::EmitterTransform::At(at[0], at[1], at[2], yaw)) : 0;
		};
		query.ForEachChunk([&](auto chunk) {
			const auto guides = chunk.template Get<gameplay::WaveGuide>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < guides.size(); ++row)
			{
				const gameplay::WaveGuide &guide = guides[row];
				if (guide.initialized == 0)
					continue;
				const auto &position = transforms[row].position;
				const std::array<float, 3> at{Engine::Math::ToFloat(position.x), Engine::Math::ToFloat(position.y), Engine::Math::ToFloat(position.z)};
				const float yaw = Engine::Math::ToFloat(Engine::Math::Radians(transforms[row].facing));
				WaveGuideEffects *fx = table.Get(entities[row]);
				if (fx == nullptr)
					fx = table.Emplace(entities[row]);
				const bool obscured = shrouded(entities[row]);
				if (fx->built == 0)
				{
					fx->built = 1;
					for (const WaveSpray &spray : WaveSprayLayout(gameplay::WaveShapePoints(guide.ySize, guide.spacing, guide.bend)))
						if (const std::uint64_t id = create(spray.system, WavePlace(at, yaw, spray.local), yaw); id != 0)
						{
							fx->systems.push_back(id);
							fx->local.push_back(spray.local);
							fx->spray.push_back(1);
						}
				}
				const float preferred = Engine::Math::ToFloat(guide.preferredHeight);
				for (std::size_t index = 0; index < fx->systems.size(); ++index)
				{
					std::array<float, 3> &local = fx->local[index];
					if (fx->spray[index] != 0 && ground)
					{
						const auto over = WavePlace(at, yaw, {local[0], local[1], 0.0f});
						local[2] = SprayHeight(local[2], ground(over[0], over[1]), preferred);
					}
					const auto world = WavePlace(at, yaw, local);
					particles.world->Move(fx->systems[index], engine::effects::EmitterTransform::At(world[0], world[1], world[2], yaw));
					particles.world->SetObscured(fx->systems[index], obscured);
				}
			}
		});
		WaveGuideCuesPlayed &played = context.Write<WaveGuideCuesPlayed>();
		const std::uint64_t tick = context.Read<PresentationFrame>().tick;
		if (played.tick == tick)
			return;
		played.tick = tick;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const auto vector = [](const Engine::Math::FixedVector3 &v) {
			return std::array<float, 3>{Engine::Math::ToFloat(v.x), Engine::Math::ToFloat(v.y), Engine::Math::ToFloat(v.z)};
		};
		using Kind = gameplay::WaveGuideCue::Kind;
		for (const gameplay::WaveGuideCue &cue : context.Read<gameplay::WaveGuideCues>().list)
		{
			const auto at = vector(cue.at);
			const float yaw = Engine::Math::ToFloat(Engine::Math::Radians(cue.yaw));
			switch (cue.kind)
			{
			case Kind::ShoreLeft:
				create("WaveSplashLeft01", at, 0.0f);
				break;
			case Kind::ShoreRight:
				create("WaveSplashRight01", at, 0.0f);
				break;
			case Kind::End:
				create("WaveSplash01", at, yaw);
				break;
			case Kind::Bridge:
				if (const DefinitionLooks *looks = catalog.Of(cue.definition))
					create(looks->waveBridgeParticle, at, yaw);
				break;
			case Kind::Hit:
			{
				const auto local = vector(cue.local);
				const std::uint64_t id = create("WaveHit01", WavePlace(at, yaw, local), yaw);
				if (WaveGuideEffects *fx = table.Get(cue.guide); fx != nullptr && id != 0)
				{
					particles.world->SetObscured(id, shrouded(cue.guide));
					fx->systems.push_back(id);
					fx->local.push_back(local);
					fx->spray.push_back(0);
				}
				break;
			}
			case Kind::Splash:
				break; // a sound (WaveGuideSoundSystem)
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::WaveGuideEffectSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.wave_guide_effects";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
