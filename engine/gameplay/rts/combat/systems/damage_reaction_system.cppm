export module engine.gameplay.rts.combat.systems.damage_reaction_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.combat.components.damage_reaction;
export import engine.gameplay.rts.combat.systems.auto_fire_system;
export import engine.gameplay.common.health.systems.health_system;
import Engine.Core.Math.FixedRandom;

// FireWeaponWhenDamagedBehavior::onDamage and update, after the tick's damage: each hit of a damage type it reacts to,
// at least its threshold, fires the reaction weapon of its damage state (if ready); then the continuous weapon of its
// damage state fires (if ready). Weapons fire at its own position (forceFireWeapon); their shots join the shot queue
// and land with the next tick's impacts (the original deals them on the spot, inside the damage that set them off).
export namespace engine::gameplay
{
struct DamageReactionSystem
{
	using Query = ecs::Query<ecs::Write<DamageReaction>, ecs::Read<Transform>, ecs::Read<Health>, ecs::Optional<Owner>, ecs::Optional<WeaponBonusConditions>,
		ecs::Exclude<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<Hits>, ecs::Read<RandomSeed>, ecs::Read<WeaponCatalog>, ecs::Write<ShotQueue>>;

	// ActiveBody::calcDamageState: 0 pristine, 1 damaged, 2 really damaged, 3 rubble.
	static std::size_t DamageState(const Health &health, const DamageReaction &reaction) noexcept
	{
		using Engine::Math::Fixed;
		if (health.maximum <= Fixed{})
			return 0;
		if (health.current > health.maximum * reaction.damaged)
			return 0;
		if (health.current > health.maximum * reaction.reallyDamaged)
			return 1;
		return health.current > Fixed{} ? 2 : 3;
	}

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		ShotQueue &queue = context.Write<ShotQueue>();
		const std::uint64_t seed = context.Read<RandomSeed>().value ^ 0xFD0Au;
		const std::uint64_t tick = context.Tick();
		const auto key = [](ecs::Entity entity) { return (static_cast<std::uint64_t>(entity.index) << 32) | entity.generation; };
		// The tick's hits by who took them, in order.
		std::vector<std::pair<std::uint64_t, Hit>> hits;
		context.Read<Hits>().ForEach([&](const Hit &hit) { hits.emplace_back(key(hit.target), hit); });
		std::stable_sort(hits.begin(), hits.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		query.ForEachChunk([&](auto chunk) {
			auto reactions = chunk.template Get<DamageReaction>();
			const auto transforms = chunk.template Get<Transform>();
			const auto healths = chunk.template Get<Health>();
			const auto owners = chunk.template Get<Owner>();
			const auto bonuses = chunk.template Get<WeaponBonusConditions>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < reactions.size(); ++row)
			{
				DamageReaction &reaction = reactions[row];
				if (reaction.active == 0)
					continue;
				const auto at = transforms[row].position;
				// Weapon::fireWeapon when READY_TO_FIRE: its delay (or its clip's reload) until the next.
				std::uint32_t fired = 0;
				const auto fire = [&](ReactionWeapon &slot) {
					if (slot.weapon == ReactionWeapon::None || slot.weapon == WeaponCatalog::None || tick < slot.readyTick)
						return;
					const WeaponDefinition &weapon = weapons.At(slot.weapon);
					const WeaponBonus bonus = weapons.Bonus(weapon, bonuses.empty() ? 0u : bonuses[row].Effective());
					Shot shot{entities[row], {}, slot.weapon, owners.empty() ? 0u : owners[row].player, at, at, tick, tick};
					shot.damageScale = bonus.Get(WeaponBonusField::Damage);
					shot.radiusScale = bonus.Get(WeaponBonusField::Radius);
					queue.Add(shot);
					auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation, ++fired});
					const auto delay = static_cast<std::uint64_t>(Engine::Math::UniformInt(random, static_cast<std::int64_t>(weapon.delayMin),
						static_cast<std::int64_t>(std::max(weapon.delayMin, weapon.delayMax))));
					slot.readyTick = tick + std::max<std::uint64_t>(BonusDelay(delay, bonus), 1);
					if (weapon.clipSize > 0)
					{
						if (slot.clip == 0 || slot.clip > weapon.clipSize)
							slot.clip = weapon.clipSize;
						if (--slot.clip == 0)
						{
							slot.clip = weapon.clipSize;
							slot.readyTick = tick + std::max<std::uint64_t>(BonusDelay(weapon.clipReload, bonus), 1);
						}
					}
				};
				const std::uint64_t self = key(entities[row]);
				auto hit = std::lower_bound(hits.begin(), hits.end(), self, [](const auto &entry, std::uint64_t value) { return entry.first < value; });
				for (; hit != hits.end() && hit->first == self; ++hit)
				{
					const Hit &taken = hit->second;
					// onDamage runs only when health went down: a handled hit (subdual, a pilot kill) takes none.
					if (taken.handled)
						continue;
					if (taken.damageType >= 64 || (reaction.damageTypes & (std::uint64_t{1} << taken.damageType)) == 0 || taken.amount < reaction.threshold)
						continue;
					fire(reaction.reaction[DamageState(healths[row], reaction)]);
				}
				fire(reaction.continuous[DamageState(healths[row], reaction)]);
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::DamageReactionSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.damage_reaction";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The game orders it after the tick's damage (HealthSystem) and the other self-fired weapons (AutoFireSystem).
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
