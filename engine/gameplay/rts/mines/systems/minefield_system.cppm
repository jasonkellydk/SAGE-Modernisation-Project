export module engine.gameplay.rts.mines.systems.minefield_system;
import std;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.weapons.components.armament;

export import engine.ecs.system.system;
export import engine.gameplay.rts.mines.components.minefield;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.components.health_floor;
export import engine.gameplay.common.health.resources.incoming_damage;
export import engine.gameplay.common.healing.components.healing;
export import engine.gameplay.common.lifetime.components.lifetime;
export import engine.gameplay.rts.combat.resources.shots;
export import engine.gameplay.rts.death.components.dying;
export import engine.ecs.system.chunk_outputs;

// MinefieldBehavior (GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/MinefieldBehavior.cpp), see Minefield.
// MineDrainSystem: a draining mine hurts itself (update: its most times DegenPercentPerSecond over the second's ticks,
// UNRESISTABLE, from itself), joining the tick's damage after the impacts.
// MinefieldSystem, once the tick's damage is taken: each mine scoots, checks on its producer, lets its health say how
// many of its virtual mines live (onDamage / onHealing), and goes off under what stands in it (onCollide). Chunk-parallel:
// a mine writes only its own row and reads the others through the spatial index and lookups; the shots its
// detonations fire are gathered per chunk and queued in chunk order once all have run (as one pass over the mines
// queued them), and its commands commit in chunk order.
export namespace engine::gameplay
{
struct MineShots : ecs::ChunkOutputs<Shot>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::MineShots>
{
	static constexpr std::string_view StableName = "engine.gameplay.mine_shots";
};
}

export namespace engine::gameplay
{
struct MineDrainSystem
{
	using Query = ecs::Query<ecs::Read<Minefield>, ecs::Read<Health>>;
	using Resources = ecs::Resources<ecs::Read<MineSettings>, ecs::Write<IncomingDamage>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const MineSettings &settings = context.Read<MineSettings>();
		IncomingDamage &incoming = context.Write<IncomingDamage>();
		bool any = false;
		query.ForEachChunk([&](auto chunk) {
			const auto mines = chunk.template Get<Minefield>();
			const auto healths = chunk.template Get<Health>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < mines.size(); ++row)
				if (mines[row].draining != 0 && !IsDead(healths[row]))
				{
					incoming.Add({entities[row], entities[row],
						healths[row].maximum * mines[row].drain / Engine::Math::Fixed::FromInt(100 * static_cast<std::int32_t>(settings.ticksPerSecond)),
						settings.unresistable, settings.normalDeath});
					any = true;
				}
		});
		if (any)
			incoming.Seal();
	}
};

struct MinefieldSystem
{
	using Query = ecs::Query<ecs::Write<Minefield>, ecs::Write<Transform>, ecs::Write<Health>, ecs::Read<Owner>, ecs::Read<Targetable>,
		ecs::Optional<Dying>>;
	using Lookup = ecs::Lookup<ecs::Read<Dying>, ecs::Read<MineSafe>, ecs::Read<Armament>, ecs::Read<AttackTarget>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<Relationships>, ecs::Read<GroundHeight>, ecs::Write<ShotQueue>, ecs::Write<MineShots>,
		ecs::Read<WeaponCatalog>>;

	// MIN_HEALTH: what a spent mine keeps.
	static Engine::Math::Fixed Least() noexcept { return Engine::Math::Fixed::FromRatio(1, 10); }

