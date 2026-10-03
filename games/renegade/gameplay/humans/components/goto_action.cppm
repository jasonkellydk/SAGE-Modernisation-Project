export module games.renegade.gameplay.humans.components.goto_action;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
export namespace renegade {
struct HumanGoto {
	ecs::Entity observer;
	std::uint64_t authored_subject{}, order{};
	Engine::Math::Fixed speed{}, arrived_distance{};
	std::uint32_t action{}, priority{}, enabled{}, reserved{};
};
}
export namespace ecs {
template<> struct ComponentTraits<renegade::HumanGoto> {
	static constexpr std::string_view StableName="renegade.human_goto";
	static constexpr std::uint32_t Version=1;
	static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
