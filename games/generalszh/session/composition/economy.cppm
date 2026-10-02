export module games.generalszh.session.composition.economy;
import std;
import games.generalszh.content.combat.combat_catalog;
import engine.gameplay.rts.economy.resources.overcharge_events;
import engine.gameplay.rts.economy.resources.player_energy;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.common.health.systems.health_system;
import engine.gameplay.common.health.systems.subdual_system;
import engine.gameplay.rts.combat.systems.impact_system;
import engine.gameplay.rts.construction.systems.construction_system;
import engine.gameplay.rts.mines.systems.minefield_system;
import engine.gameplay.rts.parachute.systems.parachute_systems;
import games.generalszh.gameplay.abilities.systems.sticky_bomb_system;
import games.generalszh.gameplay.bridges.systems.bridge_damage_system;
import games.generalszh.gameplay.containment.systems.heal_seek_system;
import games.generalszh.gameplay.production.systems.drone_repair_system;
import engine.gameplay.rts.economy.systems.auto_deposit_system;
import engine.gameplay.rts.economy.systems.energy_system;
import engine.gameplay.rts.economy.systems.overcharge_system;
import games.generalszh.gameplay.economy.systems.warehouse_crippling_system;
import engine.gameplay.rts.economy.components.auto_deposit;
import engine.gameplay.rts.economy.components.energy_source;
import engine.gameplay.rts.economy.components.overcharge;
import games.generalszh.gameplay.economy.components.warehouse_crippling;

// The economy domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The economy domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceEconomyResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::PlayerEnergy>();
	world.EmplaceResource<engine::gameplay::EnergySettings>(
			engine::gameplay::EnergySettings{setup.content.gameData.lowEnergyPenaltyModifier, setup.content.gameData.minLowEnergyProductionSpeed, setup.content.gameData.maxLowEnergyProductionSpeed});
	world.EmplaceResource<engine::gameplay::EnergyShares>();
	world.EmplaceResource<engine::gameplay::OverchargeEvents>();
	world.EmplaceResource<generalszh::gameplay::WarehouseCripplingEvents>();
	world.EmplaceResource<engine::gameplay::AutoDeposits>();
	// OverchargeBehavior::update: DAMAGE_PENALTY, DEATH_NORMAL, over LOGICFRAMES_PER_SECOND.
	world.EmplaceResource<engine::gameplay::OverchargeSettings>(engine::gameplay::OverchargeSettings{content::DamageTypeIndex("PENALTY").value_or(0),
		content::DeathTypeIndex("NORMAL").value_or(0), setup.step.TicksPerSecond()});
}

inline void RegisterEconomyComponents(ecs::World &world)
{
	world.RegisterComponent<generalszh::gameplay::WarehouseCrippling>();
	world.RegisterComponent<engine::gameplay::Overcharge>();
	world.RegisterComponent<engine::gameplay::AutoDeposit>();
	world.RegisterComponent<engine::gameplay::EnergySource>();
	world.RegisterComponent<engine::gameplay::Powered>();
}

// The economy domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterEconomySystems(ecs::SystemRegistry &registry)
{
	static generalszh::gameplay::WarehouseCripplingSystem warehouseCripplings;
	registry.Register(warehouseCripplings);
	static engine::gameplay::OverchargeDrainSystem overchargeDrain;
	registry.Register(overchargeDrain);
	static engine::gameplay::OverchargeLimitSystem overchargeLimit;
	registry.Register(overchargeLimit);
	static engine::gameplay::AutoDepositSystem autoDeposits;
	registry.Register(autoDeposits);
	static engine::gameplay::EnergySystem energy;
	registry.Register(energy);
	static engine::gameplay::BrownOutSystem brownOut;
	registry.Register(brownOut);
}

// What the economy domain's systems run after (and the few they must precede), within the tick.
inline void OrderEconomySystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	namespace domain = generalszh::gameplay;
	registry.OrderBefore<domain::StickyBombSystem, domain::WarehouseCripplingSystem>();
	registry.OrderBefore<domain::BridgeDamageSystem, domain::WarehouseCripplingSystem>();
	registry.OrderBefore<domain::DroneRepairSystem, domain::WarehouseCripplingSystem>();
	registry.OrderBefore<domain::HealSeekSystem, domain::WarehouseCripplingSystem>();
	registry.OrderBefore<gameplay::ConstructionSystem, domain::WarehouseCripplingSystem>();
	registry.OrderBefore<gameplay::SubdualSystem, domain::WarehouseCripplingSystem>();
	registry.OrderBefore<gameplay::FreeFallSystem, gameplay::EnergySystem>();
	registry.OrderBefore<gameplay::MineDrainSystem, gameplay::OverchargeDrainSystem>();
	registry.OrderBefore<gameplay::MinefieldSystem, gameplay::OverchargeLimitSystem>();
	// An overcharge drains with the tick's damage and gives out once it is taken.
	registry.OrderBefore<gameplay::ImpactSystem, gameplay::OverchargeDrainSystem>();
	registry.OrderBefore<gameplay::HealthSystem, gameplay::OverchargeLimitSystem>();
}
}
