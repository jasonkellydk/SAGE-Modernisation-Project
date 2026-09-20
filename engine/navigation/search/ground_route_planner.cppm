#include <tuple>
#include <atomic>
namespace navigation {
struct GroundRoutePlanner::State {
    std::uint64_t epoch=1,worldEpoch=1;
    struct CellSample { OccupancyResult occupancy{}; bool passable=false,phasePassable=false; };
    struct Scratch {
        RouteSearchWorkspace search;
        HpaClusterSearch hierarchy;
        CellQueryCache<CellSample> cells;
        // Phase rays consume only this permission bit. Keep their repeated
        // reads separate from cold occupancy metadata (four bytes per entry).
        CellQueryCache<std::uint8_t,std::uint16_t> groundPhase;
        CellQueryCache<std::optional<double>> ranks;
    };
    // Terrain permissions and both half-open footprint extents determine
    // which cluster crossings can guide this mover.
    using HierarchyKey=std::tuple<unsigned,bool,int,int>;
    std::map<HierarchyKey,HpaClusterTopology> liveHierarchies;
    int liveOriginX=0,liveOriginY=0;
    std::uint64_t hierarchySearches=0,hierarchyRefinements=0;
    // Four ordinary searches and one reserved slot for a fresh player order.
    // Waiting orders own no search workspace and have no population limit.
    static constexpr std::size_t OrdinaryCapturedSearches=4;
    static constexpr std::size_t MaximumCapturedSearches=OrdinaryCapturedSearches+1;
    std::vector<std::unique_ptr<Scratch>> freeScratch;
    std::size_t leasedScratch=0,leasedCapturedScratch=0,leasedPlayerScratch=0;
    std::unique_ptr<Scratch> acquireScratch() {
        if (freeScratch.empty()) return std::make_unique<Scratch>();
        auto value=std::move(freeScratch.back());freeScratch.pop_back();
        return value;
    }
    struct Lease {
        State& owner;
        std::unique_ptr<Scratch> value;
        bool captured,player;
        Lease(State& state,bool capture,bool command):owner(state),value(state.acquireScratch()),captured(capture),player(command) {
            ++owner.leasedScratch;
            if (captured) ++owner.leasedCapturedScratch;
            if (player) ++owner.leasedPlayerScratch;
        }
        Lease(const Lease&)=delete;
        ~Lease() {
            --owner.leasedScratch;
            if (captured) --owner.leasedCapturedScratch;
            if (player) --owner.leasedPlayerScratch;
            if (owner.freeScratch.size()<MaximumCapturedSearches) owner.freeScratch.push_back(std::move(value));
        }
    };
    std::map<ObjectID,WeightedTask> pending;
    std::optional<WeightedTask> standalone;
    std::uint64_t sliceSequence=0,lastDispatchedSlice=0,dispatchCycleEnd=0;
    unsigned consecutivePlayerSlices=0;
    std::shared_ptr<CapturedSlice> completedSlice;
    unsigned capturedFrame=~0u;
    std::uint64_t capturedEpoch=0;
    unsigned capturedDynamicFrame=~0u;
    std::uint64_t capturedDynamicEpoch=0;
    std::vector<CapturedLayer> capturedLayers;
    bool fullStaticCapture=true;
    std::vector<std::uint8_t> dirtyStaticCells;
    std::vector<std::size_t> dirtyStaticIndices;
    void clearStaticChanges() {
        for (const auto index:dirtyStaticIndices) dirtyStaticCells[index]=0;
        dirtyStaticIndices.clear();
    }
    std::shared_ptr<const HpaHierarchy> staticHpa;
    std::shared_ptr<const LayeredHpaHierarchy> layeredHpa;
    std::atomic<std::uint64_t> clearanceEvaluations{0};
    std::atomic<std::uint64_t> clearanceHits{0};
    std::vector<CapturedOccupancyCell> capturedOccupancyCells;
    std::shared_ptr<const CapturedOccupancyIndex> capturedDynamicOccupancy;
    unsigned capturedFixedFrame=~0u;
    std::vector<std::uint32_t> capturedOccupantIds;
    std::vector<std::uint8_t> dirtyDynamicCells;
    std::vector<std::size_t> dirtyDynamicIndices;
    void clearDynamicChanges() {
        for (const auto index:dirtyDynamicIndices) dirtyDynamicCells[index]=0;
        dirtyDynamicIndices.clear();
    }
    // IDs found in the immutable map snapshot. Dynamic capture starts from
    // this canonical set instead of allocating/sorting a fresh ID list from
    // every CellSnapshot on every frame.
    std::vector<std::uint32_t> staticOccupantIds;
    void refreshCapturedOccupantIds() {
        capturedOccupantIds=staticOccupantIds;
        for (const auto& cell:capturedOccupancyCells)
            for (const auto id:{cell.occupancy.unit,cell.goal,cell.aircraftGoal})
                if (id && id!=std::numeric_limits<std::uint32_t>::max()) capturedOccupantIds.push_back(id);
        std::sort(capturedOccupantIds.begin(),capturedOccupantIds.end());
        capturedOccupantIds.erase(std::unique(capturedOccupantIds.begin(),capturedOccupantIds.end()),
            capturedOccupantIds.end());
    }
    struct OccupantKey {
        std::uintptr_t team=0;
        unsigned crusherLevel=0;
        bool defector=false, unmanned=false, dozer=false;
        auto operator<=>(const OccupantKey&) const = default;
    };
    std::map<OccupantKey,std::shared_ptr<const OccupantSnapshot>> capturedOccupants;
    std::array<FootprintReachability,2> reachability;
    IRegion2D logicalExtent{};
    bool logicalExtentValid=false;
    Int reachabilityOriginX = 0, reachabilityOriginY = 0;
};
void GroundRoutePlanner::WeightedResult::DeletePath::operator()(Path* path) const {
    if (path) deleteInstance(path);
}
bool GroundRoutePlanner::WeightedTask::obsolete() const {
    if (owner && (slice && slice->retainAcrossRegionalEdits
        ? owner->worldEpoch!=slice->worldEpoch : owner->epoch!=epoch)) return true;
    if (!mover) return false;
    const auto* current=TheGameLogic?TheGameLogic->findObjectByID(moverId):nullptr;
    if (current!=mover || current->isDestroyed()) return true;
    const auto* ai=current->getAIUpdateInterface();
    return (ai?ai->getPathRequestRevision():0)!=requestRevision;
}
bool GroundRoutePlanner::WeightedTask::discardObsolete() {
    if (obsolete()) {
        if (slice) slice->work={};
        continuation={};
        cancelled=true;
    }
    return cancelled;
}
bool GroundRoutePlanner::WeightedTask::canStart() const {
    if (started || !slice || !owner) return true;
    if (owner->leasedCapturedScratch>=State::MaximumCapturedSearches) return false;
    if (!slice->playerCommand && owner->leasedCapturedScratch-owner->leasedPlayerScratch>=State::OrdinaryCapturedSearches)
        return false;
    const auto earlier=[&](const WeightedTask& other) {
        return !other.started && other.slice && other.slice->playerCommand==slice->playerCommand &&
            other.slice->sequence<slice->sequence && !other.obsolete();
    };
    for (const auto& [id,other]:owner->pending) if (earlier(other)) return false;
    return !owner->standalone || !earlier(*owner->standalone);
}
bool GroundRoutePlanner::WeightedTask::done() const {
    return cancelled || obsolete() || continuation.done();
}
GroundRoutePlanner::WeightedResult GroundRoutePlanner::WeightedTask::take() {
    if (discardObsolete()) return {};
    auto result=continuation.take();
    // A negative answer has no route to validate locally. Any terrain edit
    // may have opened a route, so retry against a fresh snapshot.
    if (owner && slice && slice->retainAcrossRegionalEdits &&
        slice->terrainEpoch!=owner->epoch && !result.path && result.waypoints.empty())
        result.staleTerrain=true;
    return result;
}
bool GroundRoutePlanner::WeightedTask::resume() {
    if (discardObsolete()) return false;
    if (continuation.done()) return false;
    if (!canStart()) return true;
    if (slice && slice->work) return true;
    struct Restore {
        ObjectID& target;
        ObjectID previous;
        ~Restore() { target=previous; }
    } restore{world->m_ignoreObstacleID,world->m_ignoreObstacleID};
    world->m_ignoreObstacleID=ignoredObstacle;
    started=true;
    return continuation.resume();
}
GroundRoutePlanner::GroundRoutePlanner(Pathfinder& world)
    : state_(std::make_unique<State>()), world_(world), movement_(world), builder_(world) {
    state_->freeScratch.reserve(State::MaximumCapturedSearches);
}
GroundRoutePlanner::~GroundRoutePlanner() = default;
void GroundRoutePlanner::smooth(Path* path,const Pathfinder::GroundRouteQuery& query) {
    if (query.usePathDiameter) path->optimizeGroundPath(query.crusher,query.pathDiameter);
    else path->optimize(query.object,query.acceptableSurfaces,query.considerTransient);
}
void GroundRoutePlanner::prepareCapturedSnapshot(const Pathfinder::GroundRouteQuery&,Bool downhillOnly,
    Bool /*refreshDynamic*/) {
    const auto frame=TheGameLogic?TheGameLogic->getFrame():0u;
    // Terrain and static topology survive simulation frames. Dynamic unit
    // occupancy is captured separately at the batch boundary; rebuilding the
    // full map here every frame was the dominant source of stalls.
    const bool needStatic=state_->capturedEpoch!=state_->epoch || state_->capturedLayers.empty();
    if (needStatic) {
        auto captureTiming=diagnostics::frameCapture().measure("Navigation.Capture.Static",frame);
        const auto previousLayers=std::move(state_->capturedLayers);
        state_->capturedLayers.clear();
        std::sort(state_->dirtyStaticIndices.begin(),state_->dirtyStaticIndices.end());
        state_->capturedOccupantIds.clear();
        state_->staticOccupantIds.clear();
        state_->capturedOccupants.clear();
        std::vector<PathfindLayerEnum> layers;
        for (int layer=LAYER_GROUND;layer<=LAYER_LAST;++layer) {
            if (layer!=LAYER_GROUND && world_.m_layers[layer].isUnused()) continue;
            layers.push_back(static_cast<PathfindLayerEnum>(layer));
        }
        bool sameLayered=state_->layeredHpa && !state_->fullStaticCapture &&
            previousLayers.size()==layers.size();
        bool allRegional=!state_->fullStaticCapture && previousLayers.size()==layers.size();
        for (const auto layer:layers) {
            const auto previous=std::find_if(previousLayers.begin(),previousLayers.end(),[&](const auto& plane) {
                return plane.cells && plane.cells->layer()==unsigned(layer);
            });
            const auto& extent=world_.m_extent;
            const bool regional=!state_->fullStaticCapture && previous!=previousLayers.end() &&
                previous->cells->left()==extent.lo.x && previous->cells->top()==extent.lo.y &&
                previous->cells->right()==extent.hi.x && previous->cells->bottom()==extent.hi.y;
            std::vector<CellSnapshotEdit> edits;
            std::shared_ptr<const CellSnapshot> cells;
            if (regional) {
                edits.reserve(state_->dirtyStaticIndices.size());
                const auto height=previous->cells->height();
                for (const auto index:state_->dirtyStaticIndices) {
                    const auto x=extent.lo.x+int(index/height),y=extent.lo.y+int(index%height);
                    edits.push_back({x,y,movement_.captureCell(layer,x,y)});
                }
                cells=std::make_shared<const CellSnapshot>(previous->cells->updatedCells(edits));
            } else {
                cells=std::make_shared<const CellSnapshot>(movement_.captureCells(layer));
            }
            if (!regional) { sameLayered=false;allRegional=false; }
            if (sameLayered) for (const auto& edit:edits)
                if (edit.value.connection!=previous->cells->cell(edit.x,edit.y)->connection) {
                    sameLayered=false;break;
                }
            std::shared_ptr<const HpaHierarchy> hpa=previous!=previousLayers.end()?previous->hpa:nullptr;
            {
                const auto open=[cells](int x,int y) {
                    const auto* cell=cells->cell(x+cells->left(),y+cells->top());
                    return cell && cell->occupancy.valid &&
                        static_cast<TerrainKind>(cell->terrain)!=TerrainKind::impassable;
                };
                // Cache the hierarchy by its actual traversal input. Terrain
                // labels can change fine movement policy without changing this
                // deliberately permissive abstraction. Compare the existing
                // component plane exactly; a digest is not needed for identity.
                bool same=hpa && hpa->width==cells->width() &&
                    hpa->height==cells->height() &&
                    hpa->fineComponent.size()==cells->width()*cells->height();
                if (same && regional) {
                    // Unedited cells retain the same immutable pages. Only the
                    // journal can change the abstraction's open/closed input.
                    for (const auto& edit:edits) {
                        const auto* before=previous->cells->cell(edit.x,edit.y);
                        const bool wasOpen=before->occupancy.valid &&
                            static_cast<TerrainKind>(before->terrain)!=TerrainKind::impassable;
                        if (wasOpen!=open(edit.x-cells->left(),edit.y-cells->top())) {
                            same=false;break;
                        }
                    }
                } else for (unsigned x=0;same && x<cells->width();++x)
                    for (unsigned y=0;y<cells->height();++y) {
                        const bool previous=hpa->fineComponent[
                            std::size_t(y)*cells->width()+x]!=std::numeric_limits<std::uint32_t>::max();
                        if (previous!=open(int(x),int(y))) { same=false;break; }
                    }
                if (!same) {
                    hpa=buildHpaHierarchy(static_cast<unsigned>(cells->width()),
                        static_cast<unsigned>(cells->height()),open,8);
                }
            }
            if (layer==LAYER_GROUND) state_->staticHpa=hpa;
            if (sameLayered && hpa!=previous->hpa) sameLayered=false;
            state_->capturedLayers.push_back({std::move(cells),{},std::move(hpa)});
        }
        if (!sameLayered) {
        std::vector<HpaLayerInput> hpaInputs;
        hpaInputs.reserve(state_->capturedLayers.size());
        for (const auto& plane:state_->capturedLayers) {
            if (!plane.cells || !plane.hpa) continue;
            const auto cells=plane.cells;
            hpaInputs.push_back({plane.cells->layer(),plane.hpa,[cells](int x,int y) {
                const auto* cell=cells->cell(x+cells->left(),y+cells->top());
                return cell ? unsigned(cell->connection) : 0u;
            }});
        }
        state_->layeredHpa=buildLayeredHpaHierarchy(hpaInputs);
        }
        for (const auto& plane:state_->capturedLayers) {
            if (!plane.cells) continue;
            const auto ids=plane.cells->occupantIds();
            state_->staticOccupantIds.insert(state_->staticOccupantIds.end(),ids.begin(),ids.end());
        }
        std::sort(state_->staticOccupantIds.begin(),state_->staticOccupantIds.end());
        state_->staticOccupantIds.erase(std::unique(state_->staticOccupantIds.begin(),
            state_->staticOccupantIds.end()),state_->staticOccupantIds.end());
        state_->capturedOccupantIds=state_->staticOccupantIds;
        state_->capturedFrame=frame;
        state_->capturedEpoch=state_->epoch;
        // Regional edits are also in the dynamic journal. Its old overrides
        // remain valid outside those coordinates; edited cells are rebased by
        // the next incremental capture, including intervening unit writes.
        if (allRegional && state_->capturedDynamicOccupancy)
            state_->capturedDynamicEpoch=state_->epoch;
        else state_->capturedDynamicEpoch=0;
        state_->clearStaticChanges();
        const auto journalWidth=std::size_t(world_.m_extent.hi.x-world_.m_extent.lo.x+1);
        const auto journalHeight=std::size_t(world_.m_extent.hi.y-world_.m_extent.lo.y+1);
        state_->dirtyStaticCells.resize(journalWidth*journalHeight,0);
        state_->fullStaticCapture=false;
    }
    if (downhillOnly) {
        const auto cellCount=std::size_t(world_.m_extent.hi.x-world_.m_extent.lo.x+1) *
            std::size_t(world_.m_extent.hi.y-world_.m_extent.lo.y+1);
        for (auto& plane:state_->capturedLayers) {
            if (plane.heights) continue;
            auto heights=std::make_shared<std::vector<float>>();
            heights->reserve(cellCount);
            for (int x=world_.m_extent.lo.x;x<=world_.m_extent.hi.x;++x)
                for (int y=world_.m_extent.lo.y;y<=world_.m_extent.hi.y;++y)
                    heights->push_back(TheTerrainLogic->getLayerHeight(x*PATHFIND_CELL_SIZE_F,
                        y*PATHFIND_CELL_SIZE_F,static_cast<PathfindLayerEnum>(plane.cells->layer())));
            plane.heights=std::move(heights);
        }
    }
}
void GroundRoutePlanner::captureDynamicSnapshotFull() {
    const auto frame=TheGameLogic?TheGameLogic->getFrame():0u;
    if (state_->capturedDynamicOccupancy && state_->capturedFixedFrame==frame && state_->capturedDynamicEpoch==state_->epoch &&
        state_->dirtyDynamicIndices.empty()) return;
    auto captureTiming=diagnostics::frameCapture().measure("Navigation.Capture.Dynamic",frame);
    state_->clearDynamicChanges();
    const auto journalWidth=std::size_t(world_.m_extent.hi.x-world_.m_extent.lo.x+1);
    const auto journalHeight=std::size_t(world_.m_extent.hi.y-world_.m_extent.lo.y+1);
    state_->dirtyDynamicCells.assign(journalWidth*journalHeight,0);
    state_->capturedOccupancyCells.clear();
    state_->capturedOccupantIds=state_->staticOccupantIds;

    for (const auto& plane:state_->capturedLayers) {
        if (!plane.cells) continue;
        const auto* cells=plane.cells.get();
        const auto layer=cells->layer();
        const int xLeft=cells->left(),xRight=cells->right();
        for (int x=xLeft;x<=xRight;++x) for (int y=cells->top();y<=cells->bottom();++y) {
            const auto* native=layer==LAYER_GROUND
                ? &world_.m_map[x][y]
                : world_.getCell(static_cast<PathfindLayerEnum>(layer),x,y);
            if (!native) continue;
            const auto flags=native->getFlags();
            const bool goal=flags==PathfindCell::UNIT_GOAL ||
                flags==PathfindCell::UNIT_GOAL_OTHER_MOVING;
            const bool moving=flags==PathfindCell::UNIT_PRESENT_MOVING || goal;
            const bool fixed=flags==PathfindCell::UNIT_PRESENT_FIXED;
            const bool empty=flags==PathfindCell::NO_UNITS;
            const std::uint32_t unit=(moving || fixed) ? std::uint32_t(native->getPosUnit()) : 0u;
            const OccupancyCell occupancy{unit,true,empty,goal,moving,fixed};
            const std::uint32_t goalId=std::uint32_t(native->getGoalUnit());
            const std::uint32_t aircraftGoal=std::uint32_t(native->getGoalAircraft());
            const bool aircraftReserved=native->isAircraftGoal()!=0;
            const auto* captured=cells->cell(x,y);
            if (!captured || captured->occupancy.unit!=occupancy.unit ||
                captured->occupancy.valid!=occupancy.valid || captured->occupancy.empty!=occupancy.empty ||
                captured->occupancy.goal!=goal || captured->occupancy.moving!=moving ||
                captured->occupancy.fixed!=fixed || captured->goal!=goalId ||
                captured->aircraftGoal!=aircraftGoal || captured->aircraftReserved!=aircraftReserved) {
                state_->capturedOccupancyCells.push_back({layer,x,y,occupancy,goalId,aircraftGoal,
                    aircraftReserved,static_cast<TerrainKind>(native->getType()),
                    std::uint32_t(native->getObstacleID()),true,native->getPinched()!=0,
                    native->isObstacleFence()!=0,static_cast<unsigned>(native->getConnectLayer())});
            }
            for (const auto id:{unit,goalId,aircraftGoal})
                if (id && id!=std::numeric_limits<std::uint32_t>::max()) state_->capturedOccupantIds.push_back(id);
        }
    }
    std::sort(state_->capturedOccupancyCells.begin(),state_->capturedOccupancyCells.end(),
        [](const CapturedOccupancyCell& a,const CapturedOccupancyCell& b) {
            return std::tie(a.layer,a.x,a.y)<std::tie(b.layer,b.x,b.y);
        });
    state_->capturedOccupancyCells.erase(std::unique(state_->capturedOccupancyCells.begin(),
        state_->capturedOccupancyCells.end(),[](const auto& a,const auto& b) {
            return a.layer==b.layer && a.x==b.x && a.y==b.y;
        }),state_->capturedOccupancyCells.end());
    std::sort(state_->capturedOccupantIds.begin(),state_->capturedOccupantIds.end());
    state_->capturedOccupantIds.erase(std::unique(state_->capturedOccupantIds.begin(),
        state_->capturedOccupantIds.end()),state_->capturedOccupantIds.end());
    state_->capturedOccupants.clear();
    state_->capturedFixedFrame=frame;
    state_->capturedDynamicFrame=frame;
    state_->capturedDynamicEpoch=state_->epoch;
    const auto dynamicCells=std::make_shared<const std::vector<CapturedOccupancyCell>>(
        state_->capturedOccupancyCells);
    unsigned layerCount=1;
    for (const auto& plane:state_->capturedLayers)
        if (plane.cells) layerCount=std::max(layerCount,plane.cells->layer()+1);
    state_->capturedDynamicOccupancy=std::make_shared<const CapturedOccupancyIndex>(
        dynamicCells,world_.m_extent.lo.x,world_.m_extent.lo.y,
        world_.m_extent.hi.x,world_.m_extent.hi.y,layerCount);
}

void GroundRoutePlanner::captureDynamicSnapshotIncremental() {
    const auto frame=TheGameLogic?TheGameLogic->getFrame():0u;
    const bool full=state_->capturedDynamicEpoch!=state_->epoch ||
        state_->capturedDynamicOccupancy==nullptr;
    if (full) { captureDynamicSnapshotFull();return; }
    // Each prepared query owns its immutable view. Publishing a newer capture
    // here cannot alter a suspended search's terrain or occupancy inputs.
    if (state_->capturedFixedFrame==frame && state_->dirtyDynamicIndices.empty()) return;
    auto captureTiming=diagnostics::frameCapture().measure("Navigation.Capture.Delta",frame);

    std::vector<IRegion2D> regions;
    {
        const auto height=std::size_t(world_.m_extent.hi.y-world_.m_extent.lo.y+1);
        std::sort(state_->dirtyDynamicIndices.begin(),state_->dirtyDynamicIndices.end());
        for (const auto index:state_->dirtyDynamicIndices) {
            const ICoord2D cell{int(index/height)+world_.m_extent.lo.x,
                int(index%height)+world_.m_extent.lo.y};
            regions.push_back({cell,cell});
        }
        if (regions.empty()) {
            // Cell occupancy is unchanged, but relationships, crushability,
            // and temporary unit state can change without a cell write.
            state_->capturedOccupants.clear();
            state_->refreshCapturedOccupantIds();
            state_->capturedFixedFrame=frame;
            state_->capturedDynamicFrame=frame;
            return;
        }
        state_->capturedOccupancyCells.erase(std::remove_if(state_->capturedOccupancyCells.begin(),
            state_->capturedOccupancyCells.end(),[&](const CapturedOccupancyCell& cell) {
                const auto index=std::size_t(cell.x-world_.m_extent.lo.x)*height+
                    std::size_t(cell.y-world_.m_extent.lo.y);
                return state_->dirtyDynamicCells[index]!=0;
            }),state_->capturedOccupancyCells.end());
        state_->clearDynamicChanges();
    }

    auto captureCell=[&](const CapturedLayer& plane,int x,int y,
        std::vector<CapturedOccupancyCell>& target) {
        const auto* native=plane.cells->layer()==LAYER_GROUND
            ? &world_.m_map[x][y]
            : world_.getCell(static_cast<PathfindLayerEnum>(plane.cells->layer()),x,y);
        if (!native) return;
        const auto flags=native->getFlags();
        const bool goal=flags==PathfindCell::UNIT_GOAL ||
            flags==PathfindCell::UNIT_GOAL_OTHER_MOVING;
        const bool moving=flags==PathfindCell::UNIT_PRESENT_MOVING || goal;
        const bool fixed=flags==PathfindCell::UNIT_PRESENT_FIXED;
        const bool empty=flags==PathfindCell::NO_UNITS;
        const std::uint32_t unit=(moving || fixed) ? std::uint32_t(native->getPosUnit()) : 0u;
        const OccupancyCell occupancy{unit,true,empty,goal,moving,fixed};
        const std::uint32_t goalId=std::uint32_t(native->getGoalUnit());
        const std::uint32_t aircraftGoal=std::uint32_t(native->getGoalAircraft());
        const bool aircraftReserved=native->isAircraftGoal()!=0;
        const auto terrain=static_cast<TerrainKind>(native->getType());
        const std::uint32_t obstacle=std::uint32_t(native->getObstacleID());
        const bool valid=true,pinched=native->getPinched()!=0,fence=native->isObstacleFence()!=0;
        const unsigned connection=static_cast<unsigned>(native->getConnectLayer());
        const auto* captured=plane.cells->cell(x,y);
        if (!captured || captured->occupancy.unit!=occupancy.unit ||
            captured->occupancy.valid!=occupancy.valid || captured->occupancy.empty!=occupancy.empty ||
            captured->occupancy.goal!=goal || captured->occupancy.moving!=moving ||
            captured->occupancy.fixed!=fixed || captured->goal!=goalId ||
            captured->aircraftGoal!=aircraftGoal || captured->aircraftReserved!=aircraftReserved)
            target.push_back({plane.cells->layer(),x,y,occupancy,goalId,aircraftGoal,
                aircraftReserved,terrain,obstacle,valid,pinched,fence,connection});
    };
    for (const auto& region:regions) {
        const int left=std::max(region.lo.x,world_.m_extent.lo.x);
        const int top=std::max(region.lo.y,world_.m_extent.lo.y);
        const int right=std::min(region.hi.x,world_.m_extent.hi.x);
        const int bottom=std::min(region.hi.y,world_.m_extent.hi.y);
        if (left>right || top>bottom) continue;
        for (const auto& plane:state_->capturedLayers) {
            if (!plane.cells) continue;
            const int xLeft=std::max(left,plane.cells->left());
            const int yTop=std::max(top,plane.cells->top());
            const int xRight=std::min(right,plane.cells->right());
            const int yBottom=std::min(bottom,plane.cells->bottom());
            for (int x=xLeft;x<=xRight;++x)
                for (int y=yTop;y<=yBottom;++y)
                    captureCell(plane,x,y,state_->capturedOccupancyCells);
        }
    }
    std::sort(state_->capturedOccupancyCells.begin(),state_->capturedOccupancyCells.end(),
        [](const CapturedOccupancyCell& a,const CapturedOccupancyCell& b) {
            return std::tie(a.layer,a.x,a.y)<std::tie(b.layer,b.x,b.y);
        });
    state_->capturedOccupancyCells.erase(std::unique(state_->capturedOccupancyCells.begin(),
        state_->capturedOccupancyCells.end(),[](const auto& a,const auto& b) {
            return a.layer==b.layer && a.x==b.x && a.y==b.y;
        }),state_->capturedOccupancyCells.end());
    state_->refreshCapturedOccupantIds();
    state_->capturedOccupants.clear();
    state_->capturedFixedFrame=frame;
    state_->capturedDynamicFrame=frame;
    state_->capturedDynamicEpoch=state_->epoch;
    const auto dynamicCells=std::make_shared<const std::vector<CapturedOccupancyCell>>(
        state_->capturedOccupancyCells);
    unsigned layerCount=1;
    for (const auto& plane:state_->capturedLayers)
        if (plane.cells) layerCount=std::max(layerCount,plane.cells->layer()+1);
    state_->capturedDynamicOccupancy=std::make_shared<const CapturedOccupancyIndex>(
        dynamicCells,world_.m_extent.lo.x,world_.m_extent.lo.y,
        world_.m_extent.hi.x,world_.m_extent.hi.y,layerCount);
}

void GroundRoutePlanner::captureDynamicSnapshot() {
    captureDynamicSnapshotIncremental();
}
void GroundRoutePlanner::prepareCapturedOccupant(const Pathfinder::GroundRouteQuery& query) {
    auto timing=diagnostics::frameCapture().measure("Navigation.Capture.Occupants",
        TheGameLogic?TheGameLogic->getFrame():0,query.object?unsigned(query.object->getID()):0);
    const auto context=MovementValidator::prepare(query.object,query.radius,query.centerInCell);
    const State::OccupantKey key{
        query.object?reinterpret_cast<std::uintptr_t>(query.object->getTeam()):0,
        query.object?static_cast<unsigned>(query.object->getCrusherLevel()):0,
        query.object && query.object->getIsUndetectedDefector()!=0,
        query.object && query.object->isDisabledByType(DISABLED_UNMANNED)!=0,
        query.object && query.object->isKindOf(KINDOF_DOZER)!=0};
    if (!state_->capturedOccupants.contains(key))
        state_->capturedOccupants.emplace(key,std::make_shared<const OccupantSnapshot>(
            MovementValidator::captureOccupants(context,state_->capturedOccupantIds)));
}
std::shared_ptr<const HpaClusterGraph> GroundRoutePlanner::prepareLiveHierarchy(
    const Pathfinder::GroundRouteQuery& query,std::span<const std::uint8_t>* terrain) {
    auto hierarchyTiming=diagnostics::frameCapture().measure("Navigation.Hierarchy.Prepare",
        TheGameLogic?TheGameLogic->getFrame():0,query.object?unsigned(query.object->getID()):0);
    const auto& extent=world_.m_extent;
    if (state_->liveOriginX!=extent.lo.x || state_->liveOriginY!=extent.lo.y) {
        state_->liveHierarchies.clear();
        state_->liveOriginX=extent.lo.x;state_->liveOriginY=extent.lo.y;
    }
    // Exceptional obstacle permissions are deliberately overestimated here.
    // The fine graph still owns object-specific permissions and occupancy.
    const bool exceptional=world_.m_ignoreObstacleID!=INVALID_ID || query.crusher ||
        (query.object && query.object->isKindOf(KINDOF_DOZER));
    // A group corridor may narrow and therefore checks only its anchor.
    const int radius=query.usePathDiameter?0:query.radius;
    const int above=query.usePathDiameter?1:std::max(1,radius+(query.centerInCell?1:0));
    auto& topology=state_->liveHierarchies[{unsigned(query.acceptableSurfaces),exceptional,radius,above}];
    topology.configure(extent.hi.x-extent.lo.x+1,extent.hi.y-extent.lo.y+1);
    auto graph=topology.prepare([&](int x,int y) {
        for (int yy=y-radius;yy<y+above;++yy) for (int xx=x-radius;xx<x+above;++xx) {
            const auto* cell=world_.getCell(LAYER_GROUND,xx+extent.lo.x,yy+extent.lo.y);
            if (!cell) return false;
            if (exceptional && (cell->getType()==PathfindCell::CELL_OBSTACLE || cell->isObstacleFence())) continue;
            if (!permitsTerrain({std::uint32_t(query.acceptableSurfaces),0,false},
                {static_cast<TerrainKind>(cell->getType()),std::uint32_t(cell->getObstacleID()),true,cell->isObstacleFence()!=0}))
                return false;
        }
        return true;
    });
    if (terrain) *terrain=topology.preparedCells();
    return graph;
}
void GroundRoutePlanner::warmStaticSnapshot() {
    if (!world_.m_isMapReady || state_->completedSlice || state_->leasedScratch) return;
    Pathfinder::GroundRouteQuery query;
    query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
    prepareCapturedSnapshot(query,false);
    for (const auto surfaces:std::array<LocomotorSurfaceTypeMask,2>{LOCOMOTORSURFACE_GROUND,
            LOCOMOTORSURFACE_GROUND|LOCOMOTORSURFACE_RUBBLE})
        for (int radius=0;radius<=2;++radius) for (const bool centered:{false,true})
            for (const bool exceptional:{false,true}) {
                query.acceptableSurfaces=surfaces;query.radius=radius;query.centerInCell=centered;
                query.crusher=exceptional;
                prepareLiveHierarchy(query);
            }
    // Allocate the reusable SoA workspace during map setup, not on the first
    // unit order. Include escaping states and every active layer.
    std::uint64_t layerCount=1;
    for (int layer=LAYER_GROUND+1;layer<=LAYER_LAST;++layer)
        if (!world_.m_layers[layer].isUnused()) ++layerCount;
    const auto cells=std::uint64_t(world_.m_extent.hi.x-world_.m_extent.lo.x+1)*
        (world_.m_extent.hi.y-world_.m_extent.lo.y+1)*layerCount;
    if (cells*2+1<UINT32_MAX) {
        while (state_->freeScratch.size()<State::MaximumCapturedSearches)
            state_->freeScratch.push_back(std::make_unique<State::Scratch>());
        for (const auto& cached:state_->freeScratch) {
            auto& scratch=*cached;
            scratch.search.begin(unsigned(cells*2+1),0,0,[](unsigned) { return 0u; });
            scratch.search.cancel();
            scratch.cells.begin(std::size_t(cells));scratch.ranks.begin(std::size_t(cells));
            scratch.groundPhase.begin(std::size_t(cells/layerCount));
        }
    }

}
void GroundRoutePlanner::reset() {
    ++state_->worldEpoch;
    state_->liveHierarchies.clear();
    state_->hierarchySearches=state_->hierarchyRefinements=0;
    state_->completedSlice.reset();
    ++state_->epoch;
    state_->pending.clear();
    state_->standalone.reset();
    state_->sliceSequence=state_->lastDispatchedSlice=state_->dispatchCycleEnd=0;
    state_->consecutivePlayerSlices=0;
    state_->capturedLayers.clear();
    state_->fullStaticCapture=true;
    state_->clearStaticChanges();
    state_->dirtyStaticCells.clear();
    state_->staticHpa.reset();
    state_->layeredHpa.reset();
    state_->capturedOccupantIds.clear();
    state_->staticOccupantIds.clear();
    state_->capturedOccupants.clear();
    state_->capturedOccupancyCells.clear();
    state_->capturedDynamicOccupancy.reset();
    state_->clearDynamicChanges();
    state_->capturedFixedFrame=~0u;
    state_->capturedDynamicFrame=~0u;
    state_->capturedDynamicEpoch=0;
    state_->clearanceEvaluations.store(0,std::memory_order_relaxed);
    state_->clearanceHits.store(0,std::memory_order_relaxed);
    for (auto& cache:state_->reachability) cache.reset();
    state_->logicalExtentValid=false;
    stats_ = {};
}
void GroundRoutePlanner::invalidate() {
    ++state_->worldEpoch;
    state_->fullStaticCapture=true;
    state_->clearStaticChanges();
    for (auto& [key,hierarchy]:state_->liveHierarchies)
        hierarchy.invalidate(0,0,world_.m_extent.hi.x-world_.m_extent.lo.x,
            world_.m_extent.hi.y-world_.m_extent.lo.y);
    ++state_->epoch;
    state_->capturedFixedFrame=~0u;
    state_->capturedDynamicFrame=~0u;
    state_->capturedDynamicEpoch=0;
    state_->capturedDynamicOccupancy.reset();
    state_->clearDynamicChanges();
    state_->layeredHpa.reset();
    for (auto& cache:state_->reachability) cache.invalidate();
}
void GroundRoutePlanner::invalidate(const IRegion2D& region) {
    markDynamicRegion(region);
    // Before the first capture (or following a global invalidation), a full
    // capture already owns this work. Otherwise record each coordinate once.
    if (!state_->fullStaticCapture && !state_->dirtyStaticCells.empty()) {
        const auto& extent=world_.m_extent;
        const auto height=std::size_t(extent.hi.y-extent.lo.y+1);
        const auto width=std::size_t(extent.hi.x-extent.lo.x+1);
        if (state_->dirtyStaticCells.size()!=width*height) {
            state_->fullStaticCapture=true;
            state_->clearStaticChanges();
        } else {
            for (int x=std::max(region.lo.x,extent.lo.x);x<=std::min(region.hi.x,extent.hi.x);++x)
                for (int y=std::max(region.lo.y,extent.lo.y);y<=std::min(region.hi.y,extent.hi.y);++y) {
                    const auto index=std::size_t(x-extent.lo.x)*height+std::size_t(y-extent.lo.y);
                    if (!state_->dirtyStaticCells[index]) {
                        state_->dirtyStaticCells[index]=1;
                        state_->dirtyStaticIndices.push_back(index);
                    }
                }
        }
    }
    for (auto& [key,hierarchy]:state_->liveHierarchies) {
        // A changed terrain cell affects every anchor whose footprint covers
        // it, including anchors outside the edited cell's own cluster.
        const int radius=std::get<2>(key),above=std::get<3>(key);
        hierarchy.invalidate(region.lo.x-state_->liveOriginX-above+1,region.lo.y-state_->liveOriginY-above+1,
            region.hi.x-state_->liveOriginX+radius,region.hi.y-state_->liveOriginY+radius);
    }
    ++state_->epoch;
    state_->capturedFixedFrame=~0u;
    state_->capturedDynamicFrame=~0u;
    // Keep the captured layered graph until preparation compares its exact
    // connectivity and connection inputs. Regional labels need not alter it.
    for (auto& cache:state_->reachability) cache.invalidate(region.lo.x - state_->reachabilityOriginX,
        region.lo.y - state_->reachabilityOriginY, region.hi.x - state_->reachabilityOriginX,
        region.hi.y - state_->reachabilityOriginY);
}
void GroundRoutePlanner::markDynamicRegion(const IRegion2D& region) {
    if (region.lo.x>region.hi.x || region.lo.y>region.hi.y) return;
    // Until an incremental snapshot exists, its first full capture observes
    // every cell. Do not accumulate an unbounded log during ordinary gameplay.
    if (!state_->capturedDynamicOccupancy || state_->dirtyDynamicCells.empty()) return;
    const auto& extent=world_.m_extent;
    const auto height=std::size_t(extent.hi.y-extent.lo.y+1);
    const auto width=std::size_t(extent.hi.x-extent.lo.x+1);
    if (state_->dirtyDynamicCells.size()!=width*height) return;
    for (int x=std::max(region.lo.x,extent.lo.x);x<=std::min(region.hi.x,extent.hi.x);++x)
        for (int y=std::max(region.lo.y,extent.lo.y);y<=std::min(region.hi.y,extent.hi.y);++y) {
            const auto index=std::size_t(x-extent.lo.x)*height+std::size_t(y-extent.lo.y);
            if (!state_->dirtyDynamicCells[index]) {
                state_->dirtyDynamicCells[index]=1;
                state_->dirtyDynamicIndices.push_back(index);
            }
        }
}
Bool GroundRoutePlanner::definitelyDisconnected(const Pathfinder::GroundRouteQuery& query,
    const Coord3D* from, const Coord3D* to)
{
    if (!world_.m_isMapReady || !from || !to ||
        (query.acceptableSurfaces != LOCOMOTORSURFACE_GROUND &&
         query.acceptableSurfaces != (LOCOMOTORSURFACE_GROUND|LOCOMOTORSURFACE_RUBBLE)) ||
        (query.object && query.object->getLayer() != LAYER_GROUND)) return false;
    Coord3D start = *from, goal = *to;
    ICoord2D originalStart;
    if (world_.worldToCell(from, &originalStart)) return false;
    world_.clip(&start, &goal);
    if (!query.centerInCell && !query.usePathDiameter) {
        goal.x += PATHFIND_CELL_SIZE_F / 2;
        goal.y += PATHFIND_CELL_SIZE_F / 2;
    }
    if (TheTerrainLogic->getLayerForDestination(&start) != LAYER_GROUND ||
        TheTerrainLogic->getLayerForDestination(&goal) != LAYER_GROUND) return false;
    ICoord2D first, last;
    world_.worldToCell(&start, &first); world_.worldToCell(&goal, &last);
    if (query.isHuman && world_.checkCellOutsideExtents(first)) return false;
    const auto* firstCell = world_.getCell(LAYER_GROUND, first.x, first.y);
    const auto* lastCell = world_.getCell(LAYER_GROUND, last.x, last.y);
    // Invalid starts may tunnel out; leave that policy to the exact search.
    if (!firstCell || !lastCell || firstCell->getType() != PathfindCell::CELL_CLEAR ||
        lastCell->getType() != PathfindCell::CELL_CLEAR) return false;
    if (firstCell->getPinched()) return false;
    const int above=std::max(1,query.radius+(query.centerInCell?1:0));
    for (int y=first.y-query.radius;y<first.y+above;++y)
        for (int x=first.x-query.radius;x<first.x+above;++x)
            if (!world_.validMovementPosition(query.crusher,query.acceptableSurfaces,
                world_.getCell(LAYER_GROUND,x,y))) return false;
    TCheckMovementInfo startMovement{};
    startMovement.cell=first; startMovement.layer=LAYER_GROUND;
    startMovement.radius=query.radius; startMovement.centerInCell=query.centerInCell;
    startMovement.considerTransient=query.considerTransient;
    const auto startContext=MovementValidator::prepare(query.object,query.radius,query.centerInCell);
    if (!movement_.check(startContext,startMovement) || startMovement.enemyFixed) return false;
    const auto& extent = world_.m_extent;
    if (state_->reachabilityOriginX != extent.lo.x || state_->reachabilityOriginY != extent.lo.y) {
        for (auto& cache:state_->reachability) cache.invalidate();
        state_->reachabilityOriginX = extent.lo.x; state_->reachabilityOriginY = extent.lo.y;
    }
    auto& reachability=state_->reachability[query.isHuman?1:0];
    const auto& logical=world_.m_logicalExtent;
    if (query.isHuman && (!state_->logicalExtentValid ||
        state_->logicalExtent.lo.x!=logical.lo.x || state_->logicalExtent.lo.y!=logical.lo.y ||
        state_->logicalExtent.hi.x!=logical.hi.x || state_->logicalExtent.hi.y!=logical.hi.y)) {
        reachability.invalidate();
        state_->logicalExtent=logical;
        state_->logicalExtentValid=true;
    }
    // Ordinary DX9 movement tests anchor terrain, not the full traffic
    // footprint. A disconnection proof must use that same optimistic terrain.
    const int radius = 0;
    reachability.prepare(extent.hi.x - extent.lo.x + 1,
        extent.hi.y - extent.lo.y + 1, radius, radius ? query.centerInCell : true, [&](Int x, Int y) {
            const auto* cell = world_.getCell(LAYER_GROUND, x + extent.lo.x, y + extent.lo.y);
            if (!cell) return ReachabilityCell{};
            const auto layer = cell->getConnectLayer();
            // Treat every obstacle, rubble cell and bridge portal as traversable.
            // Ignored obstacles, fences, and bridge transitions therefore
            // cannot cause a false rejection in this optimistic graph.
            const bool passable = cell->getType() == PathfindCell::CELL_CLEAR ||
                                  cell->getType() == PathfindCell::CELL_OBSTACLE ||
                                  cell->getType() == PathfindCell::CELL_RUBBLE;
            return ReachabilityCell{passable, static_cast<std::uint8_t>(
                layer > LAYER_GROUND && layer <= LAYER_LAST ? int(layer) + 1 : 0)};
        },[&](Int x,Int y) {
            ICoord2D anchor{x+extent.lo.x,y+extent.lo.y};
            return !query.isHuman || !world_.checkCellOutsideExtents(anchor);
        });
    return !reachability.connected(first.x - extent.lo.x, first.y - extent.lo.y,
        last.x - extent.lo.x, last.y - extent.lo.y);
}
Bool GroundRoutePlanner::goalIsEnclosed(const Pathfinder::GroundRouteQuery& query,
    const Coord3D* from, const Coord3D* to,std::vector<std::array<int,2>>* enclosedCells)
{
    if (!world_.m_isMapReady || !from || !to || query.usePathDiameter ||
        (query.acceptableSurfaces != LOCOMOTORSURFACE_GROUND &&
         query.acceptableSurfaces != (LOCOMOTORSURFACE_GROUND|LOCOMOTORSURFACE_RUBBLE)) ||
        (query.object && query.object->getLayer() != LAYER_GROUND)) return false;
    Coord3D start = *from, goal = *to;
    ICoord2D originalStart;
    if (world_.worldToCell(from, &originalStart)) return false;
    world_.clip(&start, &goal);
    if (!query.centerInCell && !query.usePathDiameter) {
        goal.x += PATHFIND_CELL_SIZE_F / 2;
        goal.y += PATHFIND_CELL_SIZE_F / 2;
    }
    if (TheTerrainLogic->getLayerForDestination(&start) != LAYER_GROUND ||
        TheTerrainLogic->getLayerForDestination(&goal) != LAYER_GROUND) return false;
    ICoord2D first, last;
    world_.worldToCell(&start, &first); world_.worldToCell(&goal, &last);
    auto* startCell = world_.getCell(LAYER_GROUND, first.x, first.y);
    if (!startCell || startCell->getType() == PathfindCell::CELL_OBSTACLE ||
        !world_.validMovementPosition(query.crusher, query.acceptableSurfaces, startCell)) return false;
    const auto context = MovementValidator::prepare(query.object, query.radius, query.centerInCell);
    TCheckMovementInfo movement{};
    movement.layer = LAYER_GROUND; movement.radius = query.radius;
    movement.centerInCell = query.centerInCell;
    movement.acceptableSurfaces = query.acceptableSurfaces;
    movement.considerTransient = query.considerTransient;
    const auto& logical=world_.m_logicalExtent;
    const WeightedCellQuery classification{
        {std::uint32_t(query.acceptableSurfaces),std::uint32_t(world_.m_ignoreObstacleID),query.crusher!=0},
        query.radius,std::max(1,context.cellsAbove),
        logical.lo.x,logical.lo.y,logical.hi.x,logical.hi.y,
        query.isHuman!=0,false,query.object && query.object->isKindOf(KINDOF_DOZER)};
    const auto passable=[&](Int x,Int y) {
        movement.cell={x,y};
        OccupancyResult traffic;
        return classifyWeightedCell(classification,x,y,traffic,[&](int xx,int yy) {
            const auto* cell=world_.getCell(LAYER_GROUND,xx,yy);
            if (!cell) return WeightedTerrainCell{};
            bool dozerPassage=false;
            if (classification.dozer && cell->getType()==PathfindCell::CELL_OBSTACLE) {
                const auto* obstacle=TheGameLogic->findObjectByID(cell->getObstacleID());
                dozerPassage=obstacle && query.object->getRelationship(obstacle)!=ENEMIES;
            }
            return WeightedTerrainCell{{static_cast<TerrainKind>(cell->getType()),
                std::uint32_t(cell->getObstacleID()),true,cell->isObstacleFence()!=0},
                cell->getPinched()!=0,dozerPassage};
        },[&](OccupancyResult& result) {
            const bool allowed=movement_.check(context,movement)!=0;
            result={movement.allyFixedCount,movement.enemyFixed!=0,movement.allyMoving!=0,movement.allyGoal!=0};
            return allowed;
        });
    };
    // A blocked footprint can escape under a different movement policy.
    // Only prove failure when the start is legal in this exact graph.
    if (!passable(first.x,first.y)) return false;
    Int examined = 0;
    const bool enclosed = localGoalIsEnclosed(first.x, first.y, last.x, last.y, [&](Int x, Int y) {
        ++examined;
        auto* cell = world_.getCell(LAYER_GROUND, x, y);
        if (!cell) return ReachabilityCell{};
        if (cell->getConnectLayer() != LAYER_INVALID) return ReachabilityCell{true, 1};
        return ReachabilityCell{passable(x,y), 0};
    },[&](Int fromX,Int fromY,Int toX,Int toY) {
        const auto* target=world_.getCell(LAYER_GROUND,toX,toY);
        // A goal-directed phase line can cross a diagonal without checking
        // its side anchors. It cannot enter pinched cells or cliffs.
        if (target && !target->getPinched() && target->getType()!=PathfindCell::CELL_CLIFF) return true;
        // Ordinary DX9 expansion checks side terrain before side occupancy.
        // Keep that distinction in this optimistic reverse connectivity proof.
        return world_.validMovementPosition(query.crusher,query.acceptableSurfaces,
                   world_.getCell(LAYER_GROUND,fromX,toY)) ||
               world_.validMovementPosition(query.crusher,query.acceptableSurfaces,
                   world_.getCell(LAYER_GROUND,toX,fromY));
    },enclosedCells);
    world_.recordNavigationWork(examined);
    return enclosed;
}
bool GroundRoutePlanner::hasPending(ObjectID id) const {
    if (id==INVALID_ID) return state_->standalone && !state_->standalone->obsolete();
    const auto found=state_->pending.find(id);
    if (found==state_->pending.end()) return false;
    if (!found->second.obsolete()) return true;
    if (!state_->completedSlice) return false;
    const auto* object=TheGameLogic?TheGameLogic->findObjectByID(id):nullptr;
    return object && object==found->second.mover && !object->isDestroyed() &&
        object->getAIUpdateInterface() &&
        object->getAIUpdateInterface()->getPathRequestRevision()!=found->second.requestRevision;
}
bool GroundRoutePlanner::pendingUsesFallback(ObjectID id) const {
    const auto found=state_->pending.find(id);
    return found!=state_->pending.end() && !found->second.obsolete() &&
        found->second.slice && found->second.slice->useFallback;
}
void GroundRoutePlanner::discardStalePending() {
    // A completed slice still owns references into its continuation until
    // accounting is committed. Retire obsolete requests after that boundary.
    if (state_->completedSlice) return;
    std::erase_if(state_->pending,[](const auto& entry) { return entry.second.obsolete(); });
    if (state_->standalone && state_->standalone->obsolete()) state_->standalone.reset();
}
void GroundRoutePlanner::cancelRequest(ObjectID id) {
    // Retirement is legal only after the dispatch accounting boundary.
    assert(!state_->completedSlice);
    state_->pending.erase(id);
}
void GroundRoutePlanner::discardUnqueuedRequests() {
    assert(!state_->completedSlice);
    // At the gameplay queue boundary only queued AI orders have a consumer.
    // An abandoned direct query must not retain admission precedence over them.
    std::erase_if(state_->pending,[&](const auto& entry) {
        return !world_.m_pathRequests->contains(unsigned(entry.first));
    });
}
void GroundRoutePlanner::commitCapturedSlice() {
    stats_.lastSliceNanoseconds=0;
    stats_.lastCommitNanoseconds=0;
    if (state_->completedSlice) {
        const auto began=std::chrono::steady_clock::now();
        const auto& item=state_->completedSlice;
        stats_.lastSliceNanoseconds=item->elapsedNanoseconds;
        world_.recordNavigationWork(static_cast<Int>(std::min<std::uint64_t>(
            item->workCount,std::numeric_limits<Int>::max())));
        if (item->elapsedNanoseconds>stats_.maximumSliceNanoseconds) {
            stats_.maximumSliceNanoseconds=item->elapsedNanoseconds;
            stats_.maximumSliceObject=item->moverId;
            stats_.maximumSliceWork=item->workCount;
            stats_.maximumSliceStartX=item->startCellX;
            stats_.maximumSliceStartY=item->startCellY;
            stats_.maximumSliceGoalX=item->goalCellX;
            stats_.maximumSliceGoalY=item->goalCellY;
        }
        item->work={};
        state_->completedSlice.reset();
        stats_.lastCommitNanoseconds=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-began).count());
    }
    discardStalePending();
}
bool GroundRoutePlanner::canAdmitRequest(bool playerCommand) const {
    std::size_t ordinary=0,player=0;
    for (const auto& [id,task]:state_->pending) {
        if (task.obsolete()) continue;
        if (task.slice && task.slice->playerCommand) ++player;
        else ++ordinary;
    }
    return playerCommand ? player==0 : ordinary<State::OrdinaryCapturedSearches;
}
void GroundRoutePlanner::preemptOlderPlayerRequest(unsigned frame,std::uint64_t sequence) {
    // Return an interrupted foreground request to ordinary admission. Its AI
    // still owns the order and its original FIFO entry remains queued. It can
    // be interrupted only once: ordinary continuations are never preempted.
    // This preserves bounded workspace use and eventual background progress.
    if (state_->completedSlice) return;
    std::erase_if(state_->pending,[&](const auto& entry) {
        const auto& slice=entry.second.slice;
        return slice && slice->playerCommand &&
            std::pair(slice->playerCommandFrame,slice->playerCommandSequence)<std::pair(frame,sequence);
    });
}
bool GroundRoutePlanner::isPlayerCommand(ObjectID id) const {
    const auto found=state_->pending.find(id);
    return found!=state_->pending.end() && found->second.slice && found->second.slice->playerCommand;
}
std::vector<ObjectID> GroundRoutePlanner::pollableRequests() const {
    std::vector<std::tuple<bool,std::uint64_t,ObjectID>> ordered;
    for (const auto& [id,task]:state_->pending) {
        if (task.obsolete() || (task.slice && task.slice->work) || !task.canStart()) continue;
        ordered.emplace_back(!(task.slice && task.slice->playerCommand),task.slice?task.slice->sequence:0,id);
    }
    std::sort(ordered.begin(),ordered.end());
    std::vector<ObjectID> result;
    for (const auto& [ordinary,sequence,id]:ordered) result.push_back(id);
    return result;
}
bool GroundRoutePlanner::advanceCapturedSlice() {
    // Execute one captured continuation on this thread. Commit its accounting
    // separately so polling cannot consume the same slice twice.
    if (state_->completedSlice) return false;
    discardStalePending();
    std::shared_ptr<CapturedSlice> firstReady,nextReady,playerReady;
    const auto consider=[&](const WeightedTask& task) {
        const auto& item=task.slice;
        if (!item || !item->work) return;
        if (item->playerCommand) {
            if (!playerReady || item->sequence<playerReady->sequence) playerReady=item;
            return;
        }
        if (!firstReady || item->sequence<firstReady->sequence) firstReady=item;
        if (item->sequence>state_->lastDispatchedSlice && item->sequence<=state_->dispatchCycleEnd &&
            (!nextReady || item->sequence<nextReady->sequence)) nextReady=item;
    };
    for (const auto& [id,task]:state_->pending) consider(task);
    if (state_->standalone) consider(*state_->standalone);
    if (!nextReady && firstReady) {
        // Freeze the arrival set for each fair cycle; new requests cannot
        // continually postpone the next slice of an existing search.
        state_->dispatchCycleEnd=state_->sliceSequence;
        nextReady=std::move(firstReady);
    }
    // Explicit commands get prompt slices, while at least every fourth slice
    // remains available to older/background work, even under continuous input.
    if (playerReady && (!nextReady || state_->consecutivePlayerSlices<3)) {
        nextReady=std::move(playerReady);
        ++state_->consecutivePlayerSlices;
    } else if (nextReady) state_->consecutivePlayerSlices=0;
    if (!nextReady) return false;
    state_->completedSlice=std::move(nextReady);
    auto sliceTiming=diagnostics::frameCapture().measure("Navigation.Capture.Advance",
        TheGameLogic?TheGameLogic->getFrame():0,unsigned(state_->completedSlice->moverId));
    const auto began=std::chrono::steady_clock::now();
    try {
        state_->completedSlice->work();
    } catch (...) {
        state_->completedSlice->work={};
        state_->completedSlice.reset();
        throw;
    }
    if (!state_->completedSlice->playerCommand)
        state_->lastDispatchedSlice=state_->completedSlice->sequence;
    state_->completedSlice->elapsedNanoseconds=static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-began).count());
    stats_.lastSliceNanoseconds=state_->completedSlice->elapsedNanoseconds;
    return true;
}
Path* GroundRoutePlanner::findWeighted(const Pathfinder::GroundRouteQuery& query,
    const Coord3D* from, const Coord3D* to, bool downhillOnly,
    const std::function<bool(int,int,PathfindLayerEnum)>& destination,
    const std::function<std::optional<double>(int,int,PathfindLayerEnum,unsigned)>& fallback,
    std::optional<ICoord2D> distanceOnlyOrigin, bool* usedFallback,bool deferred,
    const DestinationRankQuery* capturedRank,const DestinationQuery* capturedDestination,const FleeQuery* capturedFlee,
    const std::function<bool(int,int,PathfindLayerEnum)>* capturedDestinationPredicate,unsigned neighborLimit)
{
    if (usedFallback) *usedFallback=false;
    auto queryTiming=diagnostics::frameCapture().measure("Navigation.Request.Poll",
        TheGameLogic?TheGameLogic->getFrame():0,query.object?unsigned(query.object->getID()):0);
    if (!from || !to) return nullptr;
    if (!world_.groundQueriesDeferred()) deferred=false;
    if (deferred) {
		constexpr unsigned slice=10000;
        // Prove enclosure only when admitting a fresh request. A continuation
        // owns its captured inputs; polling must not stall it because the live
        // destination changed while its last slice awaits commit.
        const auto enclosedAtAdmission=[&] {
            return !destination && !fallback && !query.usePathDiameter &&
                goalIsEnclosed(query,from,to);
        };
        // Refresh object relationships for each new batch. Cell mutations
        // have their own journal, including edits within the same logic tick;
        // retain that journal and the preceding immutable occupancy baseline.
        if (!state_->completedSlice && state_->pending.empty() &&
            !state_->standalone) {
            state_->capturedOccupants.clear();
        }
        const auto makeCapturedSlice=[&]() -> std::shared_ptr<CapturedSlice> {
            // Native destination/rank callbacks are owner-thread policies. A
            // slice is safe when the request is ordinary, or when its policy
            // has an immutable captured equivalent.
            if ((destination || fallback) &&
                !(capturedFlee || (capturedRank && !destination) || capturedDestinationPredicate)) return {};
            auto slice=std::make_shared<CapturedSlice>();
            slice->playerCommand=world_.m_priorityGroundQuery;
            if (slice->playerCommand && query.object && query.object->getAIUpdateInterface()) {
                const auto* ai=query.object->getAIUpdateInterface();
                slice->playerCommandFrame=ai->getPlayerPathCommandFrame();
                slice->playerCommandSequence=ai->getPlayerPathCommandSequence();
            }
            slice->query=query;
            slice->moverId=query.object?query.object->getID():INVALID_ID;
            slice->downhillOnly=downhillOnly;
            if (capturedDestination) slice->destination=*capturedDestination;
            if (capturedDestinationPredicate)
                slice->destinationPredicate=*capturedDestinationPredicate;
            if (capturedFlee) {
                slice->flee.emplace(*capturedFlee,query.centerInCell!=0);
                slice->useFallback=true;
            } else if (capturedRank) {
                slice->rank=*capturedRank;
                slice->useFallback=true;
            }
            return slice;
        };
        if (!query.object) {
            if (!state_->standalone) {
                if (enclosedAtAdmission()) return nullptr;
                state_->standalone.emplace(weightedTask(query,*from,*to,downhillOnly,
                    destination,fallback,distanceOnlyOrigin,slice,makeCapturedSlice(),neighborLimit));
            }
            auto& task=*state_->standalone;
            if (!task.canStart()) return nullptr;
            if (task.slice && !task.started) {
                prepareCapturedSnapshot(query,downhillOnly,true);
                captureDynamicSnapshot();
                prepareCapturedOccupant(query);
            }
            task.resume();
            if (!task.done()) return nullptr;
            auto result=task.take();
            state_->standalone.reset();
            if (!result.path && !result.waypoints.empty()) {
                auto* materialized=materializeCapturedResult(query,std::move(result));
                result.path.reset(materialized);
            }
            if (result.staleTerrain) {
                state_->standalone.emplace(weightedTask(query,*from,*to,downhillOnly,
                    destination,fallback,distanceOnlyOrigin,slice,makeCapturedSlice(),neighborLimit));
                return nullptr;
            }
            if (usedFallback) *usedFallback=result.fallback;
            return result.path.release();
        }
        const auto id=query.object->getID();
        auto found=state_->pending.find(id);
        if (found!=state_->pending.end() && found->second.obsolete()) {
            if (state_->completedSlice) {
                return nullptr;
            }
            state_->pending.erase(found);
            found=state_->pending.end();
        }
        if (found==state_->pending.end()) {
            if (enclosedAtAdmission()) return nullptr;
            state_->pending.emplace(id,weightedTask(query,*from,*to,downhillOnly,
                destination,fallback,distanceOnlyOrigin,slice,makeCapturedSlice(),neighborLimit));
            return nullptr;
        }
        auto& task=found->second;
        if (!task.canStart()) return nullptr;
        if (task.slice && !task.started) {
            prepareCapturedSnapshot(task.slice->query,task.slice->downhillOnly,true);
            captureDynamicSnapshot();
            prepareCapturedOccupant(task.slice->query);
        }
        {
            auto resumeTiming=diagnostics::frameCapture().measure("Navigation.Request.Resume",
                TheGameLogic?TheGameLogic->getFrame():0,unsigned(id));
            task.resume();
        }
        if (!task.done()) return nullptr;
        auto result=task.take();
        {
            auto cleanupTiming=diagnostics::frameCapture().measure("Navigation.Capture.Retire",
                TheGameLogic?TheGameLogic->getFrame():0,unsigned(id));
            state_->pending.erase(found);
        }
        if (!result.path && !result.waypoints.empty()) {
            auto* materialized=materializeCapturedResult(query,std::move(result));
            result.path.reset(materialized);
        }
        if (result.staleTerrain) {
            state_->pending.emplace(id,weightedTask(query,*from,*to,downhillOnly,
                destination,fallback,distanceOnlyOrigin,slice,makeCapturedSlice(),neighborLimit));
            return nullptr;
        }
        if (result.path && result.fallback && fallback) {
            const auto* end=result.path->getLastNode();
            ICoord2D cell;
            world_.worldToCell(end->getPosition(),&cell);
            if (!fallback(cell.x,cell.y,end->getLayer(),0)) {
                // A first-observed destination may have been reserved while
                // this query slept. Replan instead of publishing a stale goal.
                state_->pending.emplace(id,weightedTask(query,*from,*to,downhillOnly,
                    destination,fallback,distanceOnlyOrigin,slice,makeCapturedSlice(),neighborLimit));
                return nullptr;
            }
        }
        if (usedFallback) *usedFallback=result.fallback;
        return result.path.release();
    }
    const int clearanceWidth=world_.m_extent.hi.x-world_.m_extent.lo.x+1;
    const int clearanceHeight=world_.m_extent.hi.y-world_.m_extent.lo.y+1;
    const auto queryStart=std::chrono::steady_clock::now();
    const auto initialWork=world_.m_cumulativeCellsAllocated;
    const auto initialHierarchySearches=state_->hierarchySearches;
    const auto initialHierarchyRefinements=state_->hierarchyRefinements;
    auto clearanceScope=world_.m_groundClearanceMemo->begin(clearanceWidth,clearanceHeight);
    auto task=weightedTask(query,*from,*to,downhillOnly,destination,fallback,distanceOnlyOrigin,0,{},neighborLimit);
    while (task.resume()) {}
    auto result=task.take();
    if (usedFallback) *usedFallback=result.fallback;
    if (result.path) smooth(result.path.get(),query);
    diagnostics::frameCapture().query(TheGameLogic?TheGameLogic->getFrame():0,
        query.object?unsigned(query.object->getID()):0,
        unsigned(world_.m_cumulativeCellsAllocated-initialWork),from->x,from->y,to->x,to->y,
        query.usePathDiameter?query.pathDiameter:0,bool(fallback),bool(destination),bool(result.path),
        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-queryStart).count(),
        query.object?query.object->getTemplate()->getName().str():"none",
        unsigned(query.acceptableSurfaces),query.radius,query.centerInCell!=0,
        state_->hierarchySearches-initialHierarchySearches,state_->hierarchyRefinements-initialHierarchyRefinements);
    return result.path.release();
}

