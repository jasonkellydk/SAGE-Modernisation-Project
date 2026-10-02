export module engine.gameplay.rts.stealth.systems.defector_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.stealth.components.undetected_defector;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.components.armament;

// Undetected defectors each tick, in parallel per chunk, before the spatial index (ObjectDefectionHelper::update):
// while its cover holds it is marked Undetected in its target classes (the tick's queries treat it as a friend); once
// its time is up (now >= its end), it has fired, or it is dead, the mark and its cover go.
export namespace engine::gameplay
{
struct DefectorSystem
{
	using Query = ecs::Query<ecs::Read<UndetectedDefector>, ecs::OptionalWrite<Targetable>, ecs::Optional<Health>, ecs::Optional<Armament>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const std::uint64_t tick = context.Tick();
		const auto defectors = chunk.Get<UndetectedDefector>();
		auto targetables = chunk.Get<Targetable>();
		const auto healths = chunk.Get<Health>();
		const auto armaments = chunk.Get<Armament>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < defectors.size(); ++row)
		{
			const bool dead = !healths.empty() && IsDead(healths[row]);
			const bool firing = !armaments.empty() && armaments[row].firedTick != 0 && armaments[row].firedTick + 1 >= tick;
			const bool covered = tick < defectors[row].until && !dead && !firing;
			if (!targetables.empty())
			{
				auto &classes = targetables[row].classes;
				classes = covered ? (classes | target_class::Undetected) : (classes & ~target_class::Undetected);
			}
			if (!covered)
				context.Commands().Remove<UndetectedDefector>(entities[row]);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::DefectorSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.defector";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	// The composition orders it before its spatial index.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
