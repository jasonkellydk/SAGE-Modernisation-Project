module;
#include <cstdint>
#include <array>
#include <memory>
#include <span>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

export module engine.navigation.movement.snapshot.cells;
import engine.navigation.movement.occupancy_policy;
import engine.navigation.movement.terrain_policy;
import engine.navigation.movement.destination.reservations;
import engine.navigation.movement.corridor.clearance;

export namespace navigation {
struct CellState {
    OccupancyCell occupancy{0,false};
    std::uint32_t goal=0,obstacle=0,aircraftGoal=0;
    unsigned char terrain=0,connection=0;
    bool pinched=false,fence=false,aircraftReserved=false;
    // Native auxiliary-layer lookups alias ground outside their footprint.
    // Zero lets standalone graph fixtures use their owning snapshot's layer.
    unsigned char resolvedLayer=0;
};
struct CellSnapshotEdit {
    int x=0,y=0;
    CellState value;
};

// One captured layer. Input must remain stable throughout construction.
// Column-major storage follows the native grid's contiguous cell columns.
class CellSnapshot {
    static constexpr std::size_t PageSize=256;
    struct Page {
        std::array<CellState,PageSize> cells{};
        // Cold identity metadata belongs to the immutable page as well. A
        // regional edit need not rescan terrain pages to rediscover their IDs.
        std::vector<std::uint32_t> ids;
        CellState& operator[](std::size_t index) { return cells[index]; }
        const CellState& operator[](std::size_t index) const { return cells[index]; }
        void refreshIds(std::size_t length) {
            ids.clear();
            for (std::size_t i=0;i<length;++i) {
                const auto& value=cells[i];
                for (const auto id:{value.occupancy.unit,value.goal,value.obstacle,value.aircraftGoal})
                    if (id && id!=std::numeric_limits<std::uint32_t>::max()) ids.push_back(id);
            }
            std::sort(ids.begin(),ids.end());
            ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
        }
    };
    std::vector<std::shared_ptr<const Page>> pages_;
    std::int64_t left_=0,top_=0;
    std::size_t width_=0,height_=0;
    unsigned layer_=0;
public:
    CellSnapshot()=default;
    template<class Read>
    CellSnapshot(unsigned layer,int left,int top,int right,int bottom,Read read)
        : left_(left),top_(top),layer_(layer) {
        if (left>right || top>bottom) throw std::invalid_argument("Invalid snapshot bounds");
        const auto width=std::uint64_t(std::int64_t(right)-left+1);
        const auto height=std::uint64_t(std::int64_t(bottom)-top+1);
        if (width>std::vector<CellState>{}.max_size()/height) throw std::length_error("Cell snapshot too large");
        width_=std::size_t(width);height_=std::size_t(height);
        const auto count=width_*height_;
        pages_.reserve(count/PageSize+(count%PageSize!=0));
        for (std::size_t base=0;base<count;) {
            auto page=std::make_shared<Page>();
            const auto length=std::min(PageSize,count-base);
            for (std::size_t offset=0;offset<length;++offset) {
                const auto index=base+offset;
                (*page)[offset]=read(int(left_+std::int64_t(index/height_)),
                    int(top_+std::int64_t(index%height_)));
            }
            page->refreshIds(length);
            pages_.push_back(std::move(page));
            base+=length;
        }
    }
    // A new immutable view clones each changed page once. Readers holding the
    // previous view keep its exact terrain and occupancy, including old IDs.
    CellSnapshot updatedCells(std::span<const CellSnapshotEdit> edits) const {
        auto result=*this;
        if (edits.empty()) return result;
        std::vector<Page*> changed(pages_.size(),nullptr);
        for (const auto& edit:edits) {
            if (!cell(edit.x,edit.y)) throw std::out_of_range("Cell snapshot edit outside bounds");
            const auto index=std::size_t(std::int64_t(edit.x)-left_)*height_
                +std::size_t(std::int64_t(edit.y)-top_);
            const auto pageIndex=index/PageSize;
            if (!changed[pageIndex]) {
                auto page=std::make_shared<Page>(*pages_[pageIndex]);
                changed[pageIndex]=page.get();
                result.pages_[pageIndex]=std::move(page);
            }
            (*changed[pageIndex])[index%PageSize]=edit.value;
        }
        for (std::size_t i=0;i<changed.size();++i)
            if (changed[i]) changed[i]->refreshIds(std::min(PageSize,width_*height_-i*PageSize));
        return result;
    }
    unsigned layer() const { return layer_; }
    int left() const { return int(left_); }
    int top() const { return int(top_); }
    int right() const { return int(left_+std::int64_t(width_)-1); }
    int bottom() const { return int(top_+std::int64_t(height_)-1); }
    std::size_t width() const { return width_; }
    std::size_t height() const { return height_; }
    std::vector<std::uint32_t> occupantIds() const {
        std::vector<std::uint32_t> ids;
        std::size_t count=0;
        for (const auto& page:pages_) count+=page->ids.size();
        ids.reserve(count);
        for (const auto& page:pages_) ids.insert(ids.end(),page->ids.begin(),page->ids.end());
        std::sort(ids.begin(),ids.end());
        ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
        return ids;
    }
    const CellState* cell(int x,int y) const {
        const auto dx=std::int64_t(x)-left_,dy=std::int64_t(y)-top_;
        if (dx<0 || dy<0 || std::uint64_t(dx)>=width_ || std::uint64_t(dy)>=height_) return nullptr;
        const auto index=std::size_t(dx)*height_+std::size_t(dy);
        return &(*pages_[index/PageSize])[index%PageSize];
    }
    OccupancyCell occupancy(int x,int y) const {
        const auto* value=cell(x,y);
        return value?value->occupancy:OccupancyCell{0,false};
    }
    bool permits(TerrainQuery query,int x,int y) const {
        const auto* value=cell(x,y);
        return value && permitsTerrain(query,{static_cast<TerrainKind>(value->terrain),
            value->obstacle,value->occupancy.valid,value->fence});
    }
    ReservationCell destination(int x,int y) const {
        const auto* value=cell(x,y);
        if (!value) return {};
        return {static_cast<TerrainKind>(value->terrain),value->obstacle,value->goal,value->aircraftGoal,
            value->occupancy.valid,value->occupancy.empty,value->occupancy.fixed,value->aircraftReserved};
    }
    CorridorCell corridor(int x,int y) const {
        const auto* value=cell(x,y);
        if (!value) return {};
        return {static_cast<TerrainKind>(value->terrain),value->occupancy.unit,
            value->occupancy.valid,value->fence,value->occupancy.fixed};
    }
};
}
