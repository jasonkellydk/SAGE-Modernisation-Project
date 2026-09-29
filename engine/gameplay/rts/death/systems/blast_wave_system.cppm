export module engine.gameplay.rts.death.systems.blast_wave_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.death.components.blast_wave;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.death.definitions.death_definition;
export import engine.gameplay.rts.death.resources.blast_waves;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.health.resources.incoming_damage;

// NeutronMissileSlowDeathBehavior::update, doBlast and doScorchBlast (GeneralsMD/Code/GameEngine/Source/GameLogic/
// Object/Update/NeutronMissileSlowDeathUpdate.cpp), for the dying with blast waves (see BlastWaveDefinition): the
// blasts and scorch waves due this tick go off, before the tick's toppling. A blast's push is left for the toppling,
// its damage for BlastDamageSystem (the tick's incoming damage: the original deals it on the spot) and a scorch
// wave's burn for the ScorchSystem.
export namespace engine::gameplay
{
struct BlastWaveSystem
{
	using Query = ecs::Query<ecs::Read<Dying>, ecs::Write<BlastWave>, ecs::Read<Transform>>;
	using Resources = ecs::Resources<ecs::Read<DeathCatalog>, ecs::Read<SpatialIndex>, ecs::Write<BlastWaves>>;

	// doBlast: full damage within the inner radius, falling off beyond (the original's 0.01 keeps it off a zero
	// width), never below the least.
	static Engine::Math::Fixed Damage(const BlastDefinition &blast, Engine::Math::Fixed distance) noexcept
	{
		using Engine::Math::Fixed;
		if (distance <= blast.innerRadius)
			return blast.maxDamage;
		const Fixed share = Fixed::One() - (distance - blast.innerRadius) / (blast.outerRadius - blast.innerRadius + Fixed::FromRatio(1, 100));
		return std::max(blast.maxDamage * share, blast.minDamage);
	}

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const DeathCatalog &catalog = context.Read<DeathCatalog>();
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		BlastWaves &waves = context.Write<BlastWaves>();
		waves.Clear();
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			const auto dyings = chunk.template Get<Dying>();
			auto progress = chunk.template Get<BlastWave>();
			const auto transforms = chunk.template Get<Transform>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < dyings.size(); ++row)
			{
				const Dying &dying = dyings[row];
				const DeathDefinition &definition = catalog.At(dying.death);
				if (dying.slow >= definition.slow.size())
					continue;
				const BlastWaveDefinition &wave = definition.slow[dying.slow].wave;
				BlastWave &state = progress[row];
				const auto center = transforms[row].position;
				const std::uint64_t elapsed = tick - dying.since;
				for (std::size_t index = 0; index < wave.blasts.size() && index < 16; ++index)
				{
					const BlastDefinition &blast = wave.blasts[index];
					const auto bit = static_cast<std::uint16_t>(1u << index);
					if ((state.blasted & bit) == 0 && elapsed >= blast.blastAfter)
					{
						state.blasted |= bit;
						if (blast.outerRadius != Engine::Math::Fixed{})
						{
							waves.pushes.push_back({center, blast.outerRadius, blast.toppleSpeed});
							spatial.ForEachWithin(center.XY(), blast.outerRadius, [&](const SpatialEntry &entry) {
								if (!BlastWaves::Reaches(center, blast.outerRadius, entry.position))
									return;
								const Engine::Math::Fixed amount = Damage(blast, Engine::Math::Length(entry.position - center));
								if (amount == Engine::Math::Fixed{})
									return;
								waves.damage.push_back({entry.entity, entities[row], amount, wave.damageType, wave.deathType});
								if (state.scorchPlaced == 0)
								{
									waves.marks.push_back({entities[row], center, wave.scorchSize});
									state.scorchPlaced = 1;
								}
							});
						}
					}
					if ((state.scorched & bit) == 0 && elapsed >= blast.scorchAfter)
					{
						state.scorched |= bit;
						if (blast.outerRadius != Engine::Math::Fixed{})
							waves.burns.push_back({center, blast.outerRadius});
					}
				}
			}
		});
	}
};

// The tick's blast damage joins its incoming damage (after the impacts fill it, before it is taken).
struct BlastDamageSystem
{
	using Query = ecs::Query<ecs::Read<BlastWave>>;
	using Resources = ecs::Resources<ecs::Read<BlastWaves>, ecs::Write<IncomingDamage>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const auto &damage = context.Read<BlastWaves>().damage;
		if (damage.empty())
			return;
		IncomingDamage &incoming = context.Write<IncomingDamage>();
		for (const DamageRecord &record : damage)
			incoming.Add(record);
		incoming.Seal();
	}
};

// doScorchBlast: everything a scorch wave reaches looks burned (setModelConditionState(MODELCONDITION_BURNED)), for
// good.
struct ScorchSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Exclude<Scorched>, ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<BlastWaves>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const auto &burns = context.Read<BlastWaves>().burns;
		if (burns.empty())
			return;
		const auto transforms = chunk.Get<Transform>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < transforms.size(); ++row)
			for (const BlastBurn &burn : burns)
				if (BlastWaves::Reaches(burn.center, burn.radius, transforms[row].position))
				{
					context.Commands().Add<Scorched>(entities[row], Scorched{});
					break;
				}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::BlastWaveSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.blast_waves";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The game orders it before the toppling.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<engine::gameplay::BlastDamageSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.blast_damage";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The game orders it after the tick's impacts, before the damage is taken.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::BlastWaveSystem>;
};

template<>
struct SystemTraits<engine::gameplay::ScorchSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.scorch";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::BlastWaveSystem>;
};
}