GroundRoutePlanner::WeightedTask GroundRoutePlanner::weightedTask(
    Pathfinder::GroundRouteQuery query,Coord3D fromValue,Coord3D toValue,bool downhillOnly,
    std::function<bool(int,int,PathfindLayerEnum)> destination,
    std::function<std::optional<double>(int,int,PathfindLayerEnum,unsigned)> fallback,
    std::optional<ICoord2D> distanceOnlyOrigin,unsigned sliceBudget,std::shared_ptr<CapturedSlice> slice,unsigned neighborLimit)
{
    const auto ignored=world_.m_ignoreObstacleID;
    const auto* ai=query.object?query.object->getAIUpdateInterface():nullptr;
    if (slice) {
        slice->sequence=++state_->sliceSequence;
        Coord3D clippedFrom=fromValue,clippedGoal=toValue;
        world_.clip(&clippedFrom,&clippedGoal);
        if (!query.centerInCell && !query.usePathDiameter) {
            clippedGoal.x+=PATHFIND_CELL_SIZE_F/2;
            clippedGoal.y+=PATHFIND_CELL_SIZE_F/2;
        }
        ICoord2D goalCell{};
        world_.worldToCell(&clippedGoal,&goalCell);
        ICoord2D startCell{};
        world_.worldToCell(&clippedFrom,&startCell);
        slice->startCellX=startCell.x;
        slice->startCellY=startCell.y;
        slice->goalCellX=goalCell.x;
        slice->goalCellY=goalCell.y;
        slice->startLayer=query.object ? static_cast<unsigned>(query.object->getLayer()) : LAYER_GROUND;
        slice->goalLayer=static_cast<unsigned>(TheTerrainLogic->getLayerForDestination(&clippedGoal));
        slice->worldEpoch=state_->worldEpoch;
        slice->retainAcrossRegionalEdits=!destination && !fallback && !downhillOnly &&
            !query.usePathDiameter && !neighborLimit;
    }
    return {weightedContinuation(query,fromValue,toValue,downhillOnly,std::move(destination),
        std::move(fallback),distanceOnlyOrigin,sliceBudget?sliceBudget:CellsPerFrame,slice,neighborLimit),&world_,ignored,state_.get(),state_->epoch,
        query.object,query.object?query.object->getID():INVALID_ID,
        ai?ai->getPathRequestRevision():0,false,false,std::move(slice)};
}

