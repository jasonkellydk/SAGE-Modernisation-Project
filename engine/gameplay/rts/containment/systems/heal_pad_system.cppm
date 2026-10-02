export module engine.gameplay.rts.containment.systems.heal_pad_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.containment.components.heal_pad;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.spatial.components.off_map;

// Heal pads each tick, chunk-parallel over those inside one (HealContain::update -> doHeal): a rider inside at least
// the pad's fullHealTicks since it got in (TheGameLogic->getFrame() - getContainedByFrame() >= framesForFullHeal) is
// made whole and let out (reserveDoorForExit, exitObjectViaDoor); any other heals by its maximum over fullHealTicks
// (as though healing from nothing; attemptHealing: up to its maximum, never the dead).
export namespace engine::gameplay
{
struct HealPadSystem
{
	using Query = ecs::Query<ecs::Write<Health>, ecs::Read<Passenger>, ecs::Read<OffMap>>;
	using Lookup = ecs::Lookup<ecs::Read<HealPad>>;
	using Resources = ecs::Resources<ecs::Write<RiderExits>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<RiderExits>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		auto &out = context.Write<RiderExits>().Slot(context);
		const std::uint64_t tick = context.Tick();
		auto healths = chunk.Get<Health>();
		const auto seats = chunk.Get<Passenger>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < healths.size(); ++row)
		{
			const HealPad *pad = lookup.Get<HealPad>(seats[row].transport);
			if (pad == nullptr)
				continue;
			Health &health = healths[row];
			const std::uint64_t full = std::max<std::uint64_t>(pad->fullHealTicks, 1);
			if (tick - seats[row].since >= full)
			{
				Heal(health, health.maximum, tick);
				out.push_back({seats[row].transport, entities[row]});
				continue;
			}
			if (IsDead(health))
				continue;
			const Engine::Math::Fixed slice = health.maximum / Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(full));
			Heal(health, slice, tick);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::HealPadSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.heal_pad";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it among the tick's other healing and damage.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
