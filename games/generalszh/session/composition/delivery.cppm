export module games.generalszh.session.composition.delivery;
import std;
export import games.generalszh.session.composition.simulation_setup;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
import engine.gameplay.rts.movement.systems.route_request_system;
import engine.gameplay.rts.slaves.systems.slaved_system;
import engine.gameplay.rts.delivery.systems.delivery_system;
import engine.gameplay.rts.delivery.components.delivery;

// The delivery domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
// The delivery domain's resources, emplaced in the world (the session's composition: what state the domain keeps outside
// its components).
inline void EmplaceDeliveryResources(ecs::World &world, [[maybe_unused]] const SimulationSetup &setup)
{
	world.EmplaceResource<engine::gameplay::DeliveriesDone>();
	world.EmplaceResource<engine::gameplay::VisibleDrops>();
	world.EmplaceResource<engine::gameplay::DeliveryCues>();
}

inline void RegisterDeliveryComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::Delivery>();
}

// The delivery domain's systems, registered with the simulation schedule (stateless: one shared instance
// each; what they run after is the domain's Order function).
inline void RegisterDeliverySystems(ecs::SystemRegistry &registry)
{
	static engine::gameplay::DeliverySystem delivery;
	registry.Register(delivery);
}

// What the delivery domain's systems run after (and the few they must precede), within the tick.
inline void OrderDeliverySystems(ecs::SystemRegistry &registry)
{
	namespace gameplay = engine::gameplay;
	registry.OrderBefore<gameplay::RouteRequestSystem, gameplay::DeliverySystem>();
	registry.OrderBefore<gameplay::SlavedSystem, gameplay::DeliverySystem>();
}
}
