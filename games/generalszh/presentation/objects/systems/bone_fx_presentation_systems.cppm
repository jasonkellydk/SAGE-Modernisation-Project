export module games.generalszh.presentation.objects.systems.bone_fx_presentation_systems;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.effects.components.bone_fx;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.health.components.health;
export import games.generalszh.presentation.objects.components.effect_attachments;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.systems.effect_attachment_systems;
import Engine.Core.Math.FixedPresentation;

// BoneFXUpdate's particle systems, which the original times in its game-logic update but with the client's random
// numbers:
//   BoneParticleSystem, once a tick after the simulation: when the simulation stopped its bone effects
//   (stopAllBoneFX) or timed its damage state's slots anew (initTimes: its first update, a change of damage state),
//   the running systems go (killRunningParticleSystems: ParticleSystem::destroy, what is out lives on) and, for new timings, each used
//   particle slot is timed from now (a delay between its min and max, truncated); then each due slot starts its system
//   at its bone, riding on the object (doParticleSystemAtBone; switched off for good when the object is hidden: contained),
//   unless the last damage's type is not one of its DamageParticleTypes, and is timed again (OnlyOnce: never).
//   BoneFxRideSystem, each frame after the objects are presented: the systems follow their objects; an object no
//   longer presented loses its systems, as other systems riding on objects do, unless the shroud hides it (then they
//   are kept, emitting nothing).
export namespace generalszh::presentation
{
struct BoneParticleSystem
{
	using Query = ecs::Query<ecs::Read<generalszh::gameplay::BoneFx>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>,
		ecs::Optional<engine::gameplay::Health>, ecs::Optional<engine::gameplay::OffMap>>;
	using SideTables = ecs::SideTables<ecs::Write<BoneFxEmission>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Write<ParticleWorldHandle>, ecs::Write<PresentationRandom>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		if (particles.world == nullptr || particles.content == nullptr)
			return;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &random = context.Write<PresentationRandom>().engine;
		auto &emissions = context.Side<SideTables, BoneFxEmission>();
		auto &commands = context.Commands();
		const auto now = static_cast<std::int64_t>(context.Tick());
		// REAL_TO_INT(GameClientRandomVariable UNIFORM getValue()).
		const auto delay = [&](const content::BoneFxEntry &entry) {
			const float low = Engine::Math::ToFloat(entry.minDelay), high = Engine::Math::ToFloat(entry.maxDelay);
			const float value = high > low ? std::uniform_real_distribution<float>(low, high)(random) : low;
			return static_cast<std::int64_t>(value);
		};
		query.ForEachChunk([&](auto chunk) {
			const auto effects = chunk.template Get<generalszh::gameplay::BoneFx>();
			const auto refs = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto healths = chunk.template Get<engine::gameplay::Health>();
			const auto hidden = chunk.template Get<engine::gameplay::OffMap>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < effects.size(); ++row)
			{
				const DefinitionLooks *looks = catalog.Of(refs[row].index);
				if (looks == nullptr || !looks->boneParticles)
					continue;
				const content::BoneFxTable &table = *looks->boneParticles;
				const generalszh::gameplay::BoneFx &fx = effects[row];
				BoneFxEmission *known = emissions.Get(entities[row]);
				BoneFxEmission fresh;
				BoneFxEmission &emission = known != nullptr ? *known : fresh;
				const auto kill = [&] {
					for (const AttachedSystem &attached : emission.systems)
						particles.world->Stop(attached.id);
					emission.systems.clear();
				};
				if (emission.stops != fx.stops)
				{
					kill();
					emission.next.fill(BoneFxEmission::Off);
					emission.stops = fx.stops;
				}
				if (emission.timings != fx.timings)
				{
					kill();
					for (std::size_t slot = 0; slot < content::BoneFxSlots; ++slot)
					{
						const content::BoneFxEntry &entry = table[fx.state][slot];
						emission.next[slot] = entry.Used() ? now + delay(entry) : BoneFxEmission::Off;
					}
					emission.timings = fx.timings;
				}
				const std::uint32_t lastType = healths.empty() ? 0u : healths[row].lastDamageType;
				const bool plays = lastType >= 64 || (looks->boneParticleTypes & (std::uint64_t{1} << lastType)) != 0;
				const auto &frame = transforms[row];
				PresentedObject at;
				at.position = {Engine::Math::ToFloat(frame.position.x), Engine::Math::ToFloat(frame.position.y), Engine::Math::ToFloat(frame.position.z)};
				at.facing = static_cast<float>(frame.facing.units) * 6.283185307179586f / 4294967296.0f;
				at.scale = 1.0f;
				for (std::size_t slot = 0; slot < content::BoneFxSlots; ++slot)
				{
					if (emission.next[slot] == BoneFxEmission::Off || emission.next[slot] > now)
						continue;
					const content::BoneFxEntry &entry = table[fx.state][slot];
					if (plays)
						if (const auto *definition = particles.content->particles.Find(entry.name))
						{
							AttachedSystem attached{0, {Engine::Math::ToFloat(entry.at.x), Engine::Math::ToFloat(entry.at.y), Engine::Math::ToFloat(entry.at.z)}, 0.0f};
							attached.id = particles.world->Create(*definition, effect_attachment_detail::Place(at, attached));
							// doParticleSystemAtBone: on a hidden drawable (contained) it is stop()ped: kept, emitting
							// nothing; nothing in BoneFXUpdate starts it again (only destroy() ends it).
							if (!hidden.empty())
							{
								particles.world->Hold(attached.id);
								particles.world->Pause(attached.id);
							}
							emission.systems.push_back(attached);
						}
					emission.next[slot] = entry.onlyOnce ? BoneFxEmission::Off : now + delay(entry);
				}
				if (known == nullptr)
					commands.Add<BoneFxEmission>(entities[row], std::move(fresh));
			}
		});
	}
};

