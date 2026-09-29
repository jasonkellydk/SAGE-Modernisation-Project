export module games.generalszh.gameplay.production.systems.production_refund_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.death.systems.death_system;
export import engine.gameplay.rts.lifecycle.resources.casualties;
export import engine.gameplay.rts.production.components.production_queue;
export import engine.gameplay.rts.economy.resources.player_money;
export import engine.gameplay.rts.upgrades.resources.player_upgrades;
export import engine.gameplay.common.identity.components.owner;
export import games.generalszh.gameplay.objects.resources.object_templates;

// ProductionUpdate::onDie -> cancelAndRefundAllProduction: a producer that
// died this tick (killed, or lost with its transport; not one deleted, which
// never dies) gives back what everything in its queue cost (cancelUnitCreate,
// cancelUpgrade, front to back), its player's upgrades under research there
// are no longer in production, and its queue empties.
export namespace generalszh::gameplay
{
struct ProductionRefundSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::ProductionQueue>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::ProductionQueue>, ecs::Read<engine::gameplay::Owner>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::Casualties>, ecs::Read<ObjectTemplates>, ecs::Write<engine::gameplay::PlayerMoney>,
		ecs::Write<engine::gameplay::PlayerUpgrades>>;

	void Execute(ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const auto &upgrades = context.Read<ObjectTemplates>().Content().upgrades.upgrades;
		gp::PlayerMoney &money = context.Write<gp::PlayerMoney>();
		gp::PlayerUpgrades &researching = context.Write<gp::PlayerUpgrades>();
		const auto lookup = context.Lookup<Lookup>();
		auto &commands = context.Commands();
		for (const gp::Casualty &death : context.Read<gp::Casualties>().list)
		{
			if (death.departure == gp::Departure::Removed)
				continue;
			const gp::ProductionQueue *queue = lookup.Get<gp::ProductionQueue>(death.entity);
			const gp::Owner *owner = lookup.Get<gp::Owner>(death.entity);
			if (queue == nullptr || owner == nullptr || queue->count == 0)
				continue;
			for (std::uint32_t index = 0; index < queue->count; ++index)
			{
				const gp::ProductionEntry &entry = queue->entries[index];
				money.Deposit(owner->player, entry.paid);
				if (entry.kind == gp::ProductionKind::Upgrade && entry.definition < upgrades.size() && upgrades[entry.definition].player)
					researching.CancelProduction(owner->player, entry.definition);
			}
			gp::ProductionQueue emptied;
			emptied.capacity = queue->capacity;
			commands.Set<gp::ProductionQueue>(death.entity, emptied);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::ProductionRefundSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.production_refund";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::DeathSystem>;
};
}
