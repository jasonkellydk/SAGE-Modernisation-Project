export module games.generalszh.gameplay.effects.systems.bone_fx_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import games.generalszh.gameplay.effects.components.bone_fx;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.rts.veterancy.components.experience;
export import engine.gameplay.rts.death.components.structure_topple;
export import engine.gameplay.rts.death.components.collapse;
export import engine.gameplay.rts.death.systems.structure_topple_system;
export import engine.gameplay.rts.death.systems.slow_death_system;
import Engine.Core.Math.FixedRandom;
import Engine.Core.Math.FixedAngle;

// BoneFXUpdate::update and BoneFXDamage::onBodyDamageStateChange for everything with bone effects, chunk-parallel,
// after the tick's damage: its body damage state by health (ActiveBody::calcDamageState: above GameData's
// UnitDamagedThreshold of its most pristine, above UnitReallyDamagedThreshold damaged, above none really damaged, else
// rubble); its first update, or a change of state, times that state's used slots (initTimes: now plus a delay between
// its min and max, truncated). Then each slot in turn: its FX list, then its creation list, when due (a delay of 0
// plays the same tick) play at its bone in the world (transformBoneToWorld) unless the last damage's type is not one it
// plays for (the skip still re-times it), and re-time (OnlyOnce: never again). A topple or collapse finishing stops
// every slot (stopAllBoneFX). What plays goes out as BoneFxEvents (ApplyBoneFx carries them out).
export namespace generalszh::gameplay
{
struct BoneFxEvent
{
	enum class Kind : std::uint8_t
	{
		FxList,
		CreationList,
	};
	ecs::Entity source;
	Engine::Math::FixedVector3 position;
	Engine::Math::TurnAngle facing;
	std::uint32_t definition{0};
	std::uint32_t team{0xFFFFFFFFu};
	std::uint32_t veterancy{0};
	std::uint8_t state{0};
	std::uint8_t slot{0};
	Kind kind{Kind::FxList};
};

struct BoneFxEvents : ecs::ChunkOutputs<BoneFxEvent>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::BoneFxEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.bone_fx_events";
};
}

