export module games.renegade.gameplay.missions.components.conversation_state;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export namespace renegade {
struct ConversationState {
    ecs::Entity observer,speaker,listener;
    std::uint64_t authored_speaker{};
    std::uint32_t definition{},action{};
    std::uint64_t created_tick{},created_order{};
};
struct ConversationVoice {ecs::Entity conversation;};
}
export namespace ecs {
template<> struct ComponentTraits<renegade::ConversationState> {
    static constexpr std::string_view StableName="renegade.conversation_state";
    static constexpr std::uint32_t Version=2;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
template<> struct ComponentTraits<renegade::ConversationVoice> {
    static constexpr std::string_view StableName="renegade.conversation_voice";
    static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