Continuation<GroundRoutePlanner::WeightedResult> GroundRoutePlanner::weightedContinuation(
    Pathfinder::GroundRouteQuery query,Coord3D fromValue,Coord3D toValue,bool downhillOnly,
    std::function<bool(int,int,PathfindLayerEnum)> destination,
    std::function<std::optional<double>(int,int,PathfindLayerEnum,unsigned)> fallback,
    std::optional<ICoord2D> distanceOnlyOrigin,unsigned sliceBudget,std::shared_ptr<CapturedSlice> slice,unsigned neighborLimit)
{
    unsigned remaining=sliceBudget;
    struct PhaseCounts {
        bool enabled;
        unsigned frame;
        std::uint64_t rays=0,samples=0;
        ~PhaseCounts() {
            if (!enabled) return;
            auto& capture=diagnostics::frameCapture();
            capture.counter("Navigation.Phase.Rays",frame,rays);
            capture.counter("Navigation.Phase.Samples",frame,samples);
        }
    } phaseCounts{diagnostics::frameCapture().detailsEnabled(),TheGameLogic?TheGameLogic->getFrame():0};
    const auto* from=&fromValue;
    const auto* to=&toValue;
    if (!world_.m_isMapReady || query.radius<0) co_return WeightedResult{};
    // Both captured and live searches execute on the simulation thread. The
    // coroutine exclusively leases its scratch until completion/cancellation;
    // the next query can then reuse its capacity with fresh generations.
    State::Lease ownerLease(*state_,slice!=nullptr,slice && slice->playerCommand);
    State::Scratch* scratch=ownerLease.value.get();
    auto recordWork=[&](std::uint64_t amount) {
        // This continuation runs on the simulation owner. Charge preparation
        // and reconstruction here; captured expansion is charged exactly once
        // by commitCapturedSlice from the dispatch's work delta.
        world_.recordNavigationWork(static_cast<Int>(std::min<std::uint64_t>(
            amount,std::numeric_limits<Int>::max())));
    };
    if (!slice && !destination && !fallback && !query.usePathDiameter &&
        definitelyDisconnected(query,from,to)) co_return WeightedResult{};
    Coord3D startPosition=*from, goalPosition=*to;
    world_.clip(&startPosition,&goalPosition);
    if (!query.centerInCell && !query.usePathDiameter) {
        goalPosition.x+=PATHFIND_CELL_SIZE_F/2;
        goalPosition.y+=PATHFIND_CELL_SIZE_F/2;
    }
    ICoord2D start,goal;
    world_.worldToCell(&startPosition,&start);
    world_.worldToCell(&goalPosition,&goal);
    const auto startLayer=query.object ? query.object->getLayer() : LAYER_GROUND;
    const auto goalLayer=destination?startLayer:TheTerrainLogic->getLayerForDestination(&goalPosition);
    const int width=world_.m_extent.hi.x-world_.m_extent.lo.x+1;
    const int height=world_.m_extent.hi.y-world_.m_extent.lo.y+1;
    const auto cellCount=std::uint64_t(width)*height;
    if (width<=0 || height<=0) co_return WeightedResult{};
    std::array<int,LAYER_LAST+1> layerIndices;
    layerIndices.fill(-1);
    std::vector<PathfindLayerEnum> layers;
    for (int layer=LAYER_GROUND;layer<=LAYER_LAST;++layer) {
        if (layer!=LAYER_GROUND && world_.m_layers[layer].isUnused()) continue;
        layerIndices[layer]=static_cast<int>(layers.size());
        layers.push_back(static_cast<PathfindLayerEnum>(layer));
    }
    if (startLayer<LAYER_GROUND || startLayer>LAYER_LAST ||
        goalLayer<LAYER_GROUND || goalLayer>LAYER_LAST ||
        layerIndices[startLayer]<0 || layerIndices[goalLayer]<0) co_return WeightedResult{};
    const auto context=MovementValidator::prepare(query.object,query.radius,query.centerInCell);
    const auto* moverAI=query.object?query.object->getAIUpdateInterface():nullptr;
    const bool markAlliedBlockers=!moverAI || !moverAI->canPathThroughUnits();
    const auto& logical=world_.m_logicalExtent;
    const WeightedCellQuery classification{
        {std::uint32_t(query.acceptableSurfaces),std::uint32_t(world_.m_ignoreObstacleID),query.crusher!=0},
        query.radius,std::max(1,context.cellsAbove),
        logical.lo.x,logical.lo.y,logical.hi.x,logical.hi.y,
        query.isHuman!=0,query.usePathDiameter!=0,query.object && query.object->isKindOf(KINDOF_DOZER)};
    std::unique_ptr<CapturedWeightedGraph> captured;
    if (slice) {
        if (!world_.getCell(startLayer,start.x,start.y)) co_return WeightedResult{};
        // The owner prepared the immutable terrain, hierarchy, heights, and
        // occupant table before submitting this slice. Reading a missing
        // entry here is a failed batch preparation, never a reason to touch
        // live game state or select a second route implementation.
        if (state_->capturedLayers.empty() || state_->capturedEpoch!=state_->epoch)
            co_return WeightedResult{};
        if (slice->retainAcrossRegionalEdits) {
            slice->capturedTerrain=state_->capturedLayers;
            slice->terrainEpoch=state_->capturedEpoch;
        }
        CapturedGraphQuery input;
        input.movement=classification;
        input.occupancy={std::uint32_t(context.objectId),std::uint32_t(context.ignoredId),0,0,
            context.radius,context.cellsAbove,query.considerTransient!=0,false};
#ifdef INFANTRY_MOVES_THROUGH_INFANTRY
        input.occupancy.infantryPassThrough=context.infantry!=0;
#endif
        input.startX=start.x;input.startY=start.y;input.goalX=goal.x;input.goalY=goal.y;
        input.startLayer=startLayer;input.goalLayer=goalLayer;input.pathDiameter=query.pathDiameter;
        input.checkOccupants=context.object!=nullptr;input.downhillOnly=downhillOnly;
        input.fallback=slice->useFallback;
        // HPA supplies a lower bound, not a hard route restriction. A single
        // coarse corridor may omit a legal footprint/occupancy detour.
        input.cacheClearance=world_.m_groundClearanceMemo->enabled;
        input.clearanceEvaluations=&state_->clearanceEvaluations;
        input.clearanceHits=&state_->clearanceHits;
        // HPA* contributes an admissible lower bound for every ground search,
        // including fixed-goal requests. The captured fine graph remains
        // authoritative for footprint, occupancy, and endpoint policy.
        // The abstract route is a lower-bound guide even when the fine search
        // may terminate at a policy-selected endpoint. It never decides
        // reachability or publication.
        input.useHpa=true;
        // Standalone requests have no mover. They still use the captured
        // slice graph, but must not dereference a native object while the
        // batch is being prepared.
        const auto sourceTeam=query.object ? reinterpret_cast<std::uintptr_t>(query.object->getTeam()) : 0;
        const auto sourceCrusher=query.object ? static_cast<unsigned>(query.object->getCrusherLevel()) : 0;
        const auto sourceDefector=query.object && query.object->getIsUndetectedDefector()!=0;
        const auto sourceUnmanned=query.object && query.object->isDisabledByType(DISABLED_UNMANNED)!=0;
        const auto sourceDozer=query.object && query.object->isKindOf(KINDOF_DOZER)!=0;
        const State::OccupantKey occupantKey{sourceTeam,sourceCrusher,sourceDefector,sourceUnmanned,sourceDozer};
        auto occupant=state_->capturedOccupants.find(occupantKey);
        if (occupant==state_->capturedOccupants.end()) co_return WeightedResult{};
        {
            auto graphTiming=diagnostics::frameCapture().measure("Navigation.Capture.Graph",
                TheGameLogic?TheGameLogic->getFrame():0,unsigned(slice->moverId));
            captured=std::make_unique<CapturedWeightedGraph>(state_->capturedLayers,occupant->second,input,
                state_->capturedDynamicOccupancy);
        }
        // An exact route cannot leave the static component reachable from its
        // captured start.  Rejecting a statically disconnected goal before
        // fine-grid expansion avoids a full-map negative search. Closest-goal
        // requests deliberately remain in the component and continue to rank
        // the best legal endpoint.
        if (!slice->useFallback && !captured->inStaticStartComponent(
                goal.x,goal.y,goalLayer))
            co_return WeightedResult{};
        // Do not rebuild a dynamic HPA hierarchy here. That proof is retained
        // for explicit owner-thread queries, but repeating it for every
        // slice would turn a shared batch into N map-wide classifications.
        // The captured weighted search below is the authoritative result.
        // A flee request with no legal destination is a deterministic negative
        // result. Prove that from the immutable snapshot before constructing a
        // weighted frontier; otherwise a fully congested map needlessly pays a
        // near-complete Dijkstra traversal on every retry.
        if (slice->flee && !slice->flee->hasAnyDestination(*captured,slice->destination))
            co_return WeightedResult{};
        if (slice->useFallback && !slice->flee && !slice->rank.allowReservedExact &&
            !captured->anyDestination(slice->destination,[&](int x,int y,unsigned layer) {
                return captured->classify(x,y,layer,nullptr);
            }))
            co_return WeightedResult{};
    }
    const auto dozerPassage=[&](const PathfindCell* cell) {
        if (!classification.dozer || cell->getType()!=PathfindCell::CELL_OBSTACLE) return false;
        const auto* obstacle=TheGameLogic->findObjectByID(cell->getObstacleID());
        return obstacle && query.object->getRelationship(obstacle)!=ENEMIES;
    };
    std::span<const std::uint8_t> terrainFootprint;
    // Only ordinary ground terrain can use the exact cached footprint result.
    // Object-specific obstacle exceptions and bridge layers retain their full
    // native checks. Occupancy is always evaluated for this query.
    const bool useTerrainFootprint=!slice && !classification.corridor &&
        !classification.dozer && !query.crusher && world_.m_ignoreObstacleID==INVALID_ID;
    auto classify=[&](int x,int y,PathfindLayerEnum layer,OccupancyResult* occupancy) {
        TCheckMovementInfo movement{};
        movement.cell={x,y}; movement.layer=layer; movement.radius=query.radius;
        movement.centerInCell=query.centerInCell; movement.acceptableSurfaces=query.acceptableSurfaces;
        movement.considerTransient=query.considerTransient;
        OccupancyResult traffic;
        if (useTerrainFootprint && layer==LAYER_GROUND) {
            const auto& extent=world_.m_extent;
            if (x<extent.lo.x || y<extent.lo.y || x>extent.hi.x || y>extent.hi.y ||
                (classification.restrictToBounds && (x<logical.lo.x || y<logical.lo.y ||
                    x>logical.hi.x || y>logical.hi.y))) return false;
            if (terrainFootprint.empty()) {
                auto terrainQuery=query;terrainQuery.radius=0;terrainQuery.centerInCell=true;
                prepareLiveHierarchy(terrainQuery,&terrainFootprint);
            }
            if (!terrainFootprint[std::size_t(y-extent.lo.y)*width+x-extent.lo.x]) return false;
            if (!movement_.check(context,movement) || movement.enemyFixed) return false;
            if (occupancy) *occupancy={movement.allyFixedCount,false,movement.allyMoving!=0,movement.allyGoal!=0};
            return true;
        }
        const bool legal=classifyWeightedCell(classification,x,y,traffic,[&](int xx,int yy) {
            const auto* cell=world_.getCell(layer,xx,yy);
            if (!cell) return WeightedTerrainCell{};
            return WeightedTerrainCell{{static_cast<TerrainKind>(cell->getType()),
                std::uint32_t(cell->getObstacleID()),true,cell->isObstacleFence()!=0},cell->getPinched()!=0,dozerPassage(cell)};
        },[&](OccupancyResult& result) {
            const bool allowed=movement_.check(context,movement)!=0;
            result={movement.allyFixedCount,movement.enemyFixed!=0,movement.allyMoving!=0,movement.allyGoal!=0};
            return allowed;
        });
        if (!legal) return false;
        if (occupancy && !query.usePathDiameter) *occupancy=traffic;
        return true;
    };
    std::shared_ptr<const HpaClusterGraph> hierarchyGraph;
    std::vector<std::uint8_t> hierarchyCorridor;
    unsigned corridorPadding=1;
    bool restrictCorridor=false;
    if (!captured) {
        scratch->cells.begin(static_cast<std::size_t>(cellCount)*layers.size());
        scratch->groundPhase.begin(static_cast<std::size_t>(cellCount));
    }
    auto sampleAt=[&](int x,int y,PathfindLayerEnum layer)->const State::CellSample* {
        if (layer<LAYER_GROUND || layer>LAYER_LAST || layerIndices[layer]<0 ||
            x<world_.m_extent.lo.x || x>world_.m_extent.hi.x ||
            y<world_.m_extent.lo.y || y>world_.m_extent.hi.y) return nullptr;
        const auto index=static_cast<std::size_t>(layerIndices[layer])*cellCount+
            static_cast<std::size_t>(y-world_.m_extent.lo.y)*width+x-world_.m_extent.lo.x;
        if (const auto* sample=scratch->cells.find(index)) return sample;
        const auto& sample=scratch->cells.get(index,[&] {
            State::CellSample value;
            value.passable=classify(x,y,layer,&value.occupancy);
            if (value.passable && !value.occupancy.enemyFixed && !value.occupancy.allyFixedCount) {
                const auto* cell=world_.getCell(layer,x,y);
                value.phasePassable=cell && !cell->getPinched() && cell->getType()!=PathfindCell::CELL_CLIFF;
            }
            return value;
        });
        return &sample;
    };
    auto allowed=[&](int x,int y,PathfindLayerEnum layer,OccupancyResult* occupancy=nullptr) {
        if (captured) return captured->classify(x,y,layer,occupancy);
        const auto* sample=sampleAt(x,y,layer);
        if (!sample || !sample->passable) return false;
        if (occupancy && !query.usePathDiameter) *occupancy=sample->occupancy;
        return true;
    };
    auto goalAllowed=[&](int x,int y) {
        return allowed(x,y,goalLayer) && (!query.usePathDiameter ||
            world_.clearCellForDiameter(query.crusher,x,y,goalLayer,query.pathDiameter)>=query.pathDiameter);
    };
    if (!slice && !destination && !fallback && !query.usePathDiameter &&
        startLayer==LAYER_GROUND && goalLayer==LAYER_GROUND &&
        (query.acceptableSurfaces==LOCOMOTORSURFACE_GROUND ||
         query.acceptableSurfaces==(LOCOMOTORSURFACE_GROUND|LOCOMOTORSURFACE_RUBBLE)) &&
        allowed(start.x,start.y,startLayer)) {
        LocalGoalProbe probe(start.x,start.y,goal.x,goal.y);
        while (!probe.complete()) {
            const auto used=probe.advance(remaining,[&](int x,int y) {
                const auto* cell=world_.getCell(LAYER_GROUND,x,y);
                if (!cell) return ReachabilityCell{};
                if (cell->getConnectLayer()!=LAYER_INVALID) return ReachabilityCell{true,1};
                return ReachabilityCell{allowed(x,y,LAYER_GROUND),0};
            },[&](int fromX,int fromY,int toX,int toY) {
                const auto* target=world_.getCell(LAYER_GROUND,toX,toY);
                if (target && !target->getPinched() && target->getType()!=PathfindCell::CELL_CLIFF) return true;
                return world_.validMovementPosition(query.crusher,query.acceptableSurfaces,
                           world_.getCell(LAYER_GROUND,fromX,toY)) ||
                       world_.validMovementPosition(query.crusher,query.acceptableSurfaces,
                           world_.getCell(LAYER_GROUND,toX,fromY));
            });
            recordWork(used);remaining-=used;
            if (!probe.complete() || remaining==0) {
                co_await std::suspend_always{};
                context.relationships={};remaining=sliceBudget;
            }
        }
        if (probe.enclosed()) co_return WeightedResult{};
    }
    if (query.usePathDiameter) {
        const auto projected=projectGroundDestination({goal.x,goal.y},goalAllowed);
        if (!projected) co_return WeightedResult{};
        goal={projected->x,projected->y};
    }
    const auto queryRank=[&](int x,int y,PathfindLayerEnum layer,unsigned cost)->std::optional<double> {
        if (captured && slice->flee) return slice->flee->rank(*captured,captured->encode(x,y,layer),slice->destination);
        if (captured) return captured->rank(captured->encode(x,y,layer),cost,slice->rank,slice->destination);
        return fallback(x,y,layer,cost);
    };
    std::optional<LocalGoalRegion<>> endpointRegion;
    if (!slice && fallback && distanceOnlyOrigin && !query.usePathDiameter &&
        startLayer==LAYER_GROUND && goalLayer==LAYER_GROUND && allowed(start.x,start.y,startLayer)) {
        endpointRegion.emplace(start.x,start.y,goal.x,goal.y);
        while (!endpointRegion->complete()) {
            const auto used=endpointRegion->advance(remaining,[&](int x,int y) {
                const auto* cell=world_.getCell(LAYER_GROUND,x,y);
                if (!cell) return ReachabilityCell{};
                return ReachabilityCell{allowed(x,y,LAYER_GROUND),
                    std::uint8_t(cell->getConnectLayer()!=LAYER_INVALID)};
            });
            recordWork(used);remaining-=used;
            if (!endpointRegion->complete() || remaining==0) {
                co_await std::suspend_always{};
                context.relationships={};remaining=sliceBudget;
            }
        }
    }
    const bool enclosedExactGoal=endpointRegion && !endpointRegion->potentiallyReachable(goal.x,goal.y);
    const bool exactGoalAllowed=!enclosedExactGoal && goalAllowed(goal.x,goal.y) &&
        (!fallback || (queryRank(goal.x,goal.y,goalLayer,0).has_value() &&
            !definitelyDisconnected(query,from,to)));
    if ((!destination && !fallback && !exactGoalAllowed) || !world_.getCell(startLayer,start.x,start.y)) co_return WeightedResult{};
    const bool captureRanks=fallback && distanceOnlyOrigin && !exactGoalAllowed;
    if (captureRanks) scratch->ranks.begin(static_cast<std::size_t>(cellCount)*layers.size());
    const auto observedRank=[&](int x,int y,PathfindLayerEnum layer,unsigned cost)->std::optional<double> {
        if (!captureRanks) return queryRank(x,y,layer,cost);
        const auto index=static_cast<std::size_t>(layerIndices[layer])*cellCount+
            static_cast<std::size_t>(y-world_.m_extent.lo.y)*width+x-world_.m_extent.lo.x;
        return scratch->ranks.get(index,[&] { return queryRank(x,y,layer,0); });
    };
    std::optional<double> provenMinimum;
    if (fallback && distanceOnlyOrigin && !exactGoalAllowed) {
        NearestGoalBound bound;
        const auto origin=*distanceOnlyOrigin;
        bound.begin(world_.m_extent.lo.x,world_.m_extent.lo.y,world_.m_extent.hi.x,world_.m_extent.hi.y,
            origin.x,origin.y,static_cast<unsigned>(layers.size()));
        const auto accept=[&](int x,int y,unsigned layerIndex) {
            const auto layer=layers[layerIndex];
            // getCell(bridge, x, y) deliberately returns the ground cell
            // outside that bridge. It is the same endpoint, not another
            // layer whose zero-distance rank can defeat the pocket proof.
            if (!captured && layer!=LAYER_GROUND) {
                const auto* cell=world_.getCell(layer,x,y);
                if (!cell || cell->getLayer()!=layer) return false;
            }
            // Endpoint reservations are usually the rejecting predicate on a
            // crowded map. Test that compact footprint before the more
            // general movement classification, then calculate the geometric
            // bound directly. Captured rank() would repeat the same
            // destination footprint scan and does not add information here.
            if (captured) {
                // A valid exact route can never leave the static component
                // reachable from its start. Restricting this lower-bound scan
                // to that component is therefore still a lower bound for all
                // authoritative dynamic routes, while avoiding a minimum that
                // can only be attained by a statically disconnected cell.
                // Flee ranking intentionally mirrors the synchronous
                // nearest-safe contract. Its lower-bound scan may include a
                // statically disconnected endpoint (as the live search does);
                // using the HPA component shortcut here changes the proven
                // minimum and can select a different equally valid escape
                // cell. Ordinary fixed-goal/ranked searches retain the HPA
                // pruning proof.
                if (!slice->flee && !captured->inStaticStartComponent(x,y,layer)) return false;
                if (!captured->destinationAllowed(x,y,layer,slice->destination)) return false;
                return allowed(x,y,layer);
            }
            if (!allowed(x,y,layer)) return false;
            if (!observedRank(x,y,layer,0).has_value()) return false;
            if (endpointRegion && layer==LAYER_GROUND && !endpointRegion->potentiallyReachable(x,y)) return false;
            return true;
        };
        while (!bound.complete()) {
            const auto used=bound.advance(remaining,accept);
            recordWork(used);
            co_await std::suspend_always{};
            context.relationships={};
            remaining=sliceBudget;
        }
        if (!bound.minimum()) co_return WeightedResult{};
        provenMinimum=*bound.minimum();
    }
    if (query.usePathDiameter) {
        auto reachabilityQuery=query;
        reachabilityQuery.centerInCell=true;
        Coord3D adjustedGoal{(goal.x+0.5f)*PATHFIND_CELL_SIZE_F,(goal.y+0.5f)*PATHFIND_CELL_SIZE_F,goalPosition.z};
        if (!slice && definitelyDisconnected(reachabilityQuery,&startPosition,&adjustedGoal)) co_return WeightedResult{};
    }
    const auto* startingCell=world_.getCell(startLayer,start.x,start.y);
    const bool pinchedStart=startingCell->getType()==PathfindCell::CELL_CLEAR && startingCell->getPinched();
    const bool escaping=!allowed(start.x,start.y,startLayer);
    // Policy-selected destinations retain their full modern fine graph. On
    // maps with bridges, the corridor also applies to layer coordinates;
    // widening eventually exposes every transition if ground guidance fails.
    if (!slice && !destination && !fallback && !escaping &&
        startLayer==LAYER_GROUND && goalLayer==LAYER_GROUND) {
        // Build optimistic anchor guidance. Group/downhill refinement may
        // widen it; ordinary phase refinement needs the complete fine graph.
        auto guidanceQuery=query;
        guidanceQuery.radius=0;guidanceQuery.centerInCell=true;
        hierarchyGraph=prepareLiveHierarchy(guidanceQuery);
        auto& hierarchy=scratch->hierarchy;
        ++state_->hierarchySearches;
        hierarchy.begin(hierarchyGraph,start.x-world_.m_extent.lo.x,start.y-world_.m_extent.lo.y,
            goal.x-world_.m_extent.lo.x,goal.y-world_.m_extent.lo.y);
        while (hierarchy.status()==RouteSearchStatus::Searching) {
            const auto before=hierarchy.result().work;
            hierarchy.advance(remaining);
            const auto used=unsigned(hierarchy.result().work-before);
            recordWork(used);remaining-=used;
            if (hierarchy.status()==RouteSearchStatus::Searching || remaining==0) {
                co_await std::suspend_always{};
                context.relationships={};remaining=sliceBudget;
            }
        }
        if (hierarchy.result().found()) {
            // An ordinary phase ray can relax cells outside a coarse corridor.
            // Deferring those cells changes the reference parent/route choices,
            // even when later widening reaches the destination. Until a bound
            // proves exclusion safe, refine its complete graph. Width-limited
            // and downhill searches retain their existing HPA corridor.
            restrictCorridor=query.usePathDiameter || downhillOnly;
            if (restrictCorridor) hierarchyCorridor=hierarchy.corridor(corridorPadding);
        }
    }
    const unsigned modes=escaping?2u:1u;
    if (cellCount*layers.size()*modes>=UINT32_MAX) co_return WeightedResult{};
    const auto terminal=static_cast<unsigned>(cellCount*layers.size()*modes);
    const unsigned modeShift=escaping?1u:0u;
    const auto planeSize=static_cast<unsigned>(cellCount);
    const UnsignedDivisor planeDivisor(planeSize), rowDivisor(static_cast<unsigned>(width));
    const auto encode=[&](int x,int y,PathfindLayerEnum layer,bool escape) {
        if (captured) return captured->encode(x,y,unsigned(layer),escape);
        // DX9 attaches search state to the returned native cell. Canonicalize
        // bridge lookups which alias ground, preserving that node identity
        // instead of exploring another copy of the entire ground map.
        if (!captured && layer!=LAYER_GROUND) {
            if (const auto* cell=world_.getCell(layer,x,y)) layer=cell->getLayer();
        }
        return static_cast<unsigned>((std::uint64_t(layerIndices[layer])*cellCount+
            std::uint64_t(y-world_.m_extent.lo.y)*width+x-world_.m_extent.lo.x)*modes+(escape?1:0));
    };
    struct Cell { int x,y; PathfindLayerEnum layer; bool escaping; };
    const auto decode=[&](unsigned id) {
        const auto index=id>>modeShift;
        const auto layer=planeDivisor.quotient(index);
        const auto local=index-layer*planeSize;
        const auto y=rowDivisor.quotient(local);
        const auto x=local-y*static_cast<unsigned>(width);
        return Cell{int(x)+world_.m_extent.lo.x,int(y)+world_.m_extent.lo.y,
            layers[layer],escaping && (id&1)!=0};
    };
    const auto heuristic=[&](unsigned id) {
            if (slice && captured && slice->flee)
                return slice->flee->heuristic(*captured,id);
            if (destination) return 0u;
            // Captured and live queries retain the same DX9 geometric ordering
            // from begin() through every slice. HPA connectivity may prune
            // impossible regions but must not replace this priority rule.
            if (slice && captured) return captured->referenceHeuristic(id);
            const auto cell=decode(id);
            const unsigned dx=std::abs(cell.x-goal.x),dy=std::abs(cell.y-goal.y);
            // The DX9 heuristic deliberately prices diagonals at 15. HPA
            // supplies connectivity pruning, not a replacement ordering that
            // would alter the low-cost line expansion's selected route.
            return 10u*std::max(dx,dy)+5u*std::min(dx,dy);
        };
    // RETAIL_COMPATIBLE_CRC in the pinned DX9 build retains this value across
    // expansions and reuses it when a ground neighbour is the goal itself.
    // Preserve that observable route choice with explicit initialized state.
    unsigned groundNeighborCost=0,examinedNeighbors=0;
    const auto neighbors=[&](unsigned id,auto emit) {
            const auto current=decode(id);
            auto* parent=world_.getCell(current.layer,current.x,current.y);
            if (!parent) return;
            if (destination && !current.escaping && destination(current.x,current.y,current.layer)) {
                emit(terminal,0);
                return;
            }
            // DX9 repair stops generating neighbours after 2,000 admitted
            // cells, but still tests every queued cell for a usable rejoin.
            // This is a query's gameplay bound, independent of slice budgets.
            if (neighborLimit && examinedNeighbors>=neighborLimit) return;
            // DX9 first admits a layer connection, then the phase-line cells.
            const auto connection=parent->getConnectLayer();
            if (connection>LAYER_INVALID && connection<=LAYER_LAST &&
                allowed(current.x,current.y,connection)) {
                const auto next=encode(current.x,current.y,connection,false);
                if (!scratch->search.observed(next)) scratch->search.relax(id,next,0,heuristic);
            }
            if (!destination && !current.escaping && !downhillOnly) {
                if (phaseCounts.enabled) ++phaseCounts.rays;
                unsigned previous=id;
                bool firstSample=true;
                if (!query.usePathDiameter && current.layer==LAYER_GROUND) {
                    auto previousCost=scratch->search.costOf(previous);
                    // Resolve the ground plane and scalar bounds once per ray.
                    // On a cache hit this avoids repeatedly walking coroutine
                    // captures, decoding layer aliases and encoding the same
                    // cell twice. Misses retain the shared exact classifier.
                    visitPhaseLine({current.x,current.y},{goal.x,goal.y},
                        [&,left=world_.m_extent.lo.x,top=world_.m_extent.lo.y,
                           width=unsigned(width),height=unsigned(height),modeShift,
                           cache=&scratch->groundPhase,search=&scratch->search](std::int64_t xx,std::int64_t yy) {
                        if (firstSample) { firstSample=false;return true; }
                        if (phaseCounts.enabled) ++phaseCounts.samples;
                        const int x=int(xx),y=int(yy);
                        const unsigned column=unsigned(x)-unsigned(left),row=unsigned(y)-unsigned(top);
                        if (column>=width || row>=height) return false;
                        const auto index=std::size_t(row)*width+column;
                        const auto* phase=cache->find(index);
                        if (!phase) phase=&cache->get(index,[&] {
                            const auto* sample=sampleAt(x,y,LAYER_GROUND);
                            return std::uint8_t(sample && sample->phasePassable);
                        });
                        if (!*phase) return false;
                        const auto next=static_cast<unsigned>(index)<<modeShift;
                        previousCost=search->relaxFromObserved(previous,next,std::uint64_t(previousCost)+5,heuristic);
                        previous=next;
                        return true;
                    });
                } else visitPhaseLine({current.x,current.y},{goal.x,goal.y},[&](std::int64_t xx,std::int64_t yy) {
                    if (firstSample) { firstSample=false;return true; }
                    if (phaseCounts.enabled) ++phaseCounts.samples;
                    const int x=int(xx),y=int(yy);
                    if (query.usePathDiameter) {
                        if (!world_.getCell(current.layer,x,y)) return false;
                        const auto next=encode(x,y,current.layer,false);
                        if (scratch->search.observed(next) ||
                            world_.clearCellForDiameter(query.crusher,x,y,current.layer,query.pathDiameter)!=query.pathDiameter)
                            return false;
                    } else {
                        const auto* sample=sampleAt(x,y,current.layer);
                        if (!sample || !sample->phasePassable) return false;
                    }
                    const auto next=encode(x,y,current.layer,false);
                    scratch->search.relax(previous,next,5,heuristic);
                    previous=next;
                    return true;
                });
            }
            WeightedStepQuery steps{world_.m_extent.lo.x,world_.m_extent.lo.y,
                world_.m_extent.hi.x,world_.m_extent.hi.y,start.x,start.y,query.pathDiameter,
                current.escaping,pinchedStart,downhillOnly,query.usePathDiameter!=0};
            const auto previousId=scratch->search.parentOf(id);
            if (previousId<terminal) {
                const auto previous=decode(previousId);
                steps.incomingX=current.x-previous.x;
                steps.incomingY=current.y-previous.y;
                steps.hasIncoming=true;
            }
            forEachWeightedStep(steps,current.x,current.y,[&](int x,int y) {
                if (!query.usePathDiameter &&
                    scratch->search.observed(encode(x,y,current.layer,current.escaping))) return WeightedStepCell{};
                auto* cell=world_.getCell(current.layer,x,y);
                if (!cell) return WeightedStepCell{};
                OccupancyResult occupancy{};
                const bool legal=allowed(x,y,current.layer,&occupancy);
                const bool terrainAllowed=world_.validMovementPosition(query.crusher,query.acceptableSurfaces,cell);
                const bool dozerException=classification.dozer && dozerPassage(cell) &&
                    !world_.validMovementPosition(query.crusher,query.acceptableSurfaces,cell);
                return WeightedStepCell{true,legal,terrainAllowed,occupancy,cell->getPinched()!=0,
                    !dozerException,dozerException?1000u:0u};
            },[&](int x,int y) {
                return TheTerrainLogic->getLayerHeight(x*PATHFIND_CELL_SIZE_F,y*PATHFIND_CELL_SIZE_F,current.layer);
            },[&](int x,int y) {
                // DX9's ground neighbour loop leaves clearDiameter at zero
                // for the goal itself (the line expansion still costs five).
                if (query.usePathDiameter && x==goal.x && y==goal.y && current.layer==goalLayer) return 0;
                return world_.clearCellForDiameter(query.crusher,x,y,current.layer,query.pathDiameter);
            },[&](int x,int y,unsigned cost,bool escaping) {
                const auto next=encode(x,y,current.layer,escaping);
                if (!scratch->search.observed(next)) ++examinedNeighbors;
                if (query.usePathDiameter) {
                    if (x!=goal.x || y!=goal.y || current.layer!=goalLayer)
                        groundNeighborCost=scratch->search.costOf(id)+cost;
                    scratch->search.relaxToCost(id,next,groundNeighborCost,heuristic);
                } else emit(next,cost);
            });
        };
    const auto nodeCount=terminal+((destination || fallback)?1u:0u);
    const auto first=encode(start.x,start.y,startLayer,escaping);
    const auto last=destination || (fallback && !exactGoalAllowed)?terminal:encode(goal.x,goal.y,goalLayer,false);
    const auto rank=[&](unsigned id,unsigned cost)->std::optional<double> {
            const auto cell=decode(id);
            if (cell.escaping) return {};
            return observedRank(cell.x,cell.y,cell.layer,cost);
        };
    auto& search=scratch->search;
    CapturedReferenceCursor referenceCursor;
    search.useReferenceOrder();
    const auto beginSearch=[&] {
        auto searchTiming=diagnostics::frameCapture().measure("Navigation.Search.Begin",
            TheGameLogic?TheGameLogic->getFrame():0,query.object?unsigned(query.object->getID()):0);
        if (fallback) {
            std::function<double(unsigned,unsigned)> remainingBound;
            if (slice && !slice->flee && !exactGoalAllowed && slice->rank.pathCostMultiplier>0)
                remainingBound=[target=slice->rank](unsigned cost,unsigned heuristic) {
                    return remainingDestinationRankBound(target,cost,heuristic);
                };
            std::function<std::optional<double>(unsigned,unsigned)> rankLowerBound;
            if (captured && !slice->flee && !slice->destinationPredicate)
                rankLowerBound=[graph=captured.get(),target=slice->rank](unsigned id,unsigned cost) {
                    return graph->rankLowerBound(id,cost,target);
                };
            search.beginWithFallback(nodeCount,first,last,heuristic,provenMinimum,std::move(remainingBound),
                false,
                std::move(rankLowerBound));
        } else {
            search.begin(nodeCount,first,last,heuristic,
                false);
        }
    };
    beginSearch();
    if (restrictCorridor) search.setExpansionFilter([&](unsigned id) {
        if (!restrictCorridor) return true;
        const auto cell=decode(id);
        return hierarchyCorridor[hierarchyGraph->node(cell.x-world_.m_extent.lo.x,
            cell.y-world_.m_extent.lo.y)]!=0;
    });
    for (;;) {
    if (restrictCorridor) ++state_->hierarchyRefinements;
    while (search.status()==RouteSearchStatus::Searching) {
        const auto before=search.result().work;
        if (slice) {
            // Only owned graph/search state crosses this boundary. Native objects,
            // topology, accounting and path materialization stay on the owner.
            slice->work=[graph=captured.get(),&search,&referenceCursor,budget=remaining,neighborLimit,&examinedNeighbors,
                          policy=slice.get(),target=slice->rank,destinationQuery=slice->destination] {
                const auto estimate=[&](unsigned id) {
                    return policy->flee ? policy->flee->heuristic(*graph,id) :
                        policy->destinationPredicate ? 0u : graph->referenceHeuristic(id);
                };
                const auto expand=[&](unsigned id,auto emit) {
                    const auto current=graph->decode(id);
                    if (policy->destinationPredicate && !current.escaping &&
                        policy->destinationPredicate(current.x,current.y,
                            PathfindLayerEnum(current.layer)) &&
                        graph->destinationAllowed(current.x,current.y,
                            current.layer,destinationQuery)) {
                        emit(graph->terminal(),0);
                        return;
                    }
                    if (neighborLimit && examinedNeighbors>=neighborLimit) return;
                    const auto boundedEmit=[&](unsigned next,unsigned cost) {
                        if (!search.observed(next)) ++examinedNeighbors;
                        emit(next,cost);
                    };
                    if (policy->flee) policy->flee->neighbors(*graph,id,destinationQuery,boundedEmit,search.parentOf(id),&search);
                    else if (policy->destinationPredicate) graph->neighbors(id,boundedEmit,search.parentOf(id),&search);
                    else graph->referenceNeighbors(id,search,emit);
                };
                const auto score=[&](unsigned id,unsigned cost) {
                    if (policy->flee) return policy->flee->rank(*graph,id,destinationQuery);
                    return graph->rank(id,cost,target,destinationQuery);
                };
                const auto stepReference=[&](unsigned id,unsigned available,auto emit) {
                    return graph->advanceReferenceNeighbors(id,search,referenceCursor,available,emit);
                };
                const auto before=search.result().work;
                const auto allowance=std::min(budget,policy->dispatchBudget);
                if (!policy->useFallback && !policy->flee && !policy->destinationPredicate && !neighborLimit) {
                    // One deterministic slice per dispatch, including partial
                    // phase rays. The owner must resume this continuation and
                    // dispatch again before the route can be published.
                    search.advanceReferenceSlice(allowance,estimate,stepReference);
                } else {
                    if (policy->useFallback)
                        search.advanceWithFallback(allowance,estimate,expand,score);
                    else
                        search.advance(allowance,estimate,expand);
                }
                policy->workCount=search.result().work-before;
            };
            co_await std::suspend_always{};
            context.relationships={};
        } else {
            auto searchTiming=diagnostics::frameCapture().measure("Navigation.Search.Advance",
                TheGameLogic?TheGameLogic->getFrame():0,query.object?unsigned(query.object->getID()):0);
            if (fallback) search.advanceWithFallback(remaining,heuristic,neighbors,rank);
            else search.advance(remaining,heuristic,neighbors);
        }
        const auto used=static_cast<unsigned>(search.result().work-before);
        if (!slice) recordWork(used);
        // Saturate before owner-side continuation accounting. Other query
        // kinds still count whole expansions rather than every phase sample.
        remaining=used>=remaining?0:remaining-used;
        if (search.status()==RouteSearchStatus::Searching || remaining==0) {
            if (!slice || search.status()!=RouteSearchStatus::Searching) {
                co_await std::suspend_always{};
                context.relationships={};
            }
            remaining=sliceBudget;
        }
    }
    if (search.result().found() || !restrictCorridor) break;
    // A coarse cluster can contain disconnected pockets or a narrow portal.
    // Widen in a fixed order until the authoritative graph is fully exposed.
    corridorPadding*=2;
    if (corridorPadding>=unsigned(std::max(hierarchyGraph->columns,hierarchyGraph->rows)))
        restrictCorridor=false;
    else hierarchyCorridor=scratch->hierarchy.corridor(corridorPadding);
    search.beginWidening();
    while (search.widening()) {
        const auto used=search.advanceWidening(remaining,heuristic);
        recordWork(used);remaining-=used;
        if (search.widening() || remaining==0) {
            co_await std::suspend_always{};
            context.relationships={};remaining=sliceBudget;
        }
    }
    }
    search.setExpansionFilter({});
    const auto& route=search.result();
    if (!route.found()) co_return WeightedResult{};
    auto waypointCount=route.nodes.size();
    if (destination && route.nodes.back()==terminal) --waypointCount;
    ReconstructionFlags optimizationFlags(waypointCount);
    const auto cliffAt=[&](std::size_t index) {
        const auto cell=decode(route.nodes[index]);
        if (captured) return captured->cliff(cell.x,cell.y,cell.layer);
        const auto* terrain=world_.getCell(cell.layer,cell.x,cell.y);
        return terrain && terrain->getType()==PathfindCell::CELL_CLIFF;
    };
    const auto samePosition=[&](std::size_t a,std::size_t b) {
        const auto first=decode(route.nodes[a]),second=decode(route.nodes[b]);
        return first.x==second.x && first.y==second.y;
    };
    while (!optimizationFlags.done()) {
        const auto used=optimizationFlags.advance(remaining,cliffAt,samePosition);
        recordWork(used);remaining-=used;
        if (!optimizationFlags.done() || remaining==0) {
            co_await std::suspend_always{};
            context.relationships={};remaining=sliceBudget;
        }
    }
    if (slice) {
        // Captured searches return compact value data. Allocate native Path
        // nodes only at publication on the simulation thread, so pending
        // requests do not each retain a fully materialized gameplay path.
        auto routeTiming=diagnostics::frameCapture().measure("Navigation.Capture.Waypoints",
            TheGameLogic?TheGameLogic->getFrame():0,unsigned(slice->moverId));
        std::vector<WeightedResult::Waypoint> waypoints;
        waypoints.reserve(route.nodes.size());
        waypoints.push_back({*from,startLayer,false,false});
        for (std::size_t i=1;i<route.nodes.size();++i) {
            if (destination && route.nodes[i]==terminal) break;
            const auto cell=decode(route.nodes[i]);
            if (i>1) {
                const auto previous=decode(route.nodes[i-1]);
                if (previous.x==cell.x && previous.y==cell.y && previous.layer!=cell.layer) {
                    waypoints.back().layer=cell.layer==LAYER_GROUND?previous.layer:cell.layer;
                    continue;
                }
            }
            Coord3D position;
            world_.adjustCoordToCell(cell.x,cell.y,query.centerInCell,position,cell.layer);
            bool blockedByAlly=false;
            if (markAlliedBlockers) {
                OccupancyResult traffic{};
                if (captured && captured->classify(cell.x,cell.y,cell.layer,&traffic))
                    blockedByAlly=traffic.allyFixedCount>0;
            }
            waypoints.push_back({position,cell.layer,optimizationFlags.at(i),blockedByAlly});
            recordWork(1);
        }
        std::vector<unsigned> links;
        if (waypoints.size()>1) {
            links.push_back(0);
            std::size_t node=0;
            while (node+1<waypoints.size()) {
                const auto& first=waypoints[node];
                const auto& next=waypoints[node+1];
                std::size_t end=node+1;
                const float dx=next.position.x-first.position.x;
                const float dy=next.position.y-first.position.y;
                while (end+1<waypoints.size() &&
                       waypoints[end].layer==first.layer &&
                       waypoints[end+1].layer==first.layer) {
                    const float nx=waypoints[end+1].position.x-waypoints[end].position.x;
                    const float ny=waypoints[end+1].position.y-waypoints[end].position.y;
                    if (!(dx*ny==dy*nx && dx*nx+dy*ny>0)) break;
                    ++end;
                    recordWork(1);
                }
                links.push_back(static_cast<unsigned>(end));
                node=end;
                recordWork(1);
            }
        }
        WeightedResult compact(nullptr,route.usedFallback);
        compact.waypoints=std::move(waypoints);
        compact.optimizedLinks=std::move(links);
        compact.capturedTerrain=std::move(slice->capturedTerrain);
        compact.terrainEpoch=slice->terrainEpoch;
        co_return std::move(compact);
    }
    WeightedResult result(newInstance(Path),route.usedFallback);
    auto* path=result.path.get();
    path->appendNode(from,startLayer);
    recordWork(1);
    if (--remaining==0) {
        co_await std::suspend_always{};
        context.relationships={};
        remaining=sliceBudget;
    }
    for (std::size_t i=route.nodes.size()==1?0:1;i<route.nodes.size();++i) {
        if (destination && route.nodes[i]==terminal) break;
        const auto cell=decode(route.nodes[i]);
        Coord3D position;
        world_.adjustCoordToCell(cell.x,cell.y,query.centerInCell,position,cell.layer);
        if (i>1) {
            const auto previous=decode(route.nodes[i-1]);
            if (previous.x==cell.x && previous.y==cell.y && previous.layer!=cell.layer) {
                path->getLastNode()->setLayer(cell.layer==LAYER_GROUND?previous.layer:cell.layer);
                continue;
            }
        }
        path->appendNode(&position,cell.layer);
        if (markAlliedBlockers) {
            OccupancyResult traffic{};
            allowed(cell.x,cell.y,cell.layer,&traffic);
            if (traffic.allyFixedCount>0) path->setBlockedByAlly(true);
        }
        path->getLastNode()->setCanOptimize(optimizationFlags.at(i));
        recordWork(1);
        if (--remaining==0) {
            co_await std::suspend_always{};
            context.relationships={};
            remaining=sliceBudget;
        }
    }
    // Collapse only collinear runs. A visibility shortcut must not erase a
    // weighted detour around traffic and send everyone back into the queue.
    for (auto* node=path->getFirstNode();node && node->getNext();) {
        auto* end=node->getNext();
        const auto* first=node->getPosition();
        const float dx=end->getPosition()->x-first->x,dy=end->getPosition()->y-first->y;
        while (end->getNext() && end->getLayer()==node->getLayer() && end->getNext()->getLayer()==node->getLayer()) {
            const float nx=end->getNext()->getPosition()->x-end->getPosition()->x;
            const float ny=end->getNext()->getPosition()->y-end->getPosition()->y;
            const bool collinear=dx*ny==dy*nx && dx*nx+dy*ny>0;
            recordWork(1);
            if (--remaining==0) {
                co_await std::suspend_always{};
                context.relationships={};
                remaining=sliceBudget;
            }
            if (!collinear) break;
            end=end->getNext();
        }
        node->setNextOptimized(end);
        node=end;
        recordWork(1);
        if (--remaining==0) {
            co_await std::suspend_always{};
            context.relationships={};
            remaining=sliceBudget;
        }
    }
    path->markOptimized();
    co_return std::move(result);
}