export namespace generalszh::gameplay
{
namespace bone_fx_detail
{
using Engine::Math::Fixed;

// ActiveBody::calcDamageState.
inline std::uint8_t BodyState(const engine::gameplay::Health &health, Fixed damaged, Fixed reallyDamaged) noexcept
{
	if (health.maximum <= Fixed{})
		return 0;
	if (health.current > health.maximum * damaged)
		return 0;
	if (health.current > health.maximum * reallyDamaged)
		return 1;
	return health.current > Fixed{} ? 2 : 3;
}

// REAL_TO_INT(GameLogicRandomVariable UNIFORM getValue()): a delay between min and max, truncated.
inline std::int64_t Delay(const content::BoneFxEntry &entry, Engine::Math::RandomStream &random)
{
	const Fixed value = entry.maxDelay > entry.minDelay ? Engine::Math::UniformFixed(random, entry.minDelay, entry.maxDelay) : entry.minDelay;
	return value.Floor();
}

// transformBoneToWorld: the bone at rest turned with the object and moved to where it is.
inline Engine::Math::FixedVector3 ToWorld(const engine::gameplay::Transform &frame, const Engine::Math::FixedVector3 &local)
{
	const Fixed c = Engine::Math::Cos(frame.facing), s = Engine::Math::Sin(frame.facing);
	return {frame.position.x + local.x * c - local.y * s, frame.position.y + local.x * s + local.y * c, frame.position.z + local.z};
}
}

struct BoneFxSystem
{
	using Query = ecs::Query<ecs::Write<BoneFx>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>,
		ecs::Optional<engine::gameplay::Health>, ecs::Optional<engine::gameplay::TeamMember>, ecs::Optional<engine::gameplay::Experience>,
		ecs::Optional<engine::gameplay::StructureTopple>, ecs::Optional<engine::gameplay::Collapse>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::RandomSeed>, ecs::Write<BoneFxEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<BoneFxEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using namespace bone_fx_detail;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const auto &gameData = templates.Content().gameData;
		auto &out = context.Write<BoneFxEvents>().Slot(context);
		const std::uint64_t seed = context.Read<engine::gameplay::RandomSeed>().value ^ 0xB0FEu;
		const auto now = static_cast<std::int64_t>(context.Tick());
		auto effects = chunk.Get<BoneFx>();
		const auto refs = chunk.Get<engine::gameplay::DefinitionRef>();
		const auto transforms = chunk.Get<engine::gameplay::Transform>();
		const auto healths = chunk.Get<engine::gameplay::Health>();
		const auto members = chunk.Get<engine::gameplay::TeamMember>();
		const auto experiences = chunk.Get<engine::gameplay::Experience>();
		const auto topples = chunk.Get<engine::gameplay::StructureTopple>();
		const auto collapses = chunk.Get<engine::gameplay::Collapse>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < effects.size(); ++row)
		{
			const content::BoneFxContent *config = templates.BoneFxOf(refs[row].index);
			if (config == nullptr)
				continue;
			BoneFx &fx = effects[row];
			std::uint32_t draws = 0;
			auto random = [&] { return Engine::Math::Stream(seed, {context.Tick(), entities[row].index, entities[row].generation, ++draws}); };
			const auto initTimes = [&] {
				for (std::size_t slot = 0; slot < content::BoneFxSlots; ++slot)
				{
					const content::BoneFxEntry &list = config->fx[fx.state][slot];
					const content::BoneFxEntry &creation = config->ocl[fx.state][slot];
					auto stream = random();
					fx.nextFx[slot] = list.Used() ? now + Delay(list, stream) : BoneFx::Off;
					fx.nextOcl[slot] = creation.Used() ? now + Delay(creation, stream) : BoneFx::Off;
				}
				++fx.timings;
			};
			// StructureToppleUpdate::doToppleDoneStuff / StructureCollapseUpdate::doCollapseDoneStuff: stopAllBoneFX.
			const bool done = (!topples.empty() && topples[row].state == engine::gameplay::StructureToppleState::Done) ||
				(!collapses.empty() && collapses[row].state == engine::gameplay::CollapseState::Done);
			if (done && fx.done == 0)
			{
				fx.done = 1;
				fx.nextFx.fill(BoneFx::Off);
				fx.nextOcl.fill(BoneFx::Off);
				++fx.stops;
			}
			// BoneFXDamage::onBodyDamageStateChange -> changeBodyDamageState: the new state's slots timed from now.
			const std::uint8_t state = healths.empty() ? 0 : BodyState(healths[row], gameData.unitDamaged, gameData.unitReallyDamaged);
			if (state != fx.state)
			{
				fx.state = state;
				initTimes();
			}
			if (fx.active == 0)
			{
				initTimes();
				fx.active = 1;
			}
			const std::uint32_t lastType = healths.empty() ? 0u : healths[row].lastDamageType; // getLastDamageInfo (never hurt: EXPLOSION)
			const auto plays = [&](std::uint64_t types) { return lastType >= 64 || (types & (std::uint64_t{1} << lastType)) != 0; };
			const auto retime = [&](const content::BoneFxEntry &entry, std::int64_t &next) {
				auto stream = random();
				next = entry.onlyOnce ? BoneFx::Off : now + Delay(entry, stream);
			};
			const auto emit = [&](const content::BoneFxEntry &entry, std::size_t slot, BoneFxEvent::Kind kind) {
				out.push_back({entities[row], ToWorld(transforms[row], entry.at), transforms[row].facing, refs[row].index,
					members.empty() ? 0xFFFFFFFFu : members[row].team, experiences.empty() ? 0u : experiences[row].level, fx.state,
					static_cast<std::uint8_t>(slot), kind});
			};
			for (std::size_t slot = 0; slot < content::BoneFxSlots; ++slot)
			{
				if (fx.nextFx[slot] != BoneFx::Off && fx.nextFx[slot] <= now)
				{
					const content::BoneFxEntry &list = config->fx[fx.state][slot];
					if (plays(config->fxTypes))
						emit(list, slot, BoneFxEvent::Kind::FxList);
					retime(list, fx.nextFx[slot]);
				}
				if (fx.nextOcl[slot] != BoneFx::Off && fx.nextOcl[slot] <= now)
				{
					const content::BoneFxEntry &creation = config->ocl[fx.state][slot];
					if (plays(config->oclTypes))
						emit(creation, slot, BoneFxEvent::Kind::CreationList);
					retime(creation, fx.nextOcl[slot]);
				}
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::BoneFxSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.bone_fx";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::StructureToppleSystem, engine::gameplay::SlowDeathSystem>;
};
}
