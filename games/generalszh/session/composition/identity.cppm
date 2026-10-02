export module games.generalszh.session.composition.identity;
import std;

export import engine.ecs.core.world;
import engine.gameplay.common.identity.components.captured;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.object_id;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.producer;
import engine.gameplay.common.identity.components.team_member;

// The identity domain's simulation components, registered with the world (the session's composition: which data
// the world holds; the domain's systems and their order follow).
export namespace generalszh::session::composition
{
inline void RegisterIdentityComponents(ecs::World &world)
{
	world.RegisterComponent<engine::gameplay::DefinitionRef>();
	world.RegisterComponent<engine::gameplay::TeamMember>();
	world.RegisterComponent<engine::gameplay::Owner>();
	world.RegisterComponent<engine::gameplay::ObjectId>();
	world.RegisterComponent<engine::gameplay::Producer>();
	world.RegisterComponent<engine::gameplay::Captured>();
}
}