bool GroundRoutePlanner::capturedTerrainUnchanged(const Pathfinder::GroundRouteQuery& query,
    const WeightedResult& result) {
    if (result.capturedTerrain.empty() || result.terrainEpoch==state_->epoch) return true;
    const auto same=[&](std::int64_t x,std::int64_t y,PathfindLayerEnum layer) {
        const auto plane=std::find_if(result.capturedTerrain.begin(),result.capturedTerrain.end(),
            [layer](const auto& value) { return value.cells && value.cells->layer()==unsigned(layer); });
        if (plane==result.capturedTerrain.end()) return false;
        const auto& before=*plane->cells;
        // Ordinary terrain reads the anchor and diagonal side cells. The
        // one-cell halo also covers either raster choice around a phase edge.
        // Dynamic occupancy is a separate publication dependency.
        const auto below=std::max(1,query.radius);
        const auto above=std::max(1,query.radius+(query.centerInCell?1:0)-1);
        for (int dx=-below;dx<=above;++dx) for (int dy=-below;dy<=above;++dy) {
            const auto xx=x+dx,yy=y+dy;
            if (xx<std::numeric_limits<int>::min() || xx>std::numeric_limits<int>::max() ||
                yy<std::numeric_limits<int>::min() || yy>std::numeric_limits<int>::max()) continue;
            world_.recordNavigationWork(1);
            const auto* old=before.cell(int(xx),int(yy));
            const auto* now=world_.getCell(layer,int(xx),int(yy));
            if (bool(old && old->occupancy.valid)!=bool(now)) return false;
            if (!now) continue;
            if (old->terrain!=unsigned(now->getType()) || old->connection!=unsigned(now->getConnectLayer()) ||
                old->obstacle!=std::uint32_t(now->getObstacleID()) || old->pinched!=(now->getPinched()!=0) ||
                old->fence!=(now->isObstacleFence()!=0)) return false;
        }
        return true;
    };
    ICoord2D previous{};
    PathfindLayerEnum previousLayer=LAYER_INVALID;
    bool valid=true;
    for (std::size_t i=0;i<result.waypoints.size();++i) {
        const auto& waypoint=result.waypoints[i];
        ICoord2D current;world_.worldToCell(&waypoint.position,&current);
        if (!i) { previous=current;previousLayer=waypoint.layer; }
        visitPhaseLine({previous.x,previous.y},{current.x,current.y},[&](auto x,auto y) {
            if (!same(x,y,waypoint.layer) ||
                (previousLayer!=waypoint.layer && !same(x,y,previousLayer))) {
                valid=false;return false;
            }
            // The legacy iterator probes beyond its endpoint; that cell is
            // not part of this route segment's dependency footprint.
            return x!=current.x || y!=current.y;
        });
        if (!valid) return false;
        previous=current;
        previousLayer=waypoint.layer;
    }
    return true;
}

