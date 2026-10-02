export module games.generalszh.gameplay.effects.systems.transition_creation_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.rts.veterancy.components.experience;
import games.generalszh.gameplay.effects.systems.bone_fx_system;
import Engine.Core.Math.FixedRandom;
import Engine.Core.Math.FixedAngle;

// TransitionDamageFX::onBodyDamageStateChange's object creation lists, where the original runs them: in the damage
// itself (ActiveBody::attemptDamage tells every damage module of each hit that changes the body's damage state, in the
// order dealt). For each hit this tick that left its target worse off (calcDamageState of the health before and after:
// IS_CONDITION_WORSE) while the damage's source is still there: each of the target's TransitionDamageFX modules, slot by
// slot, runs the new state's creation list unless the body's last damage type is not one it plays for
// (DamageOCLTypes), at its place (getLocalEffectPos: a random bone's family member picked on the logic stream) turned
// and moved with the object (transformBoneToWorld), the object as primary and the damage source's position as the
// secondary point. What runs goes out as TransitionCreationEvents (ApplyTransitionCreations carries them out). It runs
// once the tick's simulation is done (as BoneFxSystem): the object and the source where they are then, a source that
// died this tick still there (its removal follows the tick), as within the hit; a building, the only kind the data could
// give these lists, is where it was hit.
export namespace generalszh::gameplay
{
struct TransitionCreationEvent
{
	ecs::Entity object;
	Engine::Math::FixedVector3 position;
	Engine::Math::FixedVector3 secondary; // the damage source's position
	Engine::Math::TurnAngle facing;
	std::uint32_t definition{0};
	std::uint32_t team{0xFFFFFFFFu};
	std::uint32_t veterancy{0};
	std::uint16_t module{0};
	std::uint8_t state{0};
	std::uint8_t slot{0};
};

// The tick's transition creation lists (a batch system's output: spent by ApplyTransitionCreations).
struct TransitionCreationEvents
{
	std::vector<TransitionCreationEvent> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::TransitionCreationEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.transition_creation_events";
};
}

export namespace generalszh::gameplay
{
struct TransitionCreationSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Health>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::TeamMember>, ecs::Read<engine::gameplay::Experience>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::Hits>, ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::RandomSeed>,
		ecs::Write<TransitionCreationEvents>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using Engine::Math::Fixed;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const auto &gameData = templates.Content().gameData;
		const auto lookup = context.Lookup<Lookup>();
		auto &out = context.Write<TransitionCreationEvents>().list;
		const std::uint64_t seed = context.Read<gp::RandomSeed>().value ^ 0x7DFCu;
		std::uint32_t draws = 0;
		context.Read<gp::Hits>().ForEach([&](const gp::Hit &hit) {
			if (hit.handled)
				return;
			const gp::DefinitionRef *ref = lookup.Get<gp::DefinitionRef>(hit.target);
			if (ref == nullptr)
				return;
			const content::TransitionCreations *creations = templates.TransitionCreationsOf(ref->index);
			if (creations == nullptr)
				return;
			const auto state = [&](Fixed current) {
				return bone_fx_detail::BodyState(gp::Health{.current = current, .maximum = hit.maximum}, gameData.unitDamaged, gameData.unitReallyDamaged);
			};
			const std::uint8_t before = state(hit.before), after = state(hit.after);
			// IS_CONDITION_WORSE; and only with the damage's source still there (findObjectByID).
			if (after <= before || !lookup.IsAlive(hit.source))
				return;
			const gp::Transform *source = lookup.Get<gp::Transform>(hit.source);
			const gp::Transform *frame = lookup.Get<gp::Transform>(hit.target);
			if (source == nullptr || frame == nullptr)
				return;
			const gp::TeamMember *member = lookup.Get<gp::TeamMember>(hit.target);
			const gp::Experience *experience = lookup.Get<gp::Experience>(hit.target);
			const bool plays = hit.lastDamageType >= 64;
			for (std::size_t module = 0; module < creations->modules.size(); ++module)
			{
				const content::TransitionCreationModule &config = creations->modules[module];
				if (!plays && (config.types & (std::uint64_t{1} << hit.lastDamageType)) == 0)
					continue;
				for (std::size_t slot = 0; slot < content::TransitionSlots; ++slot)
				{
					const content::TransitionCreation &creation = config.slots[after][slot];
					if (!creation.Used())
						continue;
					std::size_t pick = 0;
					if (creation.at.size() > 1)
					{
						// RandomValueInt(LogicRandomValueClass, 0, boneCount - 1).
						auto stream = Engine::Math::Stream(seed, {context.Tick(), hit.target.index, hit.target.generation, ++draws});
						pick = static_cast<std::size_t>(Engine::Math::UniformInt(stream, 0, static_cast<std::int64_t>(creation.at.size()) - 1));
					}
					out.push_back({hit.target, bone_fx_detail::ToWorld(*frame, creation.at[pick]), source->position, frame->facing, ref->index,
						member != nullptr ? member->team : 0xFFFFFFFFu, experience != nullptr ? experience->level : 0u,
						static_cast<std::uint16_t>(module), after, static_cast<std::uint8_t>(slot)});
				}
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::TransitionCreationSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.transition_creation";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::StructureToppleSystem, engine::gameplay::SlowDeathSystem>;
};
}
