export module games.generalszh.gameplay.stealth.systems.supply_stealth_grant_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.stealth.components.supply_stealth_grant;
export import engine.gameplay.rts.stealth.resources.detections;
export import engine.gameplay.rts.harvesting.resources.harvest_catalog;
export import engine.gameplay.rts.harvesting.systems.harvest_system;
export import engine.gameplay.rts.stealth.systems.stealth_detector_system;
import games.generalszh.gameplay.stealth.algorithms.supply_stealth;

// SupplyCenterDockUpdate::action with GrantTemporaryStealth: each delivery of this tick's harvest (cash paid) to a
// supply centre with a grant, stealthed itself, offers its harvester the grant (TemporaryStealthApplies); the reveal
// pass gives it (receiveGrant(TRUE, frames)). A batch system: deliveries are rare.
export namespace generalszh::gameplay
{
struct SupplyStealthGrantSystem
{
	using Query = ecs::Query<ecs::Read<SupplyStealthGrant>>;
	using Lookup = ecs::Lookup<ecs::Read<SupplyStealthGrant>, ecs::Read<engine::gameplay::Stealth>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::HarvestEvents>, ecs::Write<engine::gameplay::TemporaryStealthGrants>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		auto &grants = context.Write<gp::TemporaryStealthGrants>();
		grants.list.clear();
		const auto lookup = context.Lookup<Lookup>();
		context.Read<gp::HarvestEvents>().ForEach([&](const gp::HarvestEvent &event) {
			if (event.kind != gp::HarvestEvent::Kind::Delivered || event.amount <= 0)
				return;
			const SupplyStealthGrant *grant = lookup.Get<SupplyStealthGrant>(event.depot);
			if (grant == nullptr || grant->deliveryTicks == 0)
				return;
			if (TemporaryStealthApplies(lookup.Get<gp::Stealth>(event.depot), lookup.Get<gp::Stealth>(event.harvester)))
				grants.list.push_back({event.harvester, grant->deliveryTicks});
		});
		grants.Sort();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::SupplyStealthGrantSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.supply_stealth_grants";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::StealthRevealSystem>;
	using After = SystemTypeList<engine::gameplay::HarvestSystem>;
};
}