Path* GroundRoutePlanner::materializeCapturedResult(const Pathfinder::GroundRouteQuery& query,
    WeightedResult&& result) {
    auto publicationTiming=diagnostics::frameCapture().measure("Navigation.Capture.Materialize",
        TheGameLogic?TheGameLogic->getFrame():0,query.object?unsigned(query.object->getID()):0);
    if (result.waypoints.empty()) return nullptr;
    if (!capturedTerrainUnchanged(query,result)) {
        result.staleTerrain=true;
        return nullptr;
    }
    WeightedResult holder(newInstance(Path));
    auto* path=holder.path.get();
    if (result.waypoints.size()==1) {
        // A captured destination can legitimately resolve at the start cell
        // (notably an already-safe flee request).  The native contract still
        // publishes that stationary path; rejecting it here makes a valid
        // deterministic result look like an unavailable search.
        path->appendNode(&result.waypoints.front().position,result.waypoints.front().layer);
        path->markOptimized();
        return holder.path.release();
    }
    if (result.optimizedLinks.size()<2) return nullptr;
    std::vector<PathNode*> nodes;
    nodes.reserve(result.waypoints.size());
    bool blockedByAlly=false;
    for (const auto& waypoint:result.waypoints) {
        path->appendNode(&waypoint.position,waypoint.layer);
        auto* node=path->getLastNode();
        node->setCanOptimize(waypoint.canOptimize);
        blockedByAlly=blockedByAlly || waypoint.blockedByAlly;
        nodes.push_back(node);
    }
    if (blockedByAlly) path->setBlockedByAlly(true);
    for (std::size_t i=1;i<result.optimizedLinks.size();++i)
        nodes[result.optimizedLinks[i-1]]->setNextOptimized(nodes[result.optimizedLinks[i]]);
    path->markOptimized();
    builder_.materialize(path,query.centerInCell);
    smooth(path,query);
    return holder.path.release();
}

Pathfinder::NavigationStats GroundRoutePlanner::stats() const { return stats_; }
}
