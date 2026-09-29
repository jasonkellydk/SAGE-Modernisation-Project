export module engine.gameplay.common.health.systems.health_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.components.health_floor;
export import engine.gameplay.common.health.components.damage_scalar;
export import engine.gameplay.common.health.components.second_life;
export import engine.gameplay.common.health.components.subdual;
export import engine.gameplay.common.health.resources.armor_catalog;
export import engine.gameplay.common.health.resources.incoming_damage;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;

// Applies this tick's damage through each entity's armor, in parallel per
// chunk, and reports who died (in chunk order). Removing the dead and
// their effects happen after the step. Subdual damage (ActiveBody::attemptDamage's
// IsSubdualDamage case) never touches health: a body that can be subdued adds it to
// its subdual damage (0..its cap; a hit wakes its SubdualDamageHelper afresh), any
// other body ignores it altogether (no hit, no attacker noted).
export namespace engine::gameplay
{
struct Death
{
	ecs::Entity entity;
	ecs::Entity killer;
	std::uint32_t damageType{0};
	std::uint32_t deathType{0};
	// The killing blow's damage beyond what health was left, as a share of
	// maximum health (big overkill favours violent deaths).
	Engine::Math::Fixed overkill;
	// Its health before the killing blow (ActiveBody::getPreviousHealth).
	Engine::Math::Fixed previous;
};

using Deaths = ecs::ChunkOutputs<Death>;

// Damage taken this tick (through armor, more than none), in the order dealt: what the game shows and plays for a
// hit (the original's ActiveBody::doDamageFX), the killing blow included. `armor` is the target's armor then. A
// handled damage type (ArmorCatalog::Handled) takes no health and is always told, whatever it amounts to: the game
// carries it out (`handled`).
struct Hit
{
	ecs::Entity target;
	ecs::Entity source;
	std::uint32_t damageType{0};
	std::uint32_t armor{0};
	Engine::Math::Fixed amount;
	std::uint32_t deathType{0};
	std::uint32_t fxType{0}; // the damage type whose effects it shows (its own unless overridden)
	bool handled{false};
	std::uint32_t statusType{DamageRecord::NoStatus}; // STATUS damage's object status bit
};

struct Hits : ecs::ChunkOutputs<Hit>
{
};

// The tick's second lives begun (UndeadBody::startSecondLife), in chunk order.
struct SecondLives : ecs::ChunkOutputs<SecondLifeStart>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::Deaths>
{
	static constexpr std::string_view StableName = "engine.gameplay.deaths";
};

template<>
struct ResourceTraits<engine::gameplay::Hits>
{
	static constexpr std::string_view StableName = "engine.gameplay.hits";
};

template<>
struct ResourceTraits<engine::gameplay::SecondLives>
{
	static constexpr std::string_view StableName = "engine.gameplay.second_lives";
};
}

