export module games.renegade.gameplay.humans.resources.ladders;
import std;
export import games.renegade.content.levels.level_scene;
import engine.ecs.core.resource_store;

export namespace renegade {
struct LadderPortal {
    engine::level::TraversalPortal geometry;
    content::TransitionStyle style{};
    std::int32_t index{-1};
};
struct LadderPortals {std::vector<LadderPortal> portals;};
// Typed game policy is prepared from the shared authored model before the
// simulation starts. Chunk jobs perform no configuration/schema lookups.
std::expected<LadderPortals,std::string> PrepareLadderPortals(const engine::level::Level& level) {
    LadderPortals result;
    for(const auto& portal:level.traversalPortals) {
        const auto style=portal.properties.Get<std::int64_t>("renegade.style");
        const auto index=portal.properties.Get<std::int64_t>("renegade.ladder_index");
        if(!style || !index || *style<0 || *style>9 || *index<-1 || *index>0x7fffffff)
            return std::unexpected("invalid typed Renegade transition metadata");
        if(*style>3) continue; // Vehicle policy consumes its own transition family.
        // All 62 retail transition placements use -1. Runtime path-action
        // assignment/reservation is still an explicit unfinished dependency.
        if(*index!=-1) return std::unexpected("indexed ladder occupancy is not implemented");
        result.portals.push_back({portal,static_cast<content::TransitionStyle>(*style),std::int32_t(*index)});
    }
    return result;
}
}
export namespace ecs {
template<> struct ResourceTraits<renegade::LadderPortals> {static constexpr std::string_view StableName="renegade.ladder_portals";};
}
