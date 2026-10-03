export module games.renegade.gameplay.cinematics.components.cinematic_state;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.time.value_transition;

export namespace renegade {
struct CinematicActor {ecs::Entity clock;std::int32_t slot{-1};std::uint32_t active{};Engine::Math::Fixed attack_until{};};
struct CinematicDirector {
    engine::time::ValueTransition fade,letterbox;
    std::array<Engine::Math::Fixed,3> fade_color{};
    std::int32_t camera{-1};std::uint32_t commands_seen{},scripts_seen{},customs_seen{};
};
}
export namespace ecs {
template<> struct ComponentTraits<renegade::CinematicActor> {
    static constexpr std::string_view StableName="renegade.cinematic_actor";static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
template<> struct ComponentTraits<renegade::CinematicDirector> {
    static constexpr std::string_view StableName="renegade.cinematic_director";static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
    static void HashState(const renegade::CinematicDirector& state,StateHasher& hash) noexcept {
        for(const auto& transition:{state.fade,state.letterbox}) for(const auto value:{transition.from,transition.to,transition.start,transition.duration}) hash.AppendU64(std::uint64_t(value.Raw()));
        for(const auto value:state.fade_color) hash.AppendU64(std::uint64_t(value.Raw()));
        hash.AppendU64(std::uint32_t(state.camera));hash.AppendU64(state.commands_seen);hash.AppendU64(state.scripts_seen);hash.AppendU64(state.customs_seen);
    }
};
}
