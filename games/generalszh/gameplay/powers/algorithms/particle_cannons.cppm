export module games.generalszh.gameplay.powers.algorithms.particle_cannons;
import std;

export import games.generalszh.gameplay.powers.components.particle_cannon;
export import engine.gameplay.rts.navigation.resources.waypoint_graph;
import Engine.Core.Math.FixedRandom;

// ParticleUplinkCannonUpdate's rules shared by its update and the orders that reach it.
export namespace generalszh::gameplay
{
using Engine::Math::Fixed;

// setLogicalStatus: a change of status is noted for the presentation; charging, preparing and ready start the beam
// afresh; firing restarts its launch effects.
inline void SetCannonStatus(ParticleCannon &cannon, CannonStatus status, ParticleCannonEvents &events, ecs::Entity entity)
{
	if (cannon.status == status)
		return;
	events.changes.push_back({entity, ParticleCannonEvents::Change::Kind::Status, status});
	if (status == CannonStatus::Charging || status == CannonStatus::Preparing || status == CannonStatus::AlmostReady || status == CannonStatus::ReadyToFire)
		cannon.beam = BeamStatus::None;
	if (status == CannonStatus::Firing)
		cannon.nextLaunchFxTick = 0;
	cannon.status = status;
}

// LaserRadiusUpdate::initRadius (widening over `ticks` from now) and setDecayFrames (narrowing over `ticks`).
inline void WidenBeam(ParticleCannon &cannon, std::uint64_t now, std::uint64_t ticks) noexcept
{
	if (ticks == 0)
		return;
	cannon.widening = 1;
	cannon.widenStart = now;
	cannon.widenFinish = now + ticks;
	cannon.widthScale = Fixed{};
}

inline void DecayBeam(ParticleCannon &cannon, std::uint64_t now, std::uint64_t ticks) noexcept
{
	if (ticks == 0)
		return;
	cannon.decaying = 1;
	cannon.decayStart = now;
	cannon.decayFinish = now + ticks;
	cannon.widthScale = Fixed::One();
}

// LaserRadiusUpdate::updateRadius: decaying, 1 less the part of the decay gone (not below 0); widening, the part of
// the widening done (not above 1).
inline void UpdateBeamWidth(ParticleCannon &cannon, std::uint64_t now) noexcept
{
	const auto part = [](std::uint64_t from, std::uint64_t to, std::uint64_t at) {
		return to > from ? Fixed::FromInt(static_cast<std::int64_t>(at - from)) / Fixed::FromInt(static_cast<std::int64_t>(to - from)) : Fixed::One();
	};
	if (cannon.decaying != 0)
		cannon.widthScale = std::max(Fixed{}, Fixed::One() - part(cannon.decayStart, cannon.decayFinish, now));
	else if (cannon.widening != 0)
	{
		cannon.widthScale = part(cannon.widenStart, cannon.widenFinish, now);
		if (cannon.widthScale >= Fixed::One())
		{
			cannon.widthScale = Fixed::One();
			cannon.widening = 0;
		}
	}
}

// initiateIntentToDoSpecialPower: a player's order (manual): the attack starts now at the spot, the beam driven by the
// player's clicks from there; a script's along a waypoint path (from the waypoint, on to one of its links, picked at
// random): ready at once, the attack from now; a script's at a spot or object: ready at once, the attack from now,
// the beam sweeping across it by itself. Any: its beam not yet born; it decays TotalFiringTime after the attack
// starts. Returns whether it took the order.
inline void StartCannonAttack(ParticleCannon &cannon, const ParticleCannonConfig &config, std::uint64_t now, const Engine::Math::FixedVector3 &target,
	bool fromPlayer, std::optional<std::uint32_t> waypoint, const engine::gameplay::WaypointGraph *graph, std::uint64_t seed, ParticleCannonEvents &events,
	ecs::Entity entity)
{
	cannon.beam = BeamStatus::None;
	if (fromPlayer)
	{
		cannon.startAttackTick = now;
		cannon.manual = 1;
		cannon.scripted = 0; // non-retail: a manual attack is no longer a scripted one (retail left it set)
		cannon.initialTarget = cannon.destination = cannon.currentTarget = target;
	}
	else if (waypoint && graph != nullptr && *waypoint < graph->Size())
	{
		cannon.startAttackTick = std::max<std::uint64_t>(now, 1);
		cannon.manual = 0;
		cannon.scripted = 1;
		SetCannonStatus(cannon, CannonStatus::ReadyToFire, events, entity);
		cannon.initialTarget = cannon.currentTarget = graph->Position(*waypoint);
		const auto links = graph->Links(*waypoint);
		auto random = Engine::Math::Stream(seed, {now, *waypoint, 0x9C0u});
		if (!links.empty())
		{
			const auto which = static_cast<std::size_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(links.size()) - 1));
			cannon.nextWaypoint = links[which];
			cannon.destination = graph->Position(links[which]);
		}
		else
			cannon.nextWaypoint = *waypoint;
	}
	else
	{
		cannon.startAttackTick = std::max<std::uint64_t>(now, 1);
		cannon.manual = 0;
		cannon.scripted = 0;
		SetCannonStatus(cannon, CannonStatus::ReadyToFire, events, entity);
		cannon.initialTarget = target;
	}
	cannon.startDecayTick = cannon.startAttackTick + config.totalFiringTicks;
}

// setSpecialPowerOverridableDestination: not while disabled, the beam is driven to the spot (the player's click),
// the clicks noted (two close together drive it fast).
inline void DriveCannonBeam(ParticleCannon &cannon, const Engine::Math::FixedVector3 &to, std::uint64_t now) noexcept
{
	cannon.destination = to;
	cannon.manual = 1;
	cannon.secondLastClickTick = cannon.lastClickTick;
	cannon.lastClickTick = now;
}
}
