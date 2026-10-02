export module games.generalszh.gameplay.effects.systems.radius_decal_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.effects.components.radius_decal;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.combat.algorithms.attack_goal;
export import engine.gameplay.rts.delivery.components.delivery;
export import engine.gameplay.rts.death.components.dying;

// The radius decals' ends, each tick, chunk-parallel (RadiusDecalUpdate::update, HeadOffMapState::onEnter,
// NeutronMissileUpdate::detonate / onDie): one laid until its object is no longer attacking goes once it is not (dead:
// its attack state left); a carrier's once its run heads off the map (or is cleaned up); a neutron missile's once it is
// dying.
export namespace generalszh::gameplay
{
struct RadiusDecalSystem
{
	using Query = ecs::Query<ecs::Write<RadiusDecal>, ecs::Optional<engine::gameplay::AttackTarget>, ecs::Optional<engine::gameplay::Delivery>,
		ecs::Optional<engine::gameplay::Dying>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &) const
	{
		namespace gp = engine::gameplay;
		auto decals = chunk.Get<RadiusDecal>();
		const auto attacks = chunk.Get<gp::AttackTarget>();
		const auto deliveries = chunk.Get<gp::Delivery>();
		const bool dying = !chunk.Get<gp::Dying>().empty();
		for (std::size_t row = 0; row < decals.size(); ++row)
		{
			RadiusDecal &decal = decals[row];
			if (decal.look == RadiusDecal::None)
				continue;
			bool ends = false;
			switch (decal.until)
			{
			case RadiusDecalUntil::ObjectGoes: break;
			case RadiusDecalUntil::NoLongerAttacking: ends = dying || attacks.empty() || !gp::Attacking(attacks[row]); break;
			case RadiusDecalUntil::HeadsOffMap:
				ends = !deliveries.empty() && (deliveries[row].phase == gp::DeliveryPhase::HeadOffMap || deliveries[row].phase == gp::DeliveryPhase::CleanUp);
				break;
			case RadiusDecalUntil::Dies: ends = dying; break;
			}
			if (ends)
				decal.look = RadiusDecal::None;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::RadiusDecalSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.radius_decal";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