struct BoneFxRideSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<BoneFxEmission>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::ObjectShroud>, ecs::Read<engine::gameplay::Transform>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<PresentedObjects>, ecs::Write<ParticleWorldHandle>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		if (particles.world == nullptr)
			return;
		const std::uint32_t serial = context.Read<PresentationFrame>().frame;
		auto &emissions = context.Side<SideTables, BoneFxEmission>();
		const auto lookup = context.Lookup<Lookup>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		// doParticleSystemAtBone attaches them to the object: nothing emitted while the viewer sees it fogged or shrouded
		// (ParticleSystem::update's isShrouded); kept meanwhile.
		const auto shrouded = [&](ecs::Entity entity) {
			return lookup.IsAlive(entity) && effect_attachment_detail::ShroudedFromViewer(lookup.Get<engine::gameplay::ObjectShroud>(entity), viewer);
		};
		context.Read<PresentedObjects>().ForEach([&](const PresentedObject &object) {
			if (BoneFxEmission *emission = emissions.Get(object.entity))
			{
				emission->seenFrame = serial;
				for (const AttachedSystem &attached : emission->systems)
					particles.world->Move(attached.id, effect_attachment_detail::Place(object, attached));
				effect_attachment_detail::ObscureSystems(*particles.world, emission->systems, shrouded(object.entity));
			}
		});
		for (std::size_t index = 0; index < emissions.Size(); ++index)
			if (BoneFxEmission &emission = emissions.Value(index); emission.seenFrame != serial && !emission.systems.empty())
			{
				if (const ecs::Entity entity = emissions.Entities()[index]; shrouded(entity))
				{
					// The bone's place is not scaled (doParticleSystemAtBone's setPosition): scale 1.
					effect_attachment_detail::FollowHidden(*particles.world, emission.systems, lookup.Get<engine::gameplay::Transform>(entity), 1.0f);
					continue;
				}
				effect_attachment_detail::Remove(*particles.world, emission.systems);
			}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::BoneParticleSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.bone_particles";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::BoneFxRideSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.bone_fx_ride";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
