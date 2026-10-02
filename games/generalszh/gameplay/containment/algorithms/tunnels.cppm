export module games.generalszh.gameplay.containment.algorithms.tunnels;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.containment.components.tunnel;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.spatial.components.off_map;

// Zero Hour's tunnel networks (TunnelContain and the player's TunnelTracker),
// between ticks. The cargo manifest links each player's tunnels into one
// network (and moves a lost tunnel's riders to the first one left, or takes
// them down with the last: onTunnelDestroyed). Here, for each network:
//   those moved to another tunnel are its now (onContainedBy: their time
//   inside starts again);
//   getContainCount / getContainMax: every tunnel is as full as the network;
//   leaving through any tunnel (the tracker's one list): a tunnel unloading
//   lets out everyone in the network;
//   TunnelContain::update -> healObjects (retail, PRESERVE_TUNNEL_HEAL_STACKING):
//   each tunnel standing heals everyone in the network by its max health over
//   that tunnel's TimeForFullHeal a frame, or wholly once they have been
//   inside that long.
export namespace generalszh::gameplay
{
inline void TendTunnels(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	gp::CargoManifest &manifest = game.manifest;
	for (const std::uint32_t network : manifest.Networks())
	{
		std::vector<ecs::Entity> tunnels;
		for (const ecs::Entity tunnel : manifest.Network(network))
			if (world.IsAlive(tunnel) && world.Has<gp::Transport>(tunnel) && world.Has<gp::Tunnel>(tunnel))
				tunnels.push_back(tunnel);
		if (tunnels.empty())
			continue;
		// Moved here from a lost tunnel: this one's now, inside since now.
		for (const ecs::Entity tunnel : tunnels)
			for (const ecs::Entity rider : manifest.Aboard(tunnel))
				if (auto *seat = world.Get<gp::Passenger>(rider); seat != nullptr && seat->transport != tunnel)
				{
					seat->transport = tunnel;
					seat->since = game.tick;
					if (auto *away = world.Get<gp::OffMap>(rider))
						away->holder = tunnel;
				}
		// Out through whichever tunnel unloads first.
		for (const ecs::Entity tunnel : tunnels)
			if (world.Get<gp::Transport>(tunnel)->state == gp::TransportState::Unloading)
			{
				for (const ecs::Entity other : tunnels)
					if (other != tunnel)
					{
						for (const ecs::Entity rider : manifest.Aboard(other))
						{
							if (auto *seat = world.Get<gp::Passenger>(rider))
								seat->transport = tunnel;
							if (auto *away = world.Get<gp::OffMap>(rider))
								away->holder = tunnel;
						}
						manifest.MoveAll(other, tunnel);
					}
				break;
			}
		const auto heads = static_cast<std::uint32_t>(manifest.NetworkCount(network));
		for (const ecs::Entity tunnel : tunnels)
			world.Get<gp::Transport>(tunnel)->occupied = heads;
		// Each standing tunnel heals the whole network.
		for (const ecs::Entity tunnel : tunnels)
		{
			const gp::Health *standing = world.Get<gp::Health>(tunnel);
			if (standing != nullptr && gp::IsDead(*standing))
				continue;
			const std::uint64_t full = world.Get<gp::Tunnel>(tunnel)->fullHealTicks;
			for (const ecs::Entity inside : tunnels)
				for (const ecs::Entity rider : manifest.Aboard(inside))
				{
					gp::Health *body = world.Get<gp::Health>(rider);
					const gp::Passenger *seat = world.Get<gp::Passenger>(rider);
					if (body == nullptr || seat == nullptr || body->current >= body->maximum)
						continue;
					if (game.tick - seat->since >= full)
						engine::gameplay::Heal(*body, body->maximum, game.tick);
					else
						engine::gameplay::Heal(*body, body->maximum / Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(full)), game.tick);
				}
		}
	}
}
}
