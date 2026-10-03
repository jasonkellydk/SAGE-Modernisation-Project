export module games.renegade.content.levels.traversal_portals;
import std;
export import games.renegade.content.levels.definition_catalog;
export import games.renegade.content.levels.dynamic_level;

export namespace renegade::content {
inline std::expected<void,std::string> AppendTraversalPortals(engine::level::Level& level,
    const DynamicLevel& actors,const DefinitionCatalog& catalog) {
    std::vector<engine::level::TraversalPortal> portals;
    for(const auto& actor:actors.actors) if(actor.factory==0x40124 && !actor.pending_delete) {
        const auto* definition=catalog.Find(actor.definition);
        if(!actor.transform || !definition || definition->factory!=0x40125 || definition->transitions.empty())
            return std::unexpected("transition actor requires an authored pose and transition definition");
        for(std::size_t index=0;index<definition->transitions.size();++index) {
            const auto& source=definition->transitions[index];
            engine::level::TraversalPortal portal;
            portal.subject=actor.LevelId();portal.ordinal=std::uint32_t(index);portal.bounds=source.bounds;portal.destination=source.destination;
            portal.properties.Set("renegade.style",std::int64_t(source.style));
            portal.properties.Set("renegade.ladder_index",std::int64_t(actor.ladder_index));
            portal.properties.Set("renegade.animation",source.animation);
            auto placed=engine::level::PlaceTraversalPortal(std::move(portal),*actor.transform);
            if(!placed) return std::unexpected(placed.error());portals.push_back(std::move(*placed));
        }
    }
    level.traversalPortals.insert(level.traversalPortals.end(),std::make_move_iterator(portals.begin()),std::make_move_iterator(portals.end()));
    return {};
}
}
