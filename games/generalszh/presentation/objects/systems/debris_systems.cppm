export module games.generalszh.presentation.objects.systems.debris_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.appearance.components.model_override;
export import engine.gameplay.common.physics.components.physics_body;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.algorithms.debris_animation;
import Engine.Core.Math.FixedPresentation;

// Thrown debris pieces' animations, each frame (W3DDebrisDraw::doDrawModule): the step of each piece that animates,
// its landing effect where it is as its final animation starts. Before the objects are placed (they show the step's
// animation).
export namespace generalszh::presentation
{
struct DebrisAnimationSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::ModelOverride>, ecs::Read<engine::gameplay::DebrisLook>, ecs::Read<engine::gameplay::Transform>,
		ecs::Optional<engine::gameplay::PhysicsBody>, ecs::Exclude<engine::gameplay::OffMap>>;
	using SideTables = ecs::SideTables<ecs::Write<DebrisMotion>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<LookClips>, ecs::Read<PresentationFrame>, ecs::Write<FxRequests>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const LookClips &clips = context.Read<LookClips>();
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		auto &fx = context.Write<FxRequests>().pending;
		auto &motions = context.Side<SideTables, DebrisMotion>();
		query.ForEachChunk([&](auto chunk) {
			const auto models = chunk.template Get<engine::gameplay::ModelOverride>();
			const auto looks = chunk.template Get<engine::gameplay::DebrisLook>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto bodies = chunk.template Get<engine::gameplay::PhysicsBody>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < looks.size(); ++row)
			{
				const engine::gameplay::DebrisLook &look = looks[row];
				if (look.animations == std::array<std::uint32_t, 3>{})
					continue;
				DebrisMotion *motion = motions.Get(entities[row]);
				if (motion == nullptr)
				{
					context.Commands().Add<DebrisMotion>(entities[row], DebrisMotion{});
					continue; // from the next frame
				}
				// Object::isAboveTerrain as of the last tick (a body that ended it airborne).
				const bool above = !bodies.empty() && bodies[row].Has(engine::gameplay::physics_flag::WasAirborne);
				const std::uint32_t shown = catalog.LookOfModel(models[row].model, motion->animation, motion->mode);
				const bool complete = DebrisAnimationComplete(*motion, clips.At(shown), frame.clock);
				if (StepDebris(*motion, look, above, complete, frame.clock, frame.seconds))
				{
					const std::string_view effect = catalog.EffectName(look.finalEffect);
					if (!effect.empty())
					{
						const auto &transform = transforms[row];
						fx.push_back({std::string(effect),
							{Engine::Math::ToFloat(transform.position.x), Engine::Math::ToFloat(transform.position.y), Engine::Math::ToFloat(transform.position.z)},
							static_cast<float>(static_cast<std::int32_t>(transform.facing.units)) * 6.283185307179586f / 4294967296.0f});
					}
				}
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::DebrisAnimationSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.debris_animation";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
