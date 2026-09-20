module;
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

export module engine.navigation.search.graph.captured_weighted;
import engine.navigation.movement.snapshot.cells;
import engine.navigation.movement.snapshot.occupants;
import engine.navigation.movement.terrain_policy;
import engine.navigation.movement.occupancy_policy;
import engine.navigation.movement.classification.weighted_cell;
import engine.navigation.movement.destination.reservations;
import engine.navigation.movement.corridor.clearance;
import engine.navigation.movement.corridor.phase_line;
import engine.navigation.search.graph.weighted_steps;
import engine.navigation.search.graph.destination_rank;
import engine.navigation.search.unsigned_divisor;
import engine.navigation.search.hpa_route;
import engine.navigation.search.route_search;

export namespace navigation {
struct CapturedLayer {
    std::shared_ptr<const CellSnapshot> cells;
    // Optional cell-corner heights, in the same column-major order as cells.
    std::shared_ptr<const std::vector<float>> heights;
    std::shared_ptr<const HpaHierarchy> hpa;
};
struct CapturedGraphQuery {
    WeightedCellQuery movement;
    OccupancyQuery occupancy;
    int startX=0,startY=0,goalX=0,goalY=0,pathDiameter=0;
    unsigned startLayer=1,goalLayer=1;
    bool checkOccupants=true,downhillOnly=false,useHpa=true,fallback=false;
    bool cacheClearance=true;
    std::atomic<std::uint64_t>* clearanceEvaluations=nullptr;
    std::atomic<std::uint64_t>* clearanceHits=nullptr;
    std::shared_ptr<const LayeredHpaRoute> layeredHpa;
};
struct CapturedGraphNode { int x,y; unsigned layer; bool escaping; };
struct CapturedReferenceCursor {
    enum class Stage { Begin, Line, Neighbors };
    Stage stage=Stage::Begin;
    unsigned current=0,previous=0,previousCost=0;
    CapturedGraphNode node{};
    std::optional<PhaseLineCursor> line;
    bool first=true;
};
struct CapturedOccupancyCell {
    unsigned layer=0;
    int x=0,y=0;
    OccupancyCell occupancy{};
    std::uint32_t goal=0,aircraftGoal=0;
    bool aircraftReserved=false;
    TerrainKind terrain=TerrainKind::ground;
    std::uint32_t obstacle=0;
    bool valid=true,pinched=false,fence=false;
    unsigned connection=0;
};

// The vector remains in canonical coordinate order for deterministic capture
// and diagnostics. Queries are coordinate-heavy, so publish a direct read-only
// index once per batch and share it across captured searches.
class CapturedOccupancyIndex {
    std::shared_ptr<const std::vector<CapturedOccupancyCell>> cells_;
    std::vector<std::uint32_t> slots_;
    int left_=0,top_=0;
    unsigned width_=0,height_=0,layers_=0;
    bool denseMode_=false;

    static std::size_t hashCoordinate(int x,int y,unsigned layer) noexcept {
        auto value=std::uint64_t(static_cast<std::uint32_t>(x));
        value^=std::uint64_t(static_cast<std::uint32_t>(y))*0x9e3779b9u;
        value^=std::uint64_t(layer)*0x85ebca6bu;
        value^=value>>30; value*=0xbf58476d1ce4e5b9ull;
        value^=value>>27; value*=0x94d049bb133111ebull;
        return static_cast<std::size_t>(value^(value>>31));
    }
public:
    CapturedOccupancyIndex(std::shared_ptr<const std::vector<CapturedOccupancyCell>> cells,
        int left,int top,int right,int bottom,unsigned layerCount)
        : cells_(std::move(cells)),left_(left),top_(top),
          width_(right>=left?unsigned(right-left+1):0),
          height_(bottom>=top?unsigned(bottom-top+1):0),layers_(layerCount) {
        if (!cells_ || !width_ || !height_ || !layers_)
            throw std::invalid_argument("Invalid captured occupancy index");
        const auto plane=std::size_t(width_)*height_;
        if (plane>std::numeric_limits<std::size_t>::max()/layers_ ||
            plane*layers_>std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("Captured occupancy index too large");
        if (cells_->size()>std::numeric_limits<std::size_t>::max()/2)
            throw std::length_error("Captured occupancy index too large");
        const auto total=plane*layers_;
        if (cells_->empty()) return;
        // A dense table serves populated RTS snapshots. A handful of changed
        // cells should not allocate/clear megabytes of empty slots, though;
        // their small hash table fits in cache even on a normal-sized map.
        // Very large maps also retain sparse storage.
        constexpr std::size_t denseLimit=4u*1024u*1024u;
        denseMode_=total<=denseLimit && (total<=4096 || cells_->size()>=64);
        if (denseMode_) {
            slots_.assign(total,0);
            for (std::uint32_t i=0;i<cells_->size();++i) {
                const auto& cell=(*cells_)[i];
                if (cell.layer>=layers_ || cell.x<left_ || cell.x>right ||
                    cell.y<top || cell.y>bottom) continue;
                const auto local=std::size_t(cell.y-top)*width_+
                    std::size_t(cell.x-left_);
                slots_[std::size_t(cell.layer)*plane+local]=i+1;
            }
            return;
        }
        std::size_t capacity=1;
        const auto required=(std::max)(std::size_t{8},cells_->size()*2);
        while (capacity<required) {
            if (capacity>std::numeric_limits<std::size_t>::max()/2)
                throw std::length_error("Captured occupancy index too large");
            capacity*=2;
        }
        slots_.assign(capacity,0);
        for (std::uint32_t i=0;i<cells_->size();++i) {
            const auto& cell=(*cells_)[i];
            if (cell.layer>=layers_ || cell.x<left_ || cell.x>right ||
                cell.y<top || cell.y>bottom) continue;
            auto position=hashCoordinate(cell.x,cell.y,cell.layer)&(capacity-1);
            while (slots_[position]) position=(position+1)&(capacity-1);
            slots_[position]=i+1;
        }
    }
    const CapturedOccupancyCell* find(int x,int y,unsigned layer) const noexcept {
        if (layer>=layers_ || x<left_ || x>=left_+int(width_) ||
            y<top_ || y>=top_+int(height_)) return nullptr;
        if (slots_.empty()) return nullptr;
        const auto local=std::size_t(y-top_)*width_+std::size_t(x-left_);
        if (denseMode_) {
            const auto value=slots_[std::size_t(layer)*std::size_t(width_)*height_+local];
            return value ? &(*cells_)[value-1] : nullptr;
        }
        auto position=hashCoordinate(x,y,layer)&(slots_.size()-1);
        for (;;) {
            const auto value=slots_[position];
            if (!value) return nullptr;
            const auto& cell=(*cells_)[value-1];
            if (cell.x==x && cell.y==y && cell.layer==layer) return &cell;
            position=(position+1)&(slots_.size()-1);
        }
    }
};

// Immutable equivalent of the native line-of-sight policy used while
// simplifying a ground route.  It deliberately does not cache classifications
// or allocate a graph: one instance is cheap to attach to a captured query and
// shares the batch's captured terrain and occupant planes.
class CapturedLinePassability {
    std::shared_ptr<const CellSnapshot> cells_;
    std::shared_ptr<const OccupantSnapshot> units_;
    std::shared_ptr<const CapturedOccupancyIndex> occupancyCells_;
    WeightedCellQuery movement_;
    OccupancyQuery occupancy_;