export namespace engine::gameplay
{
struct HealthSystem
{
	using Query = ecs::Query<ecs::Write<Health>, ecs::Optional<HealthFloor>, ecs::OptionalWrite<Subdual>, ecs::Optional<DamageScalar>,
		ecs::OptionalWrite<SecondLife>>;
	using Lookup = ecs::Lookup<ecs::Read<Targetable>, ecs::Read<Owner>, ecs::Read<DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<IncomingDamage>, ecs::Read<ArmorCatalog>, ecs::Write<Deaths>, ecs::Write<Hits>, ecs::Write<SecondLives>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context)
	{
		context.Write<Deaths>().Reset(query.PreparedChunkCount());
		context.Write<Hits>().Reset(query.PreparedChunkCount());
		context.Write<SecondLives>().Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const IncomingDamage &incoming = context.Read<IncomingDamage>();
		const ArmorCatalog &armors = context.Read<ArmorCatalog>();
		Deaths &deaths = context.Write<Deaths>();
		if (incoming.Empty())
			return;
		auto healths = chunk.Get<Health>();
		const auto lookup = context.Lookup<Lookup>();
		const auto floors = chunk.Get<HealthFloor>();
		const auto scalars = chunk.Get<DamageScalar>();
		auto lives = chunk.Get<SecondLife>();
		auto &secondLives = context.Write<SecondLives>().Slot(context);
		auto subduals = chunk.Get<Subdual>();
		const auto entities = chunk.Entities();
		auto &died = deaths.Slot(context);
		auto &hits = context.Write<Hits>().Slot(context);
		for (std::size_t row = 0; row < healths.size(); ++row)
		{
			Health &health = healths[row];
			// ActiveBody::attemptDamage: an indestructible body takes nothing (and tells nothing).
			if (IsDead(health) || health.indestructible)
				continue;
			const auto records = incoming.For(entities[row]);
			if (records.empty())
				continue;
			const ArmorDefinition &armor = armors.At(health.armor);
			for (const DamageRecord &record : records)
			{
				const bool subdual = armors.Subdual(record.damageType);
				if (subdual && subduals.empty())
					continue;
				// A floored body keeps its last point: a highlander's hit is cut before armor weighs it (HighlanderBody::
				// attemptDamage), an immortal's loss after (ImmortalBody::internalChangeHealth).
				Engine::Math::Fixed amount = record.amount;
				if (!floors.empty() && !floors[row].Immortal() && record.damageType != floors[row].exempt)
					amount = std::min(amount, std::max(health.current - floors[row].least, Engine::Math::Fixed{}));
				// UndeadBody::attemptDamage (the fork's estimateDamage): a health-damaging, resistable hit the armor would let
				// through for all it has left, before its second life, leaves it a point, and then its second life begins.
				bool secondLife = false;
				if (!lives.empty() && lives[row].started == 0 && !subdual && !armors.Handled(record.damageType) && !armors.Unscaled(record.damageType) &&
					AdjustDamage(armor, record.damageType, amount) >= health.current)
				{
					amount = std::max(Engine::Math::Fixed{}, std::min(amount, health.current - Engine::Math::Fixed::One()));
					secondLife = true;
				}
				Engine::Math::Fixed taken = AdjustDamage(armor, record.damageType, amount);
				const Engine::Math::Fixed previous = health.current;
				const bool handled = armors.Handled(record.damageType) || subdual;
				// ActiveBody::attemptDamage's m_damageScalar (a Strategy Center's Hold the Line): on what the armor let
				// through, unless handled, subdual or unresistable (allowModifier), before an immortal body's floor.
				if (!scalars.empty() && !handled && !armors.Unscaled(record.damageType))
					taken = taken * scalars[row].scalar;
				if (!floors.empty() && floors[row].Immortal() && !subdual)
					taken = std::min(taken, std::max(health.current - floors[row].least, Engine::Math::Fixed{}));
				if (subdual)
				{
					Subdual &body = subduals[row];
					body.damage = std::clamp(body.damage + taken, Engine::Math::Fixed{}, body.cap);
					body.touchTick = context.Tick();
					body.gaining = taken > Engine::Math::Fixed{} ? 1 : 0;
					// Object::notifySubdualDamage: a hit wakes the helper, its countdown afresh.
					if (taken > Engine::Math::Fixed{})
					{
						body.awake = 1;
						body.countdown = body.healTicks;
						body.hitTick = context.Tick();
					}
				}
				else if (!handled)
					health.current -= taken;
				if (taken > Engine::Math::Fixed{} || handled)
					hits.push_back({entities[row], record.source, record.damageType, health.armor, taken, record.deathType,
						record.fxType != DamageRecord::NoFxType ? record.fxType : record.damageType, handled, record.statusType});
				// ActiveBody::attemptDamage's last damage (who, what type, when): a fresh one unless the last was this tick or
				// the one before; then only one telling who the attacker is replaces it (a live source over a gone one; over
				// a live one, only a vehicle's, infantry's or structure's).
				{
					const std::uint64_t now = context.Tick();
					const bool fresh = health.lastDamageTick == 0 || (health.lastDamageTick != now && health.lastDamageTick + 1 != now);
					bool replace = fresh;
					if (!fresh && lookup.IsAlive(record.source))
					{
						if (!lookup.IsAlive(health.lastAttacker))
							replace = true;
						else if (const Targetable *by = lookup.Get<Targetable>(record.source);
								 by != nullptr && (by->classes & (target_class::Vehicle | target_class::Infantry | target_class::Structure)) != 0)
							replace = true;
					}
					if (replace)
					{
						health.lastAttacker = record.source;
						// Its player (as it fired, else its owner now) and definition while it is there.
						const Owner *owner = lookup.Get<Owner>(record.source);
						const DefinitionRef *definition = lookup.Get<DefinitionRef>(record.source);
						health.lastAttackerPlayer = record.sourcePlayer != DamageRecord::NoPlayer ? record.sourcePlayer
							: owner != nullptr ? owner->player : Health::None;
						health.lastAttackerDefinition = definition != nullptr ? definition->index : Health::None;
						health.lastDamageType = record.damageType;
						health.lastDamageTick = now;
					}
				}
				// startSecondLife: its new maximum, fully healed (setMaxHealth FULLY_HEAL).
				if (secondLife && !IsDead(health))
				{
					lives[row].started = 1;
					health.maximum = lives[row].maximum;
					health.current = health.maximum;
					secondLives.push_back({entities[row], record.source, record.deathType, record.damageType});
				}
				if (IsDead(health))
				{
					const Engine::Math::Fixed overkill =
						health.maximum > Engine::Math::Fixed{} ? (Engine::Math::Fixed{} - health.current) / health.maximum : Engine::Math::Fixed{};
					health.current = {};
					died.push_back({entities[row], record.source, record.damageType, record.deathType, overkill, previous});
					break;
				}
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::HealthSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.health";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
