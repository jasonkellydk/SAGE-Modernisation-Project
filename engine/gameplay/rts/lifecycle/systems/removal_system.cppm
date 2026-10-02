export module engine.gameplay.rts.lifecycle.systems.removal_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.lifecycle.algorithms.retirement;
export import engine.gameplay.rts.lifecycle.resources.removals;
export import engine.gameplay.common.lifetime.resources.expirations;
export import engine.gameplay.rts.delivery.components.delivery;
export import engine.gameplay.rts.containment.systems.cargo_transfer_system;

// After the step: removes what this tick finished with. Carriers done with
// their runs retire (with any riders), keeping teams, names and cargo in
// step; entities whose books were settled when they died (finished slow
// deaths) are only destroyed. Deaths themselves are the death system's.
export namespace engine::gameplay
{
struct RemovalSystem
{
	using Query = ecs::Query<ecs::Read<DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<TeamMember>, ecs::Read<DefinitionRef>, ecs::Read<Transform>>;
	using Resources = ecs::Resources<ecs::Read<DeliveriesDone>, ecs::Read<Removals>, ecs::Read<Deletions>, ecs::Write<TeamRoster>, ecs::Write<NameRegistry>,
		ecs::Write<CargoManifest>, ecs::Write<Casualties>>;

	void Execute(ecs::SystemContext &context)
	{
		const DeliveriesDone &deliveries = context.Read<DeliveriesDone>();
		const Removals &removals = context.Read<Removals>();
		TeamRoster &roster = context.Write<TeamRoster>();
		NameRegistry &names = context.Write<NameRegistry>();
		CargoManifest &manifest = context.Write<CargoManifest>();
		Casualties &casualties = context.Write<Casualties>();
		auto &commands = context.Commands();
		const auto lookup = context.Lookup<Lookup>();
		removals.ForEach([&](ecs::Entity entity) {
			if (lookup.IsAlive(entity))
				commands.Destroy(entity);
		});
		std::vector<Retiree> retirees;
		deliveries.ForEach([&](const DeliveryDone &done) { retirees.push_back({done.carrier, {}, Departure::Removed}); });
		context.Read<Deletions>().ForEach([&](ecs::Entity deleted) { retirees.push_back({deleted, {}, Departure::Removed}); });
		if (retirees.empty())
			return;
		Retire(std::move(retirees), {roster, names, manifest},
			[&](ecs::Entity entity) {
				return RetireeState{lookup.IsAlive(entity), lookup.Get<TeamMember>(entity), lookup.Get<DefinitionRef>(entity), lookup.Get<Transform>(entity)};
			},
			[&](const Retiree &retiree) { commands.Destroy(retiree.entity); }, casualties);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::RemovalSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.removal";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	// Both keep the cargo manifest; boarding and unloading settle first.
	using After = SystemTypeList<engine::gameplay::CargoTransferSystem>;
};
}