    bool dozerPassage(const CellState& cell) const {
        if (!movement_.dozer || cell.terrain!=unsigned(TerrainKind::obstacle)) return false;
        const auto* owner=units_?units_->find(cell.obstacle):nullptr;
        return owner && units_->permitsDozerPassage(owner);
    }
    bool fixedFootprintPassable(int x,int y,unsigned layer,
        const OccupancyQuery& occupancy) const {
        // The line simplifier asks only whether a fixed blocker invalidates a
        // 2x2 footprint. Moving traffic is deliberately ignored in this mode.
        std::array<std::uint32_t,4> seen{};
        unsigned seenCount=0;
        for (int xx=x-1;xx<x+1;++xx) for (int yy=y-1;yy<y+1;++yy) {
            OccupancyCell cell;
            if (occupancyCells_) {
                if (const auto* found=occupancyCells_->find(xx,yy,layer))
                    cell=found->occupancy;
                else
                    cell=cells_->occupancy(xx,yy);
            } else {
                cell=cells_->occupancy(xx,yy);
            }
            if (!cell.valid) return false;
            if (!cell.fixed || cell.unit==occupancy.self || cell.unit==occupancy.ignored)
                continue;
            bool duplicate=false;
            for (unsigned i=0;i<seenCount;++i)
                if (seen[i]==cell.unit) { duplicate=true; break; }
            if (duplicate) continue;
            if (seenCount<seen.size()) seen[seenCount++]=cell.unit;
            const auto* occupant=units_->find(cell.unit);
            if (!occupant) continue;
            if (units_->allied(occupant)) {
                if (!units_->canMoveAside(occupant)) return false;
            } else if (!units_->canCrush(occupant)) {
                return false;
            }
        }
        return true;
    }

public:
    CapturedLinePassability(std::shared_ptr<const CellSnapshot> cells,
        std::shared_ptr<const OccupantSnapshot> units,
        std::shared_ptr<const CapturedOccupancyIndex> occupancyCells,
        WeightedCellQuery movement,
        OccupancyQuery occupancy)
        : cells_(std::move(cells)),units_(std::move(units)),
          occupancyCells_(std::move(occupancyCells)),
          movement_(movement),occupancy_(occupancy) {}

    bool passable(LineCell start,LineCell end,unsigned layer,bool allowPinched=false,
        bool checkStaticFootprint=false,std::uint64_t* work=nullptr,
        bool ignoreMoving=false) const {
        return passable(start,end,layer,occupancy_,allowPinched,checkStaticFootprint,
            work,ignoreMoving);
    }

    // Terrain and relationship snapshots are shared by a batch. The mover
    // identity is supplied per call so large commands do not allocate one
    // otherwise-identical policy object per unit.
    bool passable(LineCell start,LineCell end,unsigned layer,
        const OccupancyQuery& occupancy,bool allowPinched=false,
        bool checkStaticFootprint=false,std::uint64_t* work=nullptr,
        bool ignoreMoving=false) const {
        if (!cells_ || cells_->layer()!=layer || !units_) return false;
        bool legal=true;
        visitPhaseLine(start,end,[&](std::int64_t wideX,std::int64_t wideY) {
            if (wideX<std::numeric_limits<int>::min() || wideX>std::numeric_limits<int>::max() ||
                wideY<std::numeric_limits<int>::min() || wideY>std::numeric_limits<int>::max()) {
                legal=false; return false;
            }
            const int x=static_cast<int>(wideX),y=static_cast<int>(wideY);
            const auto* cell=cells_->cell(x,y);
            if (work) ++*work;
            if (movement_.restrictToBounds &&
                (x<movement_.left || x>movement_.right ||
                 y<movement_.top || y>movement_.bottom)) {
                legal=false; return false;
            }
            if (!cell || !cell->occupancy.valid ||
                !permitsTerrain(movement_.terrain,{static_cast<TerrainKind>(cell->terrain),
                    cell->obstacle,true,cell->fence}) ||
                (!allowPinched && cell->pinched) ||
                (occupancy.considerTransient && cell->pinched)) {
                legal=false; return false;
            }
            if (checkStaticFootprint) {
                const int radius=movement_.radius;
                const int above=std::max(1,movement_.cellsAbove);
                for (int xx=x-radius;xx<x+above;++xx)
                    for (int yy=y-radius;yy<y+above;++yy) {
                        const auto* footprint=cells_->cell(xx,yy);
                        if (!footprint || static_cast<TerrainKind>(footprint->terrain)!=TerrainKind::ground) {
                            legal=false; return false;
                        }
                    }
            }
            OccupancyResult traffic{};
            auto query=occupancy;
            query.x=x; query.y=y;
            // Path::optimize calls isLinePassable with blocked=false.  Native
            // movement checks therefore ignore moving occupants while still
            // rejecting fixed allies/enemies, exactly as this captured policy.
            // The optimizer passes false, matching blocked=false in the
            // native line query. Raw route validation passes true and retains
            // moving-traffic policy.
            query.collectMovingTraffic=ignoreMoving ? false : occupancy.collectMovingTraffic;
            if (query.self || query.ignored || query.radius || query.cellsAbove) {
                if (ignoreMoving && !occupancy.considerTransient &&
                    occupancy.radius==1 && occupancy.cellsAbove==1) {
                    if (!fixedFootprintPassable(x,y,layer,occupancy)) {
                        legal=false; return false;
                    }
                } else {
                    if (!checkOccupancy(query,traffic,[&](int xx,int yy) {
                        if (occupancyCells_)
                            if (const auto* found=occupancyCells_->find(xx,yy,cells_->layer()))
                                return found->occupancy;
                        return cells_->occupancy(xx,yy);
                    },*units_) || traffic.allyFixedCount || traffic.enemyFixed) {
                        legal=false; return false;
                    }
                }
            }
            return true;
        });
        return legal;
    }

