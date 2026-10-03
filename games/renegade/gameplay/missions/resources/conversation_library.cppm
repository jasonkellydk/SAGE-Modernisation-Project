export module games.renegade.gameplay.missions.resources.conversation_library;
import std;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export namespace renegade {
struct ConversationLine {std::uint32_t speaker{},sound{},text{},face{};};
struct ConversationPlaybackDefinition {std::string name;std::uint32_t timeline{};std::vector<ConversationLine> lines;};
struct ConversationLibrary {std::vector<ConversationPlaybackDefinition> definitions;};
struct ConversationLineEvent {ecs::Entity conversation,speaker;std::uint32_t text{},sound{},finished{},reason{},face{};};
using ConversationLineEvents=ecs::ChunkOutputs<ConversationLineEvent>;
}
export namespace ecs {
template<> struct ResourceTraits<renegade::ConversationLibrary> {static constexpr std::string_view StableName="renegade.conversation_library";};
template<> struct ResourceTraits<renegade::ConversationLineEvents> {static constexpr std::string_view StableName="renegade.conversation_line_events";};
}
