export module engine.gameplay.rts.containment.components.tunnel;
import std;

export import engine.ecs.core.component_registry;

// A way into its player's tunnel network (the original's TunnelContain): its
// network is linked in the cargo manifest; everyone anywhere in the network
// heals over `fullHealTicks` for each way in that stands.
export namespace engine::gameplay
{
struct Tunnel
{
	std::uint64_t fullHealTicks{1};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Tunnel>
{
	static constexpr std::string_view StableName = "engine.gameplay.tunnel";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