    bool unpinchedCliff(int x,int y,unsigned layer) const {
        const auto* cell=(cells_ && cells_->layer()==layer)?cells_->cell(x,y):nullptr;
        return cell && static_cast<TerrainKind>(cell->terrain)==TerrainKind::cliff && !cell->pinched;
    }
};

// One query owns its memoized classifications. Immutable planes may be shared
// between queries; occupant answers must belong to this query's mover.
class CapturedWeightedGraph {
    struct Sample { bool legal=false; OccupancyResult traffic; };

    // Classification storage grows by a fixed page, never by rehashing all
    // observed cells or promoting to a map-sized array in one search slice.
    // Permissions and traffic are still private to the captured mover/query.
    class PagedSamples {
        static constexpr std::size_t PageSize=256;
        struct Page {
            std::array<Sample,PageSize> values{};
            std::array<std::uint8_t,PageSize> valid{};
        };
        std::vector<std::unique_ptr<Page>> pages_;
    public:
        void begin(std::size_t expected) {
            pages_.clear();
            pages_.resize((expected+PageSize-1)/PageSize);
        }
        template<class Compute>
        const Sample& get(std::size_t index,Compute compute) {
            auto& page=pages_[index/PageSize];
            if (!page) page=std::make_unique<Page>();
            const auto offset=index%PageSize;
            if (!page->valid[offset]) {
                page->values[offset]=compute();
                page->valid[offset]=1;
            }
            return page->values[offset];
        }
    };
    // Destination eligibility is immutable for the lifetime of a captured
    // graph. Fallback searches ask the same policy twice: once while finding
    // the geometric lower bound and again when ranking settled nodes. Cache
    // those answers without making the graph or its searches share mutable
    // state. A query change clears the cache so this remains exact for the
    // public destinationAllowed API as well.
    class SparseDestinations {
        struct Entry { std::size_t key=0; bool allowed=false; };
        std::vector<Entry> table_;
        std::vector<std::uint8_t> dense_;
        std::size_t size_=0;
        bool denseMode_=false;
        DestinationQuery query_{};
        bool hasQuery_=false;
        static std::size_t hash(std::size_t value) noexcept {
            std::uint64_t x=value;
            x^=x>>30; x*=0xbf58476d1ce4e5b9ull;
            x^=x>>27; x*=0x94d049bb133111ebull;
            return std::size_t(x^(x>>31));
        }
        static bool equal(const DestinationQuery& a,const DestinationQuery& b) noexcept {
            return a.self==b.self && a.ignored==b.ignored && a.radius==b.radius &&
                a.center==b.center && a.hasMover==b.hasMover && a.aircraft==b.aircraft &&
                a.rejectImpassable==b.rejectImpassable;
        }
        void clear() {
            for (auto& entry:table_) entry.key=0;
            std::fill(dense_.begin(),dense_.end(),std::uint8_t{0});
            size_=0;
        }
        void rehash(std::size_t capacity) {
            capacity=std::max<std::size_t>(64,std::bit_ceil(capacity));
            std::vector<Entry> next(capacity);
            for (const auto& entry:table_) if (entry.key) {
                auto slot=hash(entry.key)&(capacity-1);
                while (next[slot].key) slot=(slot+1)&(capacity-1);
                next[slot]=entry;
            }
            table_.swap(next);
        }
    public:
        void begin(std::size_t expected,bool denseMode=false) {
            denseMode_=denseMode;
            if (denseMode_) {
                dense_.assign(expected,0);
                table_.clear();
            }
            clear();
            hasQuery_=false;
            if (!denseMode_ && table_.empty()) {
                const auto target=(std::clamp)(expected/64u,std::size_t(64),std::size_t(8192));
                rehash(target);
            }
        }
        template<class Compute>
        bool get(std::size_t index,const DestinationQuery& query,Compute compute) {
            if (!hasQuery_ || !equal(query_,query)) {
                clear();
                query_=query;
                hasQuery_=true;
            }
            if (denseMode_ && index<dense_.size()) {
                auto& state=dense_[index];
                if (!state) state=compute()?std::uint8_t{2}:std::uint8_t{1};
                return state==2;
            }
            if (table_.empty()) rehash(64);
            if ((size_+1)*10>=table_.size()*7) rehash(table_.size()*2);
            const auto key=index+1;
            auto slot=hash(key)&(table_.size()-1);
            while (table_[slot].key && table_[slot].key!=key)
                slot=(slot+1)&(table_.size()-1);
            if (!table_[slot].key) {
                table_[slot].allowed=compute();
                table_[slot].key=key;
                ++size_;
            }
            return table_[slot].allowed;
        }
    };
    class SparseClearance {
        struct Entry { std::size_t key=0; int value=0; };
        std::vector<Entry> table_;
        std::size_t size_=0;
        static std::size_t hash(std::size_t value) noexcept {
            std::uint64_t x=value;
            x^=x>>30; x*=0xbf58476d1ce4e5b9ull;
            x^=x>>27; x*=0x94d049bb133111ebull;
            return std::size_t(x^(x>>31));
        }
        void rehash(std::size_t capacity) {
            capacity=std::max<std::size_t>(64,std::bit_ceil(capacity));
            std::vector<Entry> next(capacity);
            for (const auto& entry:table_) if (entry.key) {
                auto slot=hash(entry.key)&(capacity-1);
                while (next[slot].key) slot=(slot+1)&(capacity-1);
                next[slot]=entry;
            }
            table_.swap(next);
        }
    public:
        void begin(std::size_t expected) {
            for (auto& entry:table_) entry.key=0;
            size_=0;
            if (table_.empty()) {
                const auto target=(std::clamp)(expected/64u,std::size_t(64),std::size_t(8192));
                rehash(target);
            }
        }
        bool get(std::size_t key,int& value) const {
            if (table_.empty()) return false;
            auto slot=hash(key)&(table_.size()-1);
            while (table_[slot].key && table_[slot].key!=key)
                slot=(slot+1)&(table_.size()-1);
            if (!table_[slot].key) return false;
            value=table_[slot].value;
            return true;
        }
        void put(std::size_t key,int value) {
            if ((size_+1)*10>=table_.size()*7) rehash(table_.size()*2);
            auto slot=hash(key)&(table_.size()-1);
            while (table_[slot].key && table_[slot].key!=key)
                slot=(slot+1)&(table_.size()-1);
            if (!table_[slot].key) { table_[slot]={key,value}; ++size_; }
        }
    };
    std::vector<CapturedLayer> layers_;
    std::shared_ptr<const OccupantSnapshot> units_;
    std::shared_ptr<const CapturedOccupancyIndex> dynamicOccupancy_;
    CapturedGraphQuery query_;
    std::array<int,256> indices_;
    mutable PagedSamples samples_;
    // A phase ray needs only pass/fail, not the full traffic record. Allocate
    // compact pages on first use so a short query does not clear a map-sized
    // array. Pages belong to this immutable query and survive its slices.
    static constexpr std::size_t PhasePageSize=256;
    using PhasePage=std::array<std::uint8_t,PhasePageSize>;
    mutable std::vector<std::unique_ptr<PhasePage>> phasePages_;
    mutable SparseDestinations destinations_;
    mutable SparseClearance clearances_;
    UnsignedDivisor row_{1},plane_{1};
    int left_=0,top_=0,right_=0,bottom_=0;
    unsigned width_=0,height_=0,planeSize_=0,terminal_=0,shift_=0;
    bool pinchedStart_=false;
    const HpaRoute* hpa_=nullptr;
    std::shared_ptr<const HpaRoute> ownedHpa_;
    std::shared_ptr<const LayeredHpaRoute> layeredHpa_;
    const CapturedOccupancyCell* dynamic(int x,int y,unsigned layer) const {
        if (!dynamicOccupancy_) return nullptr;
        return dynamicOccupancy_->find(x,y,layer);
    }
    ReservationCell destinationCell(const CellSnapshot& plane,int x,int y) const {
        auto result=plane.destination(x,y);
        if (const auto* value=dynamic(x,y,plane.layer())) {
            result.terrain=value->terrain;
            result.obstacleId=value->obstacle;
            result.goalId=value->goal;
            result.aircraftId=value->aircraftGoal;
            result.reservedForAircraft=value->aircraftReserved;
            // A present ReservationCell means the terrain cell exists, not
            // that a unit occupies it. Dynamic occupancy only replaces the
            // reservation payload; clearing a unit must not make an empty
            // map cell appear invalid to destination policy.
            result.present=true;
            result.noUnits=value->occupancy.empty;
            result.stationary=value->occupancy.fixed;
        }
        return result;
    }
    bool contains(int x,int y,unsigned layer) const {
        return layer<indices_.size() && indices_[layer]>=0 && x>=left_ && x<=right_ && y>=top_ && y<=bottom_;
    }
    const CellSnapshot& cells(unsigned layer) const { return *layers_[indices_[layer]].cells; }
    unsigned baseIndex(int x,int y,unsigned layer) const {
        return unsigned(indices_[layer])*planeSize_+unsigned(std::int64_t(y)-top_)*width_+unsigned(std::int64_t(x)-left_);
    }
    bool dozerPassage(const CellState& cell) const {
        if (!query_.movement.dozer || cell.terrain!=unsigned(TerrainKind::obstacle)) return false;
        const auto* owner=units_->find(cell.obstacle);
        return owner && units_->permitsDozerPassage(owner);
    }
    bool dozerPassage(const CapturedOccupancyCell& cell) const {
        if (!query_.movement.dozer || cell.terrain!=TerrainKind::obstacle) return false;
        const auto* owner=units_->find(cell.obstacle);
        return owner && units_->permitsDozerPassage(owner);
    }
    Sample computeSample(int x,int y,unsigned layer) const {
        if (!contains(x,y,layer)) return {};
        Sample value;
        const auto& plane=cells(layer);
        value.legal=classifyWeightedCell(query_.movement,x,y,value.traffic,[&](int xx,int yy) {
            const auto* cell=plane.cell(xx,yy);
            if (!cell) return WeightedTerrainCell{};
            if (const auto* dynamicCell=dynamic(xx,yy,layer))
                return WeightedTerrainCell{{dynamicCell->terrain,dynamicCell->obstacle,
                    dynamicCell->valid,dynamicCell->fence},dynamicCell->pinched,
                    dozerPassage(*dynamicCell)};
            return WeightedTerrainCell{{static_cast<TerrainKind>(cell->terrain),cell->obstacle,
                cell->occupancy.valid,cell->fence},cell->pinched,dozerPassage(*cell)};
        },[&](OccupancyResult& traffic) {
            if (!query_.checkOccupants) return true;
            auto occupancy=query_.occupancy;occupancy.x=x;occupancy.y=y;
            return checkOccupancy(occupancy,traffic,[&](int xx,int yy) {
                if (dynamicOccupancy_)
                    if (const auto* found=dynamicOccupancy_->find(xx,yy,layer))
                        return found->occupancy;
                return plane.occupancy(xx,yy);
            },*units_);
        });
        return value;
    }
    Sample sample(int x,int y,unsigned layer) const {
        if (!query_.checkOccupants) return computeSample(x,y,layer);
        if (!contains(x,y,layer)) return {};
        return samples_.get(baseIndex(x,y,layer),[&] { return computeSample(x,y,layer); });
    }
    bool phasePassable(int x,int y,unsigned layer) const {
        if (!contains(x,y,layer)) return false;
        const auto index=baseIndex(x,y,layer);
        auto& page=phasePages_[index/PhasePageSize];
        if (!page) page=std::make_unique<PhasePage>();
        auto& permission=(*page)[index%PhasePageSize];
        if (!permission) {
            const auto* cell=cells(layer).cell(x,y);
            const auto* change=dynamic(x,y,layer);
            const auto value=sample(x,y,layer);
            const bool passable=cell && !(change?change->pinched:cell->pinched) &&
                (change?unsigned(change->terrain):cell->terrain)!=unsigned(TerrainKind::cliff) &&
                value.legal && !value.traffic.enemyFixed && !value.traffic.allyFixedCount;
            permission=passable?2:1;
        }
        return permission==2;
    }
    float height(int x,int y,unsigned layer) const {
        const auto index=std::size_t(std::int64_t(x)-left_)*height_+std::size_t(std::int64_t(y)-top_);
        return (*layers_[indices_[layer]].heights)[index];
    }
public:
    CapturedWeightedGraph(std::vector<CapturedLayer> layers,
        std::shared_ptr<const OccupantSnapshot> units,CapturedGraphQuery query,
        std::shared_ptr<const CapturedOccupancyIndex> dynamicOccupancy={},
        std::shared_ptr<const HpaRoute> hpaBounds={},
        std::shared_ptr<const LayeredHpaRoute> layeredHpa={})
        : layers_(std::move(layers)),units_(std::move(units)),
          dynamicOccupancy_(std::move(dynamicOccupancy)),query_(query),
          layeredHpa_(std::move(layeredHpa)) {
        indices_.fill(-1);
        if (layers_.empty() || layers_.size()>indices_.size() || !units_)
            throw std::invalid_argument("Missing captured graph data");
        const auto* first=layers_.front().cells.get();
        if (!first || !first->width() || !first->height()) throw std::invalid_argument("Empty captured graph");
        const auto count=std::uint64_t(first->width())*first->height();
        if (count>(std::numeric_limits<unsigned>::max()-1u)/(2u*layers_.size()))
            throw std::length_error("Captured graph index overflow");
        left_=first->left();top_=first->top();right_=first->right();bottom_=first->bottom();
        width_=unsigned(first->width());height_=unsigned(first->height());planeSize_=unsigned(count);
        row_=UnsignedDivisor(width_);plane_=UnsignedDivisor(planeSize_);
        if (query_.movement.radius<0 || query_.movement.cellsAbove<1 || query_.pathDiameter<0 ||
            query_.occupancy.radius!=query_.movement.radius || query_.occupancy.cellsAbove<0 ||
            std::max(1,query_.occupancy.cellsAbove)!=query_.movement.cellsAbove)
            throw std::invalid_argument("Invalid captured footprint");
        for (unsigned i=0;i<layers_.size();++i) {
            const auto& layer=layers_[i];const auto* cell=layer.cells.get();
            if (!cell || cell->layer()>=indices_.size() || indices_[cell->layer()]>=0 ||
                cell->left()!=left_ || cell->top()!=top_ || cell->right()!=right_ || cell->bottom()!=bottom_)
                throw std::invalid_argument("Inconsistent captured layers");
            indices_[cell->layer()]=int(i);
            if (query_.downhillOnly && !layer.heights) throw std::invalid_argument("Missing captured heights");
            if (layer.heights) {
                if (layer.heights->size()!=planeSize_) throw std::invalid_argument("Invalid captured height plane");
                for (float value:*layer.heights) if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite captured height");
            }
        }
        if (!contains(query_.startX,query_.startY,query_.startLayer) ||
            !contains(query_.goalX,query_.goalY,query_.goalLayer)) throw std::invalid_argument("Captured endpoints outside grid");
        if (query_.useHpa && query_.startLayer==layers_.front().cells->layer() &&
            query_.goalLayer==layers_.front().cells->layer()) {
            if (hpaBounds) {
                ownedHpa_=std::move(hpaBounds);
                hpa_=ownedHpa_.get();
            } else if (layers_.front().hpa) {
                ownedHpa_=std::make_shared<HpaRoute>(layers_.front().hpa->lowerBounds(
                    {query_.startX-left_,query_.startY-top_},
                    {query_.goalX-left_,query_.goalY-top_}));
                hpa_=ownedHpa_.get();
            } else {
                ownedHpa_=std::make_shared<HpaRoute>(buildHpaRoute(width_,height_,
                    {query_.startX-left_,query_.startY-top_},{query_.goalX-left_,query_.goalY-top_},
                    [&](int x,int y) {
                        const auto* cell=layers_.front().cells->cell(x+left_,y+top_);
                        return cell && permitsTerrain(query_.movement.terrain,
                            {static_cast<TerrainKind>(cell->terrain),cell->obstacle,true,cell->fence});
                    }));
                hpa_=ownedHpa_.get();
            }
            // Connectivity is shared with HPA. Fine-grid edges retain the
            // authoritative footprint and occupancy checks.
        }
        const auto* start=cells(query_.startLayer).cell(query_.startX,query_.startY);
        const auto* dynamicStart=dynamic(query_.startX,query_.startY,query_.startLayer);
        if (!start || !(dynamicStart?dynamicStart->valid:start->occupancy.valid))
            throw std::invalid_argument("Missing captured start cell");
        const auto expected=std::size_t(planeSize_)*layers_.size();
        phasePages_.resize((expected+PhasePageSize-1)/PhasePageSize);
        if (query_.checkOccupants) samples_.begin(expected);
        destinations_.begin(expected,query_.fallback);
        if (query_.cacheClearance) clearances_.begin(expected);
        pinchedStart_=(dynamicStart?dynamicStart->terrain:static_cast<TerrainKind>(start->terrain))==TerrainKind::ground &&
            (dynamicStart?dynamicStart->pinched:start->pinched);
        shift_=sample(query_.startX,query_.startY,query_.startLayer).legal?0:1;
        terminal_=unsigned(planeSize_*layers_.size())<<shift_;
    }
    CapturedWeightedGraph(const CapturedWeightedGraph&)=delete;
    CapturedWeightedGraph& operator=(const CapturedWeightedGraph&)=delete;
    CapturedWeightedGraph(CapturedWeightedGraph&&)=default;
    // Share immutable inputs while giving another search its own classifications.
    CapturedWeightedGraph cloneQuery() const {
        return CapturedWeightedGraph(layers_,units_,query_,dynamicOccupancy_,ownedHpa_,layeredHpa_);
    }
    CapturedWeightedGraph& operator=(CapturedWeightedGraph&&)=default;
    unsigned nodeCount() const { return terminal_+1; }
    unsigned terminal() const { return terminal_; }
    unsigned encode(int x,int y,unsigned layer,bool escaping=false) const {
        if (!contains(x,y,layer) || (escaping && !shift_)) throw std::out_of_range("Captured graph coordinate");
        if (layer!=layers_.front().cells->layer()) {
            const auto* cell=cells(layer).cell(x,y);
            if (cell && cell->resolvedLayer) layer=cell->resolvedLayer;
        }
        return (baseIndex(x,y,layer)<<shift_)|unsigned(escaping);
    }
    CapturedGraphNode decode(unsigned id) const {
        if (id>=terminal_) throw std::out_of_range("Captured graph node");
        const unsigned base=id>>shift_,layer=plane_.quotient(base),local=base-layer*planeSize_;
        const unsigned y=row_.quotient(local),x=local-y*width_;
        return {int(std::int64_t(left_)+x),int(std::int64_t(top_)+y),layers_[layer].cells->layer(),shift_ && (id&1u)};
    }
    unsigned start() const { return encode(query_.startX,query_.startY,query_.startLayer,shift_!=0); }
    unsigned goal() const { return encode(query_.goalX,query_.goalY,query_.goalLayer); }
    bool passable(int x,int y,unsigned layer) const { return sample(x,y,layer).legal; }
    bool terrainPermits(int x,int y,unsigned layer) const {
        if (!contains(x,y,layer)) return false;
        if (const auto* dynamicCell=dynamic(x,y,layer))
            return permitsTerrain(query_.movement.terrain,{dynamicCell->terrain,dynamicCell->obstacle,
                dynamicCell->valid,dynamicCell->fence});
        return layers_[indices_[layer]].cells->permits(query_.movement.terrain,x,y);
    }
    bool cliff(int x,int y,unsigned layer) const {
        if (!contains(x,y,layer)) return false;
        if (const auto* cell=dynamic(x,y,layer)) return cell->terrain==TerrainKind::cliff;
        const auto* cell=cells(layer).cell(x,y);
        return cell && cell->terrain==unsigned(TerrainKind::cliff);
    }
    bool dozerPassageAt(int x,int y,unsigned layer) const {
        if (!contains(x,y,layer)) return false;
        const auto* cell=layers_[indices_[layer]].cells->cell(x,y);
        return cell && dozerPassage(*cell);
    }
    bool pinched(int x,int y,unsigned layer) const {
        if (const auto* dynamicCell=dynamic(x,y,layer)) return dynamicCell->pinched;
        const auto* cell=contains(x,y,layer) ? layers_[indices_[layer]].cells->cell(x,y) : nullptr;
        return cell && cell->pinched;
    }
    unsigned connection(int x,int y,unsigned layer) const {
        const auto* cell=contains(x,y,layer) ? layers_[indices_[layer]].cells->cell(x,y) : nullptr;
        return cell ? cell->connection : 0;
    }
    bool inStaticStartComponent(int x,int y,unsigned layer) const noexcept {
        if (!hpa_ || !hpa_->hasStartComponent() || layers_.size()!=1 || layers_.empty() || !layers_.front().cells ||
            layers_.front().cells->layer()!=layer)
            return true;
        return hpa_->inStartComponent(x-left_,y-top_);
    }
    // Build a query-local abstract graph only when an explicit caller asks for
    // a dynamic negative proof. Captured searches do not invoke this helper:
    // their exact captured search remains authoritative and avoids repeating a
    // map-wide dynamic classification for every request in a batch.
    bool definitelyDisconnectedByCapturedOccupancy() const {
        constexpr unsigned groundLayer=1u;
        if (!query_.checkOccupants || layers_.size()!=1 || layers_.empty() ||
            !layers_.front().cells || layers_.front().cells->layer()!=groundLayer ||
            query_.startLayer!=groundLayer || query_.goalLayer!=groundLayer)
            return false;
        if (!sample(query_.startX,query_.startY,query_.startLayer).legal ||
            !sample(query_.goalX,query_.goalY,query_.goalLayer).legal)
            return false;
        const auto hierarchy=buildHpaHierarchy(width_,height_,[&](int x,int y) {
            return classify(x+left_,y+top_,groundLayer,nullptr);
        },8);
        const auto route=hierarchy->lowerBounds(
            {query_.startX-left_,query_.startY-top_},
            {query_.goalX-left_,query_.goalY-top_});
        if (!route.valid()) return true;
        // The cluster graph is deliberately permissive, so it can report a
        // macro route through two disconnected pockets in one cluster. The
        // captured fine-component proof is still linear and deterministic,
        // and avoids handing a known-negative request to exact A*.
        if (!query_.movement.dozer)
            return !hierarchy->fineConnected(
                {query_.startX-left_,query_.startY-top_},
                {query_.goalX-left_,query_.goalY-top_});
        return false;
    }
    float heightAt(int x,int y,unsigned layer) const { return height(x,y,layer); }
    bool classify(int x,int y,unsigned layer,OccupancyResult* traffic) const {
        const auto value=sample(x,y,layer);
        if (value.legal && traffic && !query_.movement.corridor) *traffic=value.traffic;
        return value.legal;
    }
    bool goalAllowed() const {
        return sample(query_.goalX,query_.goalY,query_.goalLayer).legal && (!query_.movement.corridor ||
            clearance(query_.goalX,query_.goalY,query_.goalLayer)>=query_.pathDiameter);
    }
    int clearance(int x,int y,unsigned layer) const {
        if (!contains(x,y,layer)) return 0;
        const auto key=std::size_t(baseIndex(x,y,layer))+1;
        if (query_.cacheClearance) {
            int cached=0;
            if (clearances_.get(key,cached)) {
                if (query_.clearanceHits) ++*query_.clearanceHits;
                return cached;
            }
        }
        const auto& plane=cells(layer);
        const auto value=corridorClearance(x,y,query_.pathDiameter,query_.movement.terrain.crusher,
            [&](int xx,int yy) {
                if (const auto* dynamicCell=dynamic(xx,yy,layer))
                    return CorridorCell{dynamicCell->terrain,dynamicCell->occupancy.unit,
                        dynamicCell->occupancy.valid,dynamicCell->fence,
                        dynamicCell->occupancy.fixed};
                return plane.corridor(xx,yy);
            },*units_);
        if (query_.clearanceEvaluations) ++*query_.clearanceEvaluations;
        if (query_.cacheClearance) clearances_.put(key,value);
        return value;
    }
    bool destinationAllowed(int x,int y,unsigned layer,DestinationQuery query) const {
        if (!contains(x,y,layer)) return false;
        return destinations_.get(baseIndex(x,y,layer),query,[&] {
            const auto& plane=cells(layer);
            return permitsDestination(query,x,y,[&](int xx,int yy) {
                return destinationCell(plane,xx,yy);
            },*units_);
        });
    }
    template<class Accept>
    bool anyDestination(DestinationQuery query,Accept accept) const {
        for (const auto& layer:layers_) {
            if (!layer.cells) continue;
            const auto layerId=layer.cells->layer();
            for (int y=top_;y<=bottom_;++y) for (int x=left_;x<=right_;++x)
                // Reservation/occupancy rejection is a compact query. Do it
                // before movement classification so a crowded endpoint does
                // not pay the full footprint and terrain policy evaluation.
                // Both predicates read the same immutable capture, so this
                // preserves the result while avoiding redundant work.
                if (destinationAllowed(x,y,layerId,query) && accept(x,y,layerId)) return true;
        }
        return false;
    }
    unsigned geometricHeuristic(unsigned id) const {
        if (id==terminal_) return 0;
        const auto node=decode(id);
        const auto dx=std::uint64_t(std::abs(std::int64_t(node.x)-query_.goalX));
        const auto dy=std::uint64_t(std::abs(std::int64_t(node.y)-query_.goalY));
        const auto octile=10*std::max(dx,dy)+4*std::min(dx,dy);
        return unsigned(std::min<std::uint64_t>(octile,std::numeric_limits<unsigned>::max()-1u));
    }
    unsigned heuristic(unsigned id) const {
        if (id==terminal_) return 0;
        const auto node=decode(id);
        const auto octile=geometricHeuristic(id);
        // The cached hierarchy describes the ground plane. Applying its
        // distance to an auxiliary layer can overestimate a route that uses
        // that layer, so only use it for ground nodes.
        const bool sameGroundLayer=!layers_.empty() && layers_.front().cells &&
            node.layer==layers_.front().cells->layer();
        const auto abstractDistance=hpa_ && hpa_->valid() && sameGroundLayer
            ? hpa_->lowerBound(node.x-left_,node.y-top_)
            : std::numeric_limits<std::uint32_t>::max();
        const auto layeredDistance=layeredHpa_ ? layeredHpa_->lowerBound(
            node.x-left_,node.y-top_,node.layer) : std::numeric_limits<std::uint32_t>::max();
        // An absent abstract route is never treated as an infinite heuristic.
        const auto macro=abstractDistance!=std::numeric_limits<std::uint32_t>::max()
            // HPA stores fixed navigation costs (1000 cardinal/1414
            // diagonal); the weighted graph's octile scale is 10/14.
            ? std::uint64_t(abstractDistance)/100u : 0u;
        const auto layeredMacro=layeredDistance!=std::numeric_limits<std::uint32_t>::max()
            ? std::uint64_t(layeredDistance)/100u : 0u;
        return unsigned(std::min<std::uint64_t>(
            std::max<std::uint64_t>(std::uint64_t(octile),std::max(macro,layeredMacro)),
            std::numeric_limits<unsigned>::max()-1u));
    }
    unsigned referenceHeuristic(unsigned id) const {
        if (id==terminal_) return 0;
        const auto node=decode(id);
        const unsigned dx=std::abs(node.x-query_.goalX),dy=std::abs(node.y-query_.goalY);
        return 10u*std::max(dx,dy)+5u*std::min(dx,dy);
    }
    // Phase scans do much cheaper work than a full frontier expansion. Charge
    // a bounded chunk of 32 samples per credit rather than throttling a long
    // straight scan to the same throughput as 32 footprint/occupancy queries.
    // Chunk boundaries depend only on the ray, never elapsed time or frame rate.
    // Connection checks and eight-neighbor enumeration each cost one credit.
    static constexpr unsigned PhaseSamplesPerCredit=32;
    template<class Workspace,class Emit>
    RouteNeighborProgress advanceReferenceNeighbors(unsigned id,Workspace& search,
        CapturedReferenceCursor& cursor,unsigned budget,Emit emit) const {
        unsigned used=0;
        const auto estimate=[&](unsigned next) { return referenceHeuristic(next); };
        while (used<budget) {
            if (cursor.stage==CapturedReferenceCursor::Stage::Begin) {
                ++used;
                if (id==terminal_) return {used,true};
                cursor.current=id;cursor.previous=id;cursor.node=decode(id);cursor.first=true;
                const auto& node=cursor.node;
                const auto* parent=cells(node.layer).cell(node.x,node.y);
                if (!parent) return {used,true};
                const auto* change=dynamic(node.x,node.y,node.layer);
                const unsigned connection=change?change->connection:parent->connection;
                if (connection && sample(node.x,node.y,connection).legal) {
                    const auto next=encode(node.x,node.y,connection);
                    if (!search.observed(next)) search.relax(id,next,0,estimate);
                }
                if (!node.escaping && !query_.downhillOnly) {
                    cursor.previousCost=search.costOf(id);
                    cursor.line.emplace(LineCell{node.x,node.y},LineCell{query_.goalX,query_.goalY});
                    cursor.stage=CapturedReferenceCursor::Stage::Line;
                } else cursor.stage=CapturedReferenceCursor::Stage::Neighbors;
            } else {
                if (cursor.current!=id) throw std::logic_error("Resumed a different captured expansion");
                const auto& node=cursor.node;
                if (cursor.stage==CapturedReferenceCursor::Stage::Line) {
                    ++used;
                    for (unsigned sampleIndex=0;sampleIndex<PhaseSamplesPerCredit;++sampleIndex) {
                        PhaseLineSample point;
                        if (!cursor.line->next(point)) {
                            cursor.stage=CapturedReferenceCursor::Stage::Neighbors;break;
                        }
                        if (cursor.first) { cursor.first=false;continue; }
                        const int x=int(point.x),y=int(point.y);
                        if (!contains(x,y,node.layer)) { cursor.stage=CapturedReferenceCursor::Stage::Neighbors;break; }
                        const auto next=encode(x,y,node.layer);
                        bool passable;
                        if (query_.movement.corridor)
                            passable=!search.observed(next) && clearance(x,y,node.layer)==query_.pathDiameter;
                        else passable=phasePassable(x,y,node.layer);
                        if (!passable) { cursor.stage=CapturedReferenceCursor::Stage::Neighbors;break; }
                        cursor.previousCost=search.relaxFromObserved(cursor.previous,next,
                            std::uint64_t(cursor.previousCost)+5,estimate);
                        cursor.previous=next;
                    }
                } else {
                    neighbors(id,[&](unsigned next,unsigned cost) {
                        if (cost) emit(next,cost); // The zero-cost portal was relaxed above.
                    },search.parentOf(id),&search);
                    ++used;cursor={};return {used,true};
                }
            }
        }
        return {used,false};
    }
    template<class Workspace,class Emit>
    void referenceNeighbors(unsigned id,Workspace& search,Emit emit) const {
        if (id==terminal_) return;
        const auto node=decode(id);
        const auto* parent=cells(node.layer).cell(node.x,node.y);
        if (!parent) return;
        const auto estimate=[&](unsigned next) { return referenceHeuristic(next); };
        const unsigned connection=dynamic(node.x,node.y,node.layer)
            ? dynamic(node.x,node.y,node.layer)->connection : parent->connection;
        if (connection && sample(node.x,node.y,connection).legal) {
            const auto next=encode(node.x,node.y,connection);
            if (!search.observed(next)) search.relax(id,next,0,estimate);
        }
        if (!node.escaping && !query_.downhillOnly) {
            bool first=true;
            auto previous=id;
            visitPhaseLine({node.x,node.y},{query_.goalX,query_.goalY},[&](std::int64_t xx,std::int64_t yy) {
                if (first) { first=false;return true; }
                const int x=int(xx),y=int(yy);
                const auto* cell=cells(node.layer).cell(x,y);
                if (!cell) return false;
                const auto next=encode(x,y,node.layer);
                if (query_.movement.corridor) {
                    if (search.observed(next) || clearance(x,y,node.layer)!=query_.pathDiameter) return false;
                } else {
                    const auto value=sample(x,y,node.layer);
                    const auto* change=dynamic(x,y,node.layer);
                    if ((change?change->pinched:cell->pinched) ||
                        (change?unsigned(change->terrain):cell->terrain)==unsigned(TerrainKind::cliff) ||
                        !value.legal || value.traffic.enemyFixed || value.traffic.allyFixedCount) return false;
                }
                search.relax(previous,next,5,estimate);
                previous=next;
                return true;
            });
        }
        neighbors(id,[&](unsigned next,unsigned cost) {
            if (cost) emit(next,cost); // Preserve steps which alias another layer.
        },search.parentOf(id),&search);
    }
    std::optional<double> rank(unsigned id,unsigned cost,const DestinationRankQuery& target,
        DestinationQuery destination) const {
        if (id==terminal_) return {};
        const auto node=decode(id);
        if (node.escaping || !inStaticStartComponent(node.x,node.y,node.layer)) return {};
        return rankDestination(target,node.x,node.y,node.layer,cost,[&](int x,int y,unsigned layer) {
            return destinationAllowed(x,y,layer,destination);
        });
    }
    std::optional<double> rankLowerBound(unsigned id,unsigned /*cost*/,
        const DestinationRankQuery& target) const {
        if (id==terminal_) return {};
        const auto node=decode(id);
        if (node.escaping || !inStaticStartComponent(node.x,node.y,node.layer)) return {};
        const double dx=double(target.x)-node.x,dy=double(target.y)-node.y;
        return dx*dx+dy*dy;
    }
    template<class Emit>
    void neighbors(unsigned id,Emit emit,unsigned previousId=std::numeric_limits<unsigned>::max(),
        const RouteSearchWorkspace* observed=nullptr) const {
        if (id==terminal_) return;
        const auto node=decode(id);const auto& plane=cells(node.layer);
        if (!inStaticStartComponent(node.x,node.y,node.layer)) return;
        const auto* parent=plane.cell(node.x,node.y);
        if (!parent || !parent->occupancy.valid) return;
        auto steps=WeightedStepQuery{left_,top_,right_,bottom_,query_.startX,query_.startY,query_.pathDiameter,
            node.escaping,pinchedStart_,query_.downhillOnly,query_.movement.corridor};
        if (previousId<terminal_) {
            const auto previous=decode(previousId);
            steps.incomingX=node.x-previous.x;
            steps.incomingY=node.y-previous.y;
            steps.hasIncoming=true;
        }
        forEachWeightedStep(steps,node.x,node.y,[&](int x,int y) {
            if (observed && !query_.movement.corridor &&
                observed->observed(encode(x,y,node.layer,node.escaping))) return WeightedStepCell{};
            const auto* cell=plane.cell(x,y);
            const auto* dynamicCell=dynamic(x,y,node.layer);
            if (!cell || !(dynamicCell?dynamicCell->valid:cell->occupancy.valid)) return WeightedStepCell{};
            const auto value=sample(x,y,node.layer);
            const bool terrainAllowed=terrainPermits(x,y,node.layer);
            const bool dozerException=!terrainAllowed &&
                (dynamicCell?dozerPassage(*dynamicCell):dozerPassage(*cell));
            return WeightedStepCell{true,value.legal,terrainAllowed,value.traffic,
                dynamicCell?dynamicCell->pinched:cell->pinched,
                !dozerException,dozerException?1000u:0u};
        },[&](int x,int y) { return height(x,y,node.layer); },
            [&](int x,int y) { return clearance(x,y,node.layer); },
            [&](int x,int y,unsigned cost,bool escaping) {
                if (inStaticStartComponent(x,y,node.layer))
                    emit(encode(x,y,node.layer,escaping),cost);
            });
        const unsigned connection=dynamic(node.x,node.y,node.layer)
            ? dynamic(node.x,node.y,node.layer)->connection : parent->connection;
        if (connection && sample(node.x,node.y,connection).legal) emit(encode(node.x,node.y,connection),0);
    }
};
}
