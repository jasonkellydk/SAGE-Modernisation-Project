export module engine.gameplay.common.health.systems.subdual_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.components.subdual;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.status.components.disabled_until;

// Subdual damage after the tick's damage (a batch: the tick's changes and disables go in shared lists):
//   SubdualDamageHelper::update, awake from the tick after a subdual hit: every SubdualDamageHealRate ticks the body
//   sheds SubdualDamageHealAmount (DAMAGE_SUBDUAL_UNRESISTABLE of minus the amount: not while dead or indestructible;
//   never below none), asleep once none is left;
//   ActiveBody::attemptDamage's weighing, on a tick subdual damage came or went: subdued once it reaches the body's
//   maximum health, free again below it (onSubdualChange: DISABLED_SUBDUED set or cleared, but a projectile is
//   jammed instead), each change noted for the game.
export namespace engine::gameplay
{
struct SubdualSystem
{
	using Query = ecs::Query<ecs::Write<Subdual>, ecs::Read<Health>, ecs::Optional<Targetable>>;
	using Resources = ecs::Resources<ecs::Write<SubdualChanges>, ecs::Write<DisableRequests>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const std::uint64_t now = context.Tick();
		auto &changes = context.Write<SubdualChanges>().list;
		auto &requests = context.Write<DisableRequests>().list;
		query.ForEachChunk([&](auto chunk) {
			auto bodies = chunk.template Get<Subdual>();
			const auto healths = chunk.template Get<Health>();
			const auto targets = chunk.template Get<Targetable>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < bodies.size(); ++row)
			{
				Subdual &body = bodies[row];
				const Health &health = healths[row];
				bool weigh = body.touchTick == now;
				if (body.awake != 0 && body.hitTick != now)
				{
					if (body.countdown > 0)
						--body.countdown;
					if (body.countdown == 0)
					{
						body.countdown = body.healTicks;
						if (!IsDead(health) && !health.indestructible)
						{
							body.damage = std::max(Engine::Math::Fixed{}, body.damage - body.healAmount);
							body.gaining = 0;
							weigh = true;
						}
						if (body.damage <= Engine::Math::Fixed{})
							body.awake = 0;
					}
				}
				if (!weigh)
					continue;
				const bool subdued = health.maximum <= body.damage;
				if (subdued == (body.subdued != 0))
					continue;
				body.subdued = subdued ? 1 : 0;
				const bool projectile = !targets.empty() && (targets[row].classes & target_class::Projectile) != 0;
				changes.push_back({entities[row], subdued, projectile});
				if (!projectile)
					requests.push_back({entities[row], disabled_type::Subdued, subdued ? 0u : 1u, subdued ? DisabledForever : 0u});
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::SubdualSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.subdual";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the tick's damage.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
