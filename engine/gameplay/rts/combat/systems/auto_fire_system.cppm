export module engine.gameplay.rts.combat.systems.auto_fire_system;
import std;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.rts.combat.components.auto_fire;
export import engine.gameplay.rts.combat.systems.impact_system;
import Engine.Core.Math.FixedRandom;

// Entities that fire a weapon at themselves when it is ready, in parallel
// per chunk; the shots join the shot queue after the chunks (on the caller,
// in chunk order) and land in this tick's impacts.
export namespace engine::gameplay
{
struct AutoShots : ecs::ChunkOutputs<Shot>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::AutoShots>
{
	static constexpr std::string_view StableName = "engine.gameplay.auto_shots";
};
}

export namespace engine::gameplay
{
struct AutoFireSystem
{
	using Query = ecs::Query<ecs::Write<AutoFire>, ecs::Read<Transform>, ecs::Optional<Owner>, ecs::Optional<Armament>, ecs::Exclude<OffMap>, ecs::Optional<Disabled>,
		ecs::Optional<WeaponBonusConditions>>;
	using Resources = ecs::Resources<ecs::Read<RandomSeed>, ecs::Read<WeaponCatalog>, ecs::Write<AutoShots>, ecs::Write<ShotQueue>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<AutoShots>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0xA070u;
		auto &shots = context.Write<AutoShots>().Slot(context);
		const std::uint64_t tick = context.Tick();
		auto fires = chunk.Get<AutoFire>();
		const auto transforms = chunk.Get<Transform>();
		const auto owners = chunk.Get<Owner>();
		const auto armaments = chunk.Get<Armament>();
		const auto entities = chunk.Entities();
		const auto disabledRows = chunk.Get<Disabled>();
		const auto bonusRows = chunk.Get<WeaponBonusConditions>();
		for (std::size_t row = 0; row < fires.size(); ++row)
		{
			if (!disabledRows.empty() && !RunsWhileDisabled(disabledRows[row], disabled_type::None))
				continue;
			AutoFire &fire = fires[row];
			if (fire.weapon == WeaponCatalog::None || tick < fire.readyTick)
				continue;
			// Its own weapons firing hold this one back.
			if (fire.exclusiveDelay > 0 && !armaments.empty() && armaments[row].firedTick != 0 && tick < armaments[row].firedTick + fire.exclusiveDelay)
				continue;
			const WeaponDefinition &weapon = weapons.At(fire.weapon);
			const auto &at = transforms[row].position;
			const WeaponBonus bonus = weapons.Bonus(weapon, bonusRows.empty() ? 0u : bonusRows[row].Effective());
			Shot &shot = shots.emplace_back(Shot{entities[row], {}, fire.weapon, owners.empty() ? 0u : owners[row].player, at, at, tick, tick});
			shot.damageScale = bonus.Get(WeaponBonusField::Damage);
			shot.radiusScale = bonus.Get(WeaponBonusField::Radius);
			auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation});
			const auto delay = static_cast<std::uint64_t>(Engine::Math::UniformInt(random, static_cast<std::int64_t>(weapon.delayMin),
				static_cast<std::int64_t>(std::max(weapon.delayMin, weapon.delayMax))));
			fire.readyTick = tick + std::max<std::uint64_t>(BonusDelay(delay, bonus), 1);
			if (weapon.clipSize > 0)
			{
				if (fire.clip == 0 || fire.clip > weapon.clipSize)
					fire.clip = weapon.clipSize;
				if (--fire.clip == 0)
				{
					fire.clip = weapon.clipSize;
					fire.readyTick = tick + std::max<std::uint64_t>(BonusDelay(weapon.clipReload, bonus), 1);
				}
			}
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const
	{
		ShotQueue &queue = context.Write<ShotQueue>();
		context.Write<AutoShots>().ForEach([&](const Shot &shot) { queue.Add(shot); });
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::AutoFireSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.auto_fire";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// Its shots land this tick; after the entities' own weapons fired.
	using Before = SystemTypeList<engine::gameplay::ImpactSystem>;
	using After = SystemTypeList<engine::gameplay::WeaponSystem>;
};
}
