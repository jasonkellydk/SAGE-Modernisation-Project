export module engine.gameplay.common.scripts.components.scheduled_message;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

export namespace engine::gameplay {
// An invalid observer broadcasts to every behavior attached to recipient.
// Arguments are game-supplied event payloads; the engine assigns no meaning.
struct BehaviorMessage {
    ecs::Entity recipient, observer;
    std::array<std::int64_t,4> arguments{};
    std::uint64_t due_tick{}, order{};
    std::uint32_t event{}, argument_count{};
};
struct ScheduledBehaviorMessage { BehaviorMessage message; };
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::ScheduledBehaviorMessage> {
    static constexpr std::string_view StableName="engine.gameplay.scheduled_behavior_message";
    static constexpr std::uint32_t Version=1;
    static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
