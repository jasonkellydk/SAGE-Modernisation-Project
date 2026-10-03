export module engine.gameplay.common.inventory.systems.inventory_selection_system;
import std;
export import engine.gameplay.common.inventory.components.inventory;
export import engine.ecs.system.system;
import engine.ecs.system.chunk_outputs;

export namespace engine::gameplay {
struct InventoryEntry {ecs::Entity entity;InventoryItem item;};
using InventoryEntries=ecs::ChunkOutputs<InventoryEntry>;
inline std::uint64_t InventoryKey(ecs::Entity entity) noexcept {return (std::uint64_t(entity.generation)<<32)|entity.index;}
struct InventorySnapshot {std::map<std::uint64_t,std::vector<InventoryEntry>> containers;};
}
export namespace ecs {
template<> struct ResourceTraits<engine::gameplay::InventoryEntries> {static constexpr std::string_view StableName="engine.gameplay.inventory_entries";};
template<> struct ResourceTraits<engine::gameplay::InventorySnapshot> {static constexpr std::string_view StableName="engine.gameplay.inventory_snapshot";};
}
export namespace engine::gameplay {
struct InventorySnapshotSystem {
    using Query=ecs::Query<ecs::Read<InventoryItem>>;
    using Resources=ecs::Resources<ecs::Write<InventoryEntries>,ecs::Write<InventorySnapshot>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {context.Write<InventoryEntries>().Reset(query.PreparedChunkCount());}
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto entities=chunk.Entities();const auto items=chunk.Get<InventoryItem>();auto& output=context.Write<InventoryEntries>().Slot(context);
        for(std::size_t i=0;i<items.size();++i) output.push_back({entities[i],items[i]});
    }
    void AfterChunks(Query&,ecs::SystemContext& context) const {
        auto& bags=context.Write<InventorySnapshot>().containers;bags.clear();
        context.Write<InventoryEntries>().ForEach([&](const auto& entry) {bags[InventoryKey(entry.item.container)].push_back(entry);});
        for(auto& [owner,bag]:bags) std::ranges::sort(bag,[](const auto& a,const auto& b) {return a.item.order!=b.item.order ? a.item.order<b.item.order : InventoryKey(a.entity)<InventoryKey(b.entity);});
    }
};
struct InventorySelectionSystem {
    using Query=ecs::Query<ecs::Write<InventorySelection>,ecs::Write<InventoryControl>>;
    using Resources=ecs::Resources<ecs::Read<InventorySnapshot>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto selections=chunk.Get<InventorySelection>();const auto controls=chunk.Get<InventoryControl>();const auto entities=chunk.Entities();const auto& bags=context.Read<InventorySnapshot>().containers;
        for(std::size_t row=0;row<selections.size();++row) {
            const auto control=std::exchange(controls[row],InventoryControl{});auto& selected=selections[row].selected;
            if(control.operation==InventoryOperation::Clear) {selected={};continue;}
            const auto bag=bags.find(InventoryKey(entities[row]));if(bag==bags.end() || bag->second.empty()) {selected={};continue;}
            const auto& items=bag->second;auto current=std::ranges::find(items,selected,&InventoryEntry::entity);
            if(current==items.end() || !current->item.available) {selected={};current=items.end();}
            if(control.operation==InventoryOperation::None) continue;
            const auto start=current==items.end() ? (control.operation==InventoryOperation::Previous ? 0 : items.size()-1) : std::size_t(current-items.begin());
            for(std::size_t step=1;step<=items.size();++step) {
                const auto index=control.operation==InventoryOperation::Previous ? (start+items.size()-step)%items.size() : (start+step)%items.size();const auto& entry=items[index];
                if(!entry.item.available) continue;
                if(control.operation==InventoryOperation::Group && entry.item.group!=control.value) continue;
                if(control.operation==InventoryOperation::Definition && entry.item.definition!=control.value) continue;
                selected=entry.entity;break;
            }
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::InventorySnapshotSystem> {
    static constexpr std::string_view StableName="engine.gameplay.inventory_snapshot";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PreSimulation;
    using Before=SystemTypeList<engine::gameplay::InventorySelectionSystem>;using After=SystemTypeList<>;
};
template<> struct SystemTraits<engine::gameplay::InventorySelectionSystem> {
    static constexpr std::string_view StableName="engine.gameplay.inventory_selection";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PreSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<engine::gameplay::InventorySnapshotSystem>;
};
}
