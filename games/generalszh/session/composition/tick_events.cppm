export module games.generalszh.session.composition.tick_events;
import std;

export import engine.ecs.core.world;
import engine.gameplay.common.health.components.subdual;
import engine.gameplay.common.identity.resources.name_registry;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.death.components.height_die;
import engine.gameplay.rts.lifecycle.resources.casualties;
import games.generalszh.gameplay.abilities.resources.ability_notices;
import games.generalszh.gameplay.abilities.resources.sticky_bomb_cues;
import games.generalszh.gameplay.battleplans.resources.battle_plan_cues;
import games.generalszh.gameplay.beacons.resources.beacons;
import games.generalszh.gameplay.bridges.resources.bridge_cues;
import games.generalszh.gameplay.combat.algorithms.battle_bus;
import games.generalszh.gameplay.effects.resources.effect_cues;
import games.generalszh.gameplay.eva.resources.eva_notices;
import games.generalszh.gameplay.hacking.resources.hack_cues;
import games.generalszh.gameplay.mines.components.minefield_generator;
import games.generalszh.gameplay.powers.components.launcher_door;
import games.generalszh.gameplay.powers.components.particle_cannon;
import games.generalszh.gameplay.powers.resources.cash_notices;
import games.generalszh.gameplay.production.resources.production_notices;
import games.generalszh.gameplay.production.resources.rally_notices;
import games.generalszh.gameplay.railroad.resources.rail_network;
import games.generalszh.gameplay.world.resources.deselections;

// The simulation's one-tick events (what the presentation and the scripts read of the last tick: casualties, cues,
// notices and changes), cleared as a tick begins, before its commands.
export namespace generalszh::session::composition
{
inline void ClearTickEvents(ecs::World &world)
{
	namespace gp = engine::gameplay;
	namespace domain = generalszh::gameplay;
	world.Resource<gp::Casualties>().list.clear();
	world.Resource<gp::NameRegistry>().ClearReleased();
	world.Resource<domain::CashNotices>().list.clear();
	world.Resource<domain::RallyNotices>().list.clear();
	world.Resource<domain::MinefieldEffects>().played.clear();
	world.Resource<domain::StickyBombCues>().list.clear();
	world.Resource<domain::BridgeCues>().list.clear();
	world.Resource<domain::HackCues>().list.clear();
	world.Resource<domain::RailroadCues>().list.clear();
	world.Resource<domain::BeaconCues>().list.clear();
	world.Resource<domain::BattlePlanCues>().list.clear();
	world.Resource<domain::BattleBusCues>().list.clear();
	world.Resource<domain::EffectCues>().list.clear();
	world.Resource<domain::LauncherDoorEffects>().played.clear();
	auto &cannonEvents = world.Resource<domain::ParticleCannonEvents>();
	cannonEvents.scorches.clear();
	cannonEvents.played.clear();
	cannonEvents.remnants.clear();
	cannonEvents.changes.clear();
	world.Resource<gp::SubdualChanges>().list.clear();
	world.Resource<gp::ParticleClears>().entities.clear();
	world.Resource<domain::EvaNotices>().list.clear();
	world.Resource<domain::Deselections>().list.clear();
	world.Resource<domain::InfiltrationNotices>().list.clear();
	auto &abilities = world.Resource<domain::AbilityNotices>();
	abilities.abilities.clear();
	abilities.powers.clear();
	abilities.defected.clear();
	abilities.hijacks.clear();
	world.Resource<domain::ProductionNotices>().created.clear();
	world.Resource<gp::CargoManifest>().ClearChanges();
}
}
