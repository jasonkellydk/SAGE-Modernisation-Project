export module games.generalszh.gameplay.containment.algorithms.put_inside;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.common.spatial.components.targetable;
import engine.gameplay.common.spatial.components.off_map;

// Putting something straight inside a container, as OpenContain::addToContain does, for the composition's own uses
// (object creation lists, initial payloads and rosters).
export namespace generalszh::gameplay
{
// OpenContain::addToContain: `passenger` straight inside `container` (armed as it lets it be; out of the world unless the
// container does not enclose it: addOrRemoveObjFromWorld), taking `slots` of its room (a TransportContain's
// getTransportSlotCount; one elsewhere); `quiet`: without its load sound.
inline void PutInside(GameWorld &game, ecs::Entity container, ecs::Entity passenger, std::uint32_t slots = 1, bool quiet = false)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	gp::Transport *transport = world.Get<gp::Transport>(container);
	if (transport == nullptr || !world.IsAlive(passenger))
		return;
	const gp::Targetable *kind = world.Get<gp::Targetable>(passenger);
	const bool infantry = kind != nullptr && (kind->classes & gp::target_class::Infantry) != 0;
	const bool armed = transport->definition.passengersFire && (!transport->definition.infantryOnly || infantry);
	world.Add<gp::Passenger>(passenger);
	*world.Get<gp::Passenger>(passenger) = {container, slots, 0, game.tick};
	world.Add<gp::OffMap>(passenger);
	*world.Get<gp::OffMap>(passenger) =
		gp::OffMap{transport->definition.enclosesRiders != 0 ? gp::off_map_reason::Contained : gp::off_map_reason::Stationed, armed, {}, container};
	world.Resource<gp::CargoManifest>().Board(container, passenger, quiet);
	transport->occupied += slots;
}
}