	// onDamage / onHealing: how many virtual mines its health says live (rounded up after damage, down after healing; none
	// at MIN_HEALTH or less).
	static std::uint32_t Expected(const Minefield &mine, const Health &health, bool healing) noexcept
	{
		if (health.maximum <= Engine::Math::Fixed{} || health.current <= Least())
			return 0;
		const Engine::Math::Fixed share = Engine::Math::Fixed::FromInt(static_cast<std::int32_t>(mine.total)) * health.current / health.maximum;
		const std::int64_t count = healing ? share.Floor() : share.Ceil();
		return static_cast<std::uint32_t>(std::clamp<std::int64_t>(count, 0, mine.total));
	}

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<MineShots>().Reset(query.PreparedChunkCount()); }

	void AfterChunks(Query &, ecs::SystemContext &context) const
	{
		ShotQueue &queue = context.Write<ShotQueue>();
		context.Write<MineShots>().ForEach([&](const Shot &shot) { queue.Add(shot); });
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const Relationships &relationships = context.Read<Relationships>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		auto &shots = context.Write<MineShots>().Slot(context);
		const auto lookup = context.Lookup<Lookup>();
		const WeaponCatalog &weapons = context.Read<WeaponCatalog>();
		auto &commands = context.Commands();
		const std::uint64_t tick = context.Tick();
		{
			auto mines = chunk.template Get<Minefield>();
			auto transforms = chunk.template Get<Transform>();
			auto healths = chunk.template Get<Health>();
			const auto owners = chunk.template Get<Owner>();
			const auto targetables = chunk.template Get<Targetable>();
			const auto dyings = chunk.template Get<Dying>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < mines.size(); ++row)
			{
				if (!dyings.empty())
					continue;
				Minefield &mine = mines[row];
				Transform &transform = transforms[row];
				Health &health = healths[row];
				const ecs::Entity self = entities[row];
				bool gone = false;
				// detonateOnce: its weapon where it went off, a virtual mine spent, its health down to match (a mine that does
				// not regenerate is removed once spent).
				const auto detonate = [&](Engine::Math::FixedVector3 at) {
					if (mine.weapon != Minefield::NoWeapon)
						shots.push_back(Shot{self, {}, mine.weapon, owners[row].player, at, at, tick, tick});
					if (mine.remaining > 0)
						--mine.remaining;
					if (mine.regenerates == 0 && mine.remaining == 0)
					{
						gone = true;
						return;
					}
					const Engine::Math::Fixed desired = std::max(health.maximum * Engine::Math::Fixed::FromInt(static_cast<std::int32_t>(mine.remaining)) /
							Engine::Math::Fixed::FromInt(static_cast<std::int32_t>(std::max<std::uint32_t>(mine.total, 1))), Least());
					if (health.current > desired)
						health.current = desired;
				};
				// Scooting out from where it was made (update): accelerating, down on the ground at the last.
				if (mine.scootLeft > 0)
				{
					mine.scootVelocity = mine.scootVelocity + mine.scootAcceleration;
					transform.position = transform.position + mine.scootVelocity;
					const Engine::Math::Fixed floor = ground.At(transform.position.XY());
					if (transform.position.z < floor || mine.scootLeft <= 1)
						transform.position.z = floor;
					--mine.scootLeft;
				}
				// Its producer gone: no more regenerating, no more healing, and it drains away.
				if (tick >= mine.nextCheck && mine.regenerates != 0 && mine.stopsRegen != 0)
				{
					mine.nextCheck = tick + mine.checkRate;
					if (mine.producer.IsValid() && (!lookup.IsAlive(mine.producer) || lookup.Get<Dying>(mine.producer) != nullptr))
					{
						mine.regenerates = 0;
						mine.draining = 1;
						commands.Remove<HealthFloor>(self);
						commands.Remove<SelfHealing>(self);
					}
				}
				// onDamage / onHealing: its live mines follow its health; damage beyond them sets them off where it is (a
				// draining mine's own drain spends them quietly).
				const bool healing = health.current > mine.lastHealth;
				for (std::uint32_t guard = 0; guard <= mine.total + 1 && !gone; ++guard)
				{
					const std::uint32_t expected = Expected(mine, health, healing);
					if (mine.remaining < expected)
						mine.remaining = expected;
					else if (mine.remaining > expected)
					{
						if (mine.draining != 0)
							--mine.remaining;
						else
							detonate(transform.position);
					}
					else
						break;
				}
				// update: an immunity lapses once its holder is gone or has not touched it for 2 frames.
				for (MineImmune &immune : mine.immunes)
					if (immune.who.IsValid() && (!lookup.IsAlive(immune.who) || tick > immune.touched + 2))
						immune = MineImmune{};
				// onCollide: what stands in it sets it off, once per move of RepeatDetonateMoveThresh.
				if (!gone && mine.remaining > 0)
				{
					const auto center = transform.position;
					spatial.ForEachWithin(center.XY(), mine.radius, [&](const SpatialEntry &entry) {
						if (gone || mine.remaining == 0 || entry.entity == self)
							return;
						if ((entry.classes & (target_class::AirborneVehicle | target_class::AirborneInfantry)) != 0)
							return;
						const Engine::Math::Fixed reach = mine.radius + entry.radius;
						if (Engine::Math::DistanceSquared(entry.position.XY(), center.XY()) > reach * reach)
							return;
						// First, one immune to it (its immunity kept up while it keeps touching it).
						for (MineImmune &immune : mine.immunes)
							if (immune.who == entry.entity)
							{
								immune.touched = tick;
								return;
							}
						const Relationship relation = relationships.Between(owners[row].player, entry.player);
						const std::uint8_t bit = relation == Relationship::Allies ? 1 : relation == Relationship::Enemies ? 2 : 4;
						if ((mine.detonatedBy & bit) == 0)
							return;
						if (mine.workersDetonate == 0 && lookup.Get<MineSafe>(entry.entity) != nullptr)
							return;
						// Still scooting: not live yet.
						if (mine.scootLeft > 0)
							return;
						// One clearing mines (attacking something with a WEAPON_ANTI_MINE weapon in hand: isClearingMines, with a
						// goal object) is made immune to it for as long as it keeps touching it (a free place, or its own).
						const Armament *armament = lookup.Get<Armament>(entry.entity);
						const AttackTarget *attack = lookup.Get<AttackTarget>(entry.entity);
						if (armament != nullptr && attack != nullptr && attack->target.IsValid() && attack->atPosition == 0 &&
							armament->weapon != WeaponCatalog::None && (weapons.At(armament->weapon).anti & weapon_anti::Mine) != 0)
						{
							for (MineImmune &immune : mine.immunes)
								if (!immune.who.IsValid() || immune.who == entry.entity)
								{
									immune = {entry.entity, tick};
									break;
								}
							return;
						}
						MineDetonator *known = nullptr;
						for (std::uint8_t index = 0; index < mine.detonatorCount; ++index)
							if (mine.detonators[index].who == entry.entity)
								known = &mine.detonators[index];
						if (known != nullptr)
						{
							if (Engine::Math::DistanceSquared(entry.position, known->where) <= mine.repeatThreshold * mine.repeatThreshold)
								return;
							known->where = entry.position;
						}
						else
						{
							mine.detonators[mine.nextDetonator] = {entry.entity, entry.position};
							mine.nextDetonator = static_cast<std::uint8_t>((mine.nextDetonator + 1) % Minefield::MaxDetonators);
							mine.detonatorCount = static_cast<std::uint8_t>(std::min<std::size_t>(mine.detonatorCount + 1u, Minefield::MaxDetonators));
						}
						// Where it touched the mine (clipPointToFootprint).
						Engine::Math::FixedVector3 at = entry.position;
						const Engine::Math::FixedVector2 away = at.XY() - center.XY();
						const Engine::Math::Fixed distance = Engine::Math::Length(away);
						if (distance > mine.radius && distance > Engine::Math::Fixed{})
						{
							at.x = center.x + away.x * mine.radius / distance;
							at.y = center.y + away.y * mine.radius / distance;
						}
						detonate(at);
					});
				}
				if (gone)
				{
					// destroyObject: gone without dying.
					commands.Set<Lifetime>(self, Lifetime{tick, 1, 0});
					mine.remaining = 0;
				}
				// Spent: rubble to look at and not to be attacked (MASKED).
				const std::uint8_t masked = mine.remaining == 0 ? 1 : 0;
				if (masked != mine.masked)
				{
					mine.masked = masked;
					Targetable target = targetables[row];
					target.classes = masked != 0 ? target.classes | target_class::Unattackable : target.classes & ~target_class::Unattackable;
					commands.Set<Targetable>(self, target);
				}
				mine.lastHealth = health.current;
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::MineDrainSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.mine_drain";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The game orders it after the tick's impacts, before the damage is taken.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<engine::gameplay::MinefieldSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.minefields";
	// Its rows are independent: large chunks are shared out in pieces of 32 rows.
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The game orders it after the damage is taken (HealthSystem).
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
