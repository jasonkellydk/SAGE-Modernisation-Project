module;

#include <string>
#include <fstream>
#include <bit>
#include <cmath>
#include <array>
#include <vector>
#include <span>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include "Utility/CppMacros.h"
#include "PreRTS.h"
#include "engine/navigation/pathfinder_api.h"
#include "Common/CriticalSection.h"
#include "Common/GlobalData.h"
#include "Common/ThingFactory.h"
#include "GameLogic/AI.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameLogic/W3DTerrainLogic.h"

export module engine.navigation.world_fixture;
import engine.navigation.pathfinder;
import engine.navigation.topology.terrain_slope;
import engine.navigation.querymemo;
import engine.navigation.search_workspace;
import engine.navigation.dynamic_request_queue;
import engine.navigation.movement.terrain_policy;
import engine.navigation.movement.snapshot.cells;
import engine.navigation.movement.classification.weighted_cell;
import engine.navigation.movement.occupancy_policy;
import engine.navigation.movement.corridor.clearance;
import engine.navigation.movement.snapshot.occupants;
import engine.navigation.search.graph.captured_weighted;
import engine.navigation.search.cluster_topology;
import engine.navigation.search.route_search;
import engine.navigation.scheduling.continuation;
import engine.navigation.search.graph.destination_rank;
import engine.navigation.movement.destination.reservations;
import Graphics.Frame.Runtime;

// Supply executable-owned platform symbols without starting a renderer or game loop.
extern "C++" {
const Char* g_strFile = "data\\Generals.str";
const Char* g_csfFile = "data\\%s\\Generals.csf";
const char* gAppPrefix = "";
class GameEngine;
GameEngine* CreateGameEngine() { throw std::logic_error("Fixture must not create a game engine"); }
}

namespace navigation::testing::detail {
class InspectableAI final : public AI {
public:
    std::vector<unsigned> groupOrder() const {
        std::vector<unsigned> result;
        for (AIGroup* group : m_groupList) result.push_back(group->getID());
        return result;
    }
};
class HeadlessHeightMap final : public BaseHeightMapRenderObjClass {
public:
    void Render(W3DRenderContext&) override {}
    void doPartialUpdate(const IRegion2D&, WorldHeightMap*, Graphics::SceneObjectList<W3DRenderObject>::Cursor*) override {}
    void oversizeTerrain(Int) override {}
    void setTerrainDrawSize(Int, Int) override {}
    int updateBlock(Int, Int, Int, Int, WorldHeightMap*, Graphics::SceneObjectList<W3DRenderObject>::Cursor*) override { return 0; }
};
class BridgeDamageTerrain final : public W3DTerrainLogic {
public:
    std::function<void()> synchronize;
    void updateBridgeDamageStates() override { synchronize(); }
};
class FlatTerrain : public TerrainLogic {
    Region3D extent_{};
public:
    void setExtent(int width, int height) { extent_ = {{0, 0, 0}, {width * 10.0f, height * 10.0f, 0}}; }
    void getExtent(Region3D* out) const override { *out = extent_; }
    void getMaximumPathfindExtent(Region3D* out) const override { *out = extent_; }
    void ownBridge(Bridge* bridge) { m_bridgeListHead = bridge; }
    Real getGroundHeight(Real, Real, Coord3D* normal = nullptr) const override {
        if (normal) *normal = {0, 0, 1};
        return 0;
    }
    Real getLayerHeight(Real, Real, PathfindLayerEnum, Coord3D* normal = nullptr, Bool = true) const override {
        if (normal) *normal = {0, 0, 1};
        return 0;
    }
};
class HeightTerrain final : public FlatTerrain {
    std::vector<unsigned char> heights_;
    float vertex(int x, int y) const {
        x=std::clamp(x+70,0,629); y=std::clamp(y+70,0,629);
        return heights_[y*630+x]*0.625f;
    }
public:
    HeightTerrain() : heights_(630*630) {
        std::ifstream input(std::string(NAVIGATION_FIXTURE_DIRECTORY)+"/twilight_flame/heights.bin",std::ios::binary);
        if (!input.read(reinterpret_cast<char*>(heights_.data()),heights_.size()) || input.peek()!=std::char_traits<char>::eof())
            throw std::runtime_error("Missing or invalid Twilight Flame height fixture");
        std::uint64_t checksum=14695981039346656037ull;
        for (auto value : heights_) checksum=(checksum^value)*1099511628211ull;
        if (checksum!=0x832606714b6beff6ull) throw std::runtime_error("Twilight Flame fixture checksum differs");
        setExtent(486,469);
    }
    // Height-only fixture: map water polygons and object footprints are added
    // separately; do not consult a renderer in this headless terrain service.
    Bool isUnderwater(Real, Real, Real* waterZ=nullptr, Real* terrainZ=nullptr) override {
        if (waterZ) *waterZ=0;
        if (terrainZ) *terrainZ=0;
        return false;
    }
    Bool isCliffCell(Real x, Real y) const override {
        const int xx=int(std::floor(x/10)),yy=int(std::floor(y/10));
        return terrainCellIsCliff({vertex(xx,yy),vertex(xx+1,yy),vertex(xx,yy+1),vertex(xx+1,yy+1)});
    }
    Real getGroundHeight(Real x, Real y, Coord3D* normal=nullptr) const override {
        if (normal) *normal={0,0,1};
        const float xx=x/10, yy=y/10;
        const int ix=int(std::floor(xx)),iy=int(std::floor(yy));
        const float fx=xx-ix,fy=yy-iy;
        return std::lerp(std::lerp(vertex(ix,iy),vertex(ix+1,iy),fx),
            std::lerp(vertex(ix,iy+1),vertex(ix+1,iy+1),fx),fy);
    }
    Real getLayerHeight(Real x, Real y, PathfindLayerEnum, Coord3D* normal=nullptr, Bool=true) const override {
        return getGroundHeight(x,y,normal);
    }
};

}

export namespace navigation::testing {
struct Result {
    bool found = false;
    bool legal = false;
    bool macroEdges = false;
    bool optimizedEdges = false;
    std::vector<std::array<int, 2>> rawGroundCells;
    std::vector<unsigned> rawOptimizationFlags;
    std::string raw;
    std::string optimized;
    int cells = 0;
    bool clean = false;
};
struct CapturedRouteCase {
    std::unique_ptr<CapturedWeightedGraph> graph;
    std::shared_ptr<const HpaClusterGraph> hierarchy;
    std::vector<unsigned> reference;
};
// Only classification and service setup are synthetic. Zone building, searching,
// clearance, costs, path construction and optimization are production code.
extern "C++" class World {
    Pathfinder* pf;
    static constexpr PathfindLayerEnum LAYER_BRIDGE_1 = static_cast<PathfindLayerEnum>(2);
public:
    explicit World(const std::vector<std::string>& rows) {
        if (rows.empty() || rows.front().empty()) throw std::invalid_argument("Empty map");
        for (const auto& row : rows) {
            if (row.size() != rows.front().size()) throw std::invalid_argument("Ragged map");
            for (char c : row) type(c);
        }
        if (TheAI || TheTerrainLogic || TheGameLogic || TheWritableGlobalData || TheThingFactory)
            throw std::logic_error("Fixtures must run serially in an isolated process");
        static CriticalSection locks[5];
        TheAsciiStringCriticalSection = &locks[0];
        TheUnicodeStringCriticalSection = &locks[1];
        TheDmaCriticalSection = &locks[2];
        TheMemoryPoolCriticalSection = &locks[3];
        TheDebugLogCriticalSection = &locks[4];
        initMemoryManager();
        TheWritableGlobalData = NEW GlobalData;
        TheGameLogic = NEW GameLogic;
        TheTerrainLogic = NEW detail::FlatTerrain;
        TheAI = NEW detail::InspectableAI;
        TheThingFactory = NEW ThingFactory;
        pf = TheAI->pathfinder();
        const int width = static_cast<int>(rows.front().size());
        const int height = static_cast<int>(rows.size());
        static_cast<detail::FlatTerrain*>(TheTerrainLogic)->setExtent(width, height);
        pf->m_extent = {{0, 0}, {width - 1, height - 1}};
        pf->m_logicalExtent = pf->m_extent;
        pf->m_blockOfMapCells = NEW PathfindCell[width * height];
        pf->m_map = NEW PathfindCell*[width];
        for (int x = 0; x < width; ++x) {
            pf->m_map[x] = pf->m_blockOfMapCells + x * height;
            for (int y = 0; y < height; ++y) {
                auto& cell = pf->m_map[x][y];
                cell.setType(type(rows[y][x]));
                cell.setLayer(LAYER_GROUND);
            }
        }
        pf->m_isMapReady = true;
        pf->m_groundPlanner->warmStaticSnapshot();
    }
    ~World() {
        delete TheGameLogic; // GameLogic owns terrain and needs AI during teardown.
        TheGameLogic = nullptr;
        pf->reset();
        delete TheAI;
        TheAI = nullptr;
        delete TheThingFactory;
        TheThingFactory = nullptr;
        delete TheWritableGlobalData;
        TheWritableGlobalData = nullptr;
    }
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    bool incrementalOccupancyCapturesSameFrameMutations(bool regional=false) {
        auto& planner=*pf->m_groundPlanner;
        Pathfinder::GroundRouteQuery query;query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        planner.prepareCapturedSnapshot(query,false);
        planner.captureDynamicSnapshotIncremental();
        const auto initial=planner.state_->capturedDynamicOccupancy;
        const auto nativeMatches=[&] {
            const auto native=MovementValidator(*pf).captureCells(LAYER_GROUND);
            const auto& base=*planner.state_->capturedLayers.front().cells;
            for (int x=0;x<=pf->m_extent.hi.x;++x) for (int y=0;y<=pf->m_extent.hi.y;++y) {
                const auto* expected=native.cell(x,y);
                const auto* before=base.cell(x,y);
                const auto* changed=planner.state_->capturedDynamicOccupancy->find(x,y,LAYER_GROUND);
                const auto actual=changed?changed->occupancy:before->occupancy;
                if (actual.unit!=expected->occupancy.unit || actual.valid!=expected->occupancy.valid ||
                    actual.empty!=expected->occupancy.empty || actual.goal!=expected->occupancy.goal ||
                    actual.moving!=expected->occupancy.moving || actual.fixed!=expected->occupancy.fixed ||
                    (changed?changed->goal:before->goal)!=expected->goal ||
                    (changed?changed->aircraftGoal:before->aircraftGoal)!=expected->aircraftGoal ||
                    (changed?changed->aircraftReserved:before->aircraftReserved)!=expected->aircraftReserved ||
                    unsigned(changed?changed->terrain:static_cast<TerrainKind>(before->terrain))!=expected->terrain ||
                    (changed?changed->obstacle:before->obstacle)!=expected->obstacle ||
                    (changed?changed->pinched:before->pinched)!=expected->pinched ||
                    (changed?changed->fence:before->fence)!=expected->fence ||
                    (changed?changed->connection:before->connection)!=expected->connection)
                    return false;
            }
            return true;
        };
        auto& cell=pf->m_map[3][4];const ICoord2D position{3,4};
        cell.setPosUnit(ObjectID(17),position);
        cell.setGoalUnit(ObjectID(17),position);
        cell.setGoalAircraft(ObjectID(23),position);
        // Repeated writes to one cell must not grow the mutation journal.
        for (unsigned repeat=0;repeat<4096;++repeat) cell.setGoalAircraft(ObjectID(23),position);
        if (planner.state_->dirtyDynamicIndices.size()!=1) return false;
        planner.captureDynamicSnapshotIncremental();
        if (!nativeMatches()) return false;
        const auto occupied=planner.state_->capturedDynamicOccupancy;
        cell.setGoalUnit(ObjectID(19),position);
        planner.captureDynamicSnapshotIncremental();
        if (!nativeMatches()) return false;
        if (regional) {
            const auto baseline=planner.state_->capturedDynamicOccupancy;
            const auto oldCells=planner.state_->capturedLayers.front().cells;
            // Writes on both sides of the terrain invalidation remain pending.
            auto& other=pf->m_map[7][8];const ICoord2D otherPosition{7,8};
            other.setPosUnit(ObjectID(29),otherPosition);
            other.setGoalUnit(ObjectID(29),otherPosition);
            setCell(3,4,'^');setPinched(3,4,true,true);
            other.setGoalAircraft(ObjectID(31),otherPosition);
            if (planner.state_->capturedDynamicOccupancy!=baseline ||
                planner.state_->dirtyDynamicIndices.size()!=2) return false;
            planner.prepareCapturedSnapshot(query,false);
            if (planner.state_->capturedDynamicOccupancy!=baseline ||
                planner.state_->capturedDynamicEpoch!=planner.state_->epoch) return false;
            planner.captureDynamicSnapshotIncremental();
            if (!nativeMatches() || oldCells->cell(3,4)->terrain!=unsigned(TerrainKind::ground) ||
                oldCells->cell(3,4)->pinched) return false;
            const auto ids=planner.state_->capturedOccupantIds;
            if (!std::binary_search(ids.begin(),ids.end(),29u) ||
                !std::binary_search(ids.begin(),ids.end(),31u)) return false;
            // An empty clipped edit still refreshes epoch and relationship
            // metadata; it must not lose IDs from the retained dynamic view.
            planner.invalidate(IRegion2D{{30,30},{31,31}});
            planner.prepareCapturedSnapshot(query,false);
            planner.captureDynamicSnapshotIncremental();
            if (planner.state_->capturedOccupantIds!=ids || !nativeMatches()) return false;
        }
        cell.setGoalUnit(INVALID_ID,position);
        cell.setPosUnit(INVALID_ID,position);
        cell.setGoalAircraft(INVALID_ID,position);
        planner.captureDynamicSnapshotIncremental();
        if (!nativeMatches()) return false;
        const auto* retained=occupied->find(3,4,LAYER_GROUND);
        return !initial->find(3,4,LAYER_GROUND) && retained && retained->occupancy.fixed &&
            retained->occupancy.unit==17 && retained->goal==17 && retained->aircraftGoal==23 &&
            retained->aircraftReserved;
    }

    bool regionalStaticCaptureRetainsUnaffectedPages() {
        auto& planner=*pf->m_groundPlanner;
        Pathfinder::GroundRouteQuery query;query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        planner.prepareCapturedSnapshot(query,false);
        const auto original=planner.state_->capturedLayers.front().cells;
        const auto hierarchy=planner.state_->staticHpa;
        const auto layered=planner.state_->layeredHpa;
        setCell(3,4,'^');setPinched(3,4,true,true);
        setCell(31,30,'r');
        if (planner.state_->dirtyStaticIndices.size()!=2) return false;
        planner.prepareCapturedSnapshot(query,false);
        const auto edited=planner.state_->capturedLayers.front().cells;
        if (edited->cell(16,16)!=original->cell(16,16) ||
            edited->cell(3,4)==original->cell(3,4) ||
            edited->cell(31,30)==original->cell(31,30) ||
            planner.state_->staticHpa!=hierarchy || planner.state_->layeredHpa!=layered) return false;
        if (original->cell(3,4)->terrain!=unsigned(TerrainKind::ground) ||
            original->cell(3,4)->pinched ||
            edited->cell(3,4)->terrain!=unsigned(TerrainKind::cliff) ||
            !edited->cell(3,4)->pinched ||
            edited->cell(31,30)->terrain!=unsigned(TerrainKind::rubble)) return false;
        pf->m_map[16][16].setConnectLayer(LAYER_BRIDGE_1);
        planner.invalidate(IRegion2D{{16,16},{16,16}});
        planner.prepareCapturedSnapshot(query,false);
        if (planner.state_->layeredHpa==layered ||
            planner.state_->capturedLayers.front().cells->cell(16,16)->connection!=unsigned(LAYER_BRIDGE_1) ||
            edited->cell(16,16)->connection==unsigned(LAYER_BRIDGE_1)) return false;
        // A global invalidation still replaces every page, including a change
        // outside any regional journal. Retained views must remain unchanged.
        pf->m_map[16][16].setType(PathfindCell::CELL_WATER);
        planner.invalidate();planner.prepareCapturedSnapshot(query,false);
        const auto rebuilt=planner.state_->capturedLayers.front().cells;
        return rebuilt->cell(16,16)!=edited->cell(16,16) &&
            rebuilt->cell(16,16)->terrain==unsigned(TerrainKind::water) &&
            edited->cell(16,16)->terrain==unsigned(TerrainKind::ground);
    }

    bool regionalCaptureReusesEveryUnchangedLayerHierarchy() {
        addBridge();
        auto& planner=*pf->m_groundPlanner;
        Pathfinder::GroundRouteQuery query;query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        planner.prepareCapturedSnapshot(query,false);
        const auto previous=planner.state_->capturedLayers;
        const auto layered=planner.state_->layeredHpa;
        if (previous.size()<2) return false;
        setCell(16,16,'^');planner.prepareCapturedSnapshot(query,false);
        if (planner.state_->layeredHpa!=layered) return false;
        for (std::size_t i=0;i<previous.size();++i) {
            const auto& next=planner.state_->capturedLayers[i];
            if (next.hpa!=previous[i].hpa ||
                next.cells->cell(16,16)->terrain!=unsigned(TerrainKind::cliff) ||
                previous[i].cells->cell(16,16)->terrain!=unsigned(TerrainKind::ground)) return false;
        }
        // Outside the bridge rectangle, its native cell lookup aliases ground.
        // A real topology edit must invalidate both layer abstractions.
        setCell(16,16,'#');planner.prepareCapturedSnapshot(query,false);
        for (std::size_t i=0;i<previous.size();++i) {
            const auto& next=planner.state_->capturedLayers[i];
            if (next.hpa==previous[i].hpa || next.hpa->fineConnected({15,16},{16,16}) ||
                !previous[i].hpa->fineConnected({15,16},{16,16})) return false;
        }
        return planner.state_->layeredHpa!=layered;
    }

    std::array<double,4> regionalStaticCaptureTimings() {
        auto& planner=*pf->m_groundPlanner;
        Pathfinder::GroundRouteQuery query;query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        setCell(100,100,'^');
        planner.prepareCapturedSnapshot(query,false);planner.captureDynamicSnapshot();
        std::array<double,4> maxima{};
        for (unsigned mode=0;mode<2;++mode) for (unsigned repeat=0;repeat<16;++repeat) {
            setCell(100,100,(repeat&1)?'^':'r');
            if (mode) planner.invalidate();
            const auto start=std::chrono::steady_clock::now();
            planner.prepareCapturedSnapshot(query,false);
            const auto captured=std::chrono::steady_clock::now();
            planner.captureDynamicSnapshot();
            const auto done=std::chrono::steady_clock::now();
            maxima[mode*2]=std::max(maxima[mode*2],std::chrono::duration<double,std::milli>(captured-start).count());
            maxima[mode*2+1]=std::max(maxima[mode*2+1],std::chrono::duration<double,std::milli>(done-captured).count());
            const auto expected=(repeat&1)?TerrainKind::cliff:TerrainKind::rubble;
            if (planner.state_->capturedLayers.front().cells->cell(100,100)->terrain!=unsigned(expected))
                throw std::logic_error("Regional capture missed the terrain edit");
        }
        return maxima;
    }

    bool capturedHierarchyTracksTraversalRatherThanTerrainLabels() {
        auto& planner=*pf->m_groundPlanner;
        Pathfinder::GroundRouteQuery query;query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        planner.prepareCapturedSnapshot(query,false);
        const auto original=planner.state_->staticHpa;
        const auto initialCells=planner.state_->capturedLayers.front().cells;
        const auto fineAllows=[&] {
            CapturedGraphQuery input;
            input.movement.terrain.surfaces=1;input.occupancy.cellsAbove=1;
            input.startX=1;input.startY=4;input.goalX=4;input.goalY=4;input.checkOccupants=false;
            CapturedWeightedGraph graph(planner.state_->capturedLayers,
                std::make_shared<const OccupantSnapshot>(),input);
            return graph.passable(4,4,1);
        };
        if (!fineAllows()) return false;
        setCell(4,4,'^');planner.prepareCapturedSnapshot(query,false);
        // This abstract graph deliberately admits every valid non-impassable
        // anchor. Fine ground policy must still reject the new cliff.
        if (planner.state_->staticHpa!=original || fineAllows() ||
            initialCells->cell(4,4)->terrain!=unsigned(TerrainKind::ground)) return false;
        setCell(4,4,'.');planner.prepareCapturedSnapshot(query,false);
        if (planner.state_->staticHpa!=original || !fineAllows()) return false;
        setCell(4,4,'#');planner.prepareCapturedSnapshot(query,false);
        if (planner.state_->staticHpa==original || fineAllows() ||
            planner.state_->staticHpa->fineConnected({1,4},{4,4})) return false;
        setCell(4,4,'.');planner.prepareCapturedSnapshot(query,false);
        return fineAllows() && planner.state_->staticHpa->fineConnected({1,4},{4,4});
    }

    void loadTwilightFlameTerrain() {
        if (pf->m_extent.hi.x!=485 || pf->m_extent.hi.y!=468)
            throw std::logic_error("Twilight Flame requires a 486 by 469 grid");
        auto* terrain=NEW detail::HeightTerrain;
        delete TheTerrainLogic;
        TheTerrainLogic=terrain;
        pf->classifyMap();
        pf->m_groundPlanner->warmStaticSnapshot();
    }
    std::vector<std::string> terrainClassification() const {
        std::vector<std::string> rows(pf->m_extent.hi.y+1,std::string(pf->m_extent.hi.x+1,'#'));
        for (int y=0;y<=pf->m_extent.hi.y;++y)
            for (int x=0;x<=pf->m_extent.hi.x;++x) {
                const auto* cell=pf->getCell(LAYER_GROUND,x,y);
                if (cell->getType()==PathfindCell::CELL_CLEAR && !cell->getPinched()) rows[y][x]='.';
            }
        return rows;
    }

    bool unownedPreviewTracksRemainEmpty() {
        class Probe final : public TerrainTracksRenderObjClass {
        public:
            void pendingCap() { m_haveCap=false; }
            bool empty() const { return !m_haveAnchor && m_activeEdgeCount==0 && m_totalEdgesAdded==0; }
        } track;
        track.setOwnerDrawable(nullptr);
        for (int i=0;i<100;++i) {
            track.addEdgeToTrack(float(i*20),float(i*10));
            track.pendingCap();
            track.addCapEdgeToTrack(float(i*20),float(i*10));
        }
        return track.empty();
    }

    std::vector<std::vector<unsigned>> exerciseGroupOwnership() {
        TheAI->init();
        std::vector<AIGroupPtr> groups;
        for (unsigned i = 0; i < 2048; ++i) groups.push_back(TheAI->createGroup());
        auto order = [] { return static_cast<detail::InspectableAI*>(TheAI)->groupOrder(); };
        std::vector<std::vector<unsigned>> result{order()};
        auto release = [&](unsigned index) {
#if RETAIL_COMPATIBLE_AIGROUP
            TheAI->destroyGroup(groups[index]);
#endif
            groups[index] = nullptr;
        };
        // Erase interior entries without changing the simulation traversal order.
        for (unsigned i = 1; i < 2048; i += 2) release(i);
        result.push_back(order());
        for (unsigned i = 0; i < 512; ++i) groups.push_back(TheAI->createGroup());
        result.push_back(order());
        for (unsigned i = 0; i < groups.size(); ++i) if (groups[i]) release(i);
        TheAI->destroyGroup(nullptr);
        if (TheAI->doesGroupExist(nullptr)) throw std::logic_error("Null group was registered");
        result.push_back(order());
        return result;
    }

    // Retained as a source-compatible fixture control; macro expansion is
    // selected by the modern graph and is never a separate route algorithm.

    std::string smoothReferenceGroundPath(const char* raw,int diameter=1) {
        auto* path=newInstance(Path);
        std::istringstream input(raw);
        input.imbue(std::locale::classic());
        Coord3D position{};
        int layer=0,optimize=0;
        char comma1,comma2,colon1,colon2;
        while (input >> position.x >> comma1 >> position.y >> comma2 >> position.z
                     >> colon1 >> layer >> colon2 >> optimize) {
            if (comma1!=',' || comma2!=',' || colon1!=':' || colon2!=':') {
                deleteInstance(path);
                throw std::runtime_error("Invalid DX9 route fixture");
            }
            path->appendNode(&position,PathfindLayerEnum(layer));
            path->getLastNode()->setCanOptimize(optimize!=0);
        }
        path->optimizeGroundPath(false,diameter);
        return capture(path,diameter,false).optimized;
    }

    bool dx9PathPublicationSmoothsWithoutRemovingRawNodes() {
        Path* path = newInstance(Path);
        Coord3D first{15.0f, 15.0f, 0.0f};
        Coord3D middle{25.0f, 15.0f, 0.0f};
        Coord3D last{35.0f, 15.0f, 0.0f};
        path->appendNode(&first, LAYER_GROUND);
        path->appendNode(&middle, LAYER_GROUND);
        path->appendNode(&last, LAYER_GROUND);
        path->optimizeGroundPath(false, 1);
        const auto* a = path->getFirstNode();
        const auto* b = a ? a->getNextOptimized() : nullptr;
        const bool valid = a && b && a->getNext() && b == a->getNext()->getNext() &&
            b == path->getLastNode() && b->getNextOptimized() == nullptr;
        deleteInstance(path);
        return valid;
    }
    void setLogicalBounds(int left, int top, int right, int bottom) {
        pf->m_logicalExtent = {{left, top}, {right, bottom}};
    }
    bool disconnected(int fromX,int fromY,int toX,int toY,bool human,int radius=0) {
        Pathfinder::GroundRouteQuery query;
        query.isHuman=human;
        query.radius=radius;
        query.centerInCell=true;
        Coord3D from{fromX*10.0f+5,fromY*10.0f+5,0};
        Coord3D to{toX*10.0f+5,toY*10.0f+5,0};
        return pf->m_groundPlanner->definitelyDisconnected(query,&from,&to);
    }
    bool enclosedGoal(int fromX,int fromY,int toX,int toY,int radius) {
        Pathfinder::GroundRouteQuery query;
        query.radius=radius;
        query.centerInCell=true;
        Coord3D from{fromX*10.0f+5,fromY*10.0f+5,0};
        Coord3D to{toX*10.0f+5,toY*10.0f+5,0};
        return pf->m_groundPlanner->goalIsEnclosed(query,&from,&to);
    }

    static PathfindCell::CellType type(char c) {
        switch (c) {
        case '.': return PathfindCell::CELL_CLEAR;
        case '#': return PathfindCell::CELL_IMPASSABLE;
        case '~': return PathfindCell::CELL_WATER;
        case '^': return PathfindCell::CELL_CLIFF;
        case 'r': return PathfindCell::CELL_RUBBLE;
        default: throw std::invalid_argument("Unknown map cell");
        }
    }
    void setCell(int x, int y, char c) {
        pf->m_map[x][y].setType(type(c));
        pf->invalidateNavigationTopology(IRegion2D{{x, y}, {x, y}});
    }
    void setPinched(int x, int y, bool pinched, bool invalidate=false) {
        auto* cell = pf->getCell(LAYER_GROUND, x, y);
        if (!cell) throw std::invalid_argument("Pinched cell outside fixture");
        cell->setPinched(pinched);
        if (invalidate) pf->invalidateNavigationTopology(IRegion2D{{x,y},{x,y}});
    }
    void movingUnit(int x, int y, bool present) {
        pf->m_map[x][y].setPosUnit(present ? static_cast<ObjectID>(42) : INVALID_ID, {x, y});
    }
    bool hasMovingUnit(int x, int y) const {
        const auto& cell = pf->m_map[x][y];
        return cell.getFlags() == PathfindCell::UNIT_PRESENT_MOVING && cell.getPosUnit() == static_cast<ObjectID>(42);
    }
    void populateMovingUnits(int count) {
        int placed = 0;
        for (int y = 0; y <= pf->m_extent.hi.y && placed < count; ++y)
            for (int x = 0; x <= pf->m_extent.hi.x && placed < count; ++x)
                pf->m_map[x][y].setPosUnit(static_cast<ObjectID>(++placed), {x, y});
        if (placed != count) throw std::invalid_argument("Map too small for occupancy fixture");
    }
    int movingUnitCount() const {
        int count = 0;
        for (int y = 0; y <= pf->m_extent.hi.y; ++y)
            for (int x = 0; x <= pf->m_extent.hi.x; ++x)
                if (pf->m_map[x][y].getFlags() == PathfindCell::UNIT_PRESENT_MOVING &&
                    pf->m_map[x][y].getPosUnit() != INVALID_ID) ++count;
        return count;
    }
    void enqueue(int count) {
        for (int i = 1; i <= count; ++i)
            if (!pf->queueForPath(static_cast<ObjectID>(i))) throw std::logic_error("Queue rejected a request");
    }
    std::size_t pendingRequests() const { return pf->m_pathRequests->size(); }
    bool appendedPathLinksRemainOrdered() {
        auto* path=newInstance(Path);
        Coord3D position{0,0,0};
        path->appendNode(&position,LAYER_GROUND);
        path->markOptimized();
        auto* first=path->getFirstNode();
        for (unsigned i=1;i<8192;++i) {
            auto* previous=path->getLastNode();
            position.x=float(i);
            path->appendNode(&position,LAYER_GROUND);
            auto* tail=path->getLastNode();
            if (tail->getPrevious()!=previous || previous->getNext()!=tail ||
                previous->getNextOptimized()!=tail || tail->getNext() || tail->getNextOptimized()) {
                deleteInstance(path); return false;
            }
        }
        auto* tail=path->getLastNode();
        path->appendNode(&position,LAYER_GROUND); // Optimized duplicate remains a no-op.
        bool valid=path->getFirstNode()==first && path->getLastNode()==tail;
        unsigned count=0;
        for (auto* node=first;node;node=node->getNext())
            valid=(node->getPosition()->x==float(count++)) && valid;
        valid=count==8192 && valid;
        position.x=-1;
        path->prependNode(&position,LAYER_GROUND);
        position.x=8192;
        path->appendNode(&position,LAYER_GROUND); // Prepend made the route unoptimized.
        valid=path->getFirstNode()->getNext()==first && first->getPrevious()==path->getFirstNode() &&
            tail->getNext()==path->getLastNode() && path->getLastNode()->getPrevious()==tail &&
            !path->getLastNode()->getNext() && valid;
        deleteInstance(path);
        return valid;
    }
    bool editedWaypointDirectionsRemainCurrent() {
        auto* path=newInstance(Path);
        for (float x:{0.f,5.f,10.f,20.f}) {
            Coord3D p{x,0,0};
            path->appendNode(&p,LAYER_GROUND);
        }
        auto* first=path->getFirstNode();
        auto* second=first->getNext();
        auto* third=second->getNext();
        auto* tail=path->getLastNode();
        first->setNextOptimized(third);
        second->setNextOptimized(third);
        third->setNextOptimized(tail);
        path->markOptimized();
        Coord3D moved{10,10,0};
        third->setPosition(&moved);
        const auto matches=[](PathNode* node,PathNode* next) {
            Coord2D direction{};
            Real length{};
            if (node->getNextOptimized(&direction,&length)!=next) return false;
            const float dx=next->getPosition()->x-node->getPosition()->x;
            const float dy=next->getPosition()->y-node->getPosition()->y;
            const float expected=std::sqrt(dx*dx+dy*dy);
            return length==expected && direction.x==dx/expected && direction.y==dy/expected;
        };
        bool valid=matches(first,third) && matches(second,third) && matches(third,tail);
        // Materialization and aircraft routing insert raw nodes directly.
        auto* inserted=newInstance(PathNode);
        Coord3D insertedPosition{2,2,0};
        inserted->setPosition(&insertedPosition);
        first->append(inserted);
        inserted->setNextOptimized(third);
        insertedPosition.y=4;
        inserted->setPosition(&insertedPosition);
        moved.x=12;
        third->setPosition(&moved);
        valid=valid && matches(first,third) && matches(second,third) &&
            matches(inserted,third) && matches(third,tail);
        deleteInstance(path);
        return valid;
    }
    void processMissingRequests() { pf->processPathfindQueue(); }
    std::size_t searchStorageBytes() const { return PathfindCell::s_search.storageBytes(); }
    std::size_t cellBytes() const { return sizeof(PathfindCell); }
    void addBridge() {
        pf->invalidateNavigationTopology();
        BridgeInfo info;
        info.from = {45, 65, 0};
        info.to = {105, 65, 0};
        info.fromLeft = {45, 45, 0};
        info.fromRight = {45, 85, 0};
        info.toLeft = {105, 45, 0};
        info.toRight = {105, 85, 0};
        info.bridgeWidth = 40;
        // The empty ThingFactory lets Bridge retain geometry without creating
        // a GenericBridge game object, towers or render resources.
        auto* bridge = newInstance(Bridge)(info, nullptr, AsciiString("FixtureBridge"));
        bridge->setLayer(LAYER_BRIDGE_1);
        bridge->setNext(nullptr);
        static_cast<detail::FlatTerrain*>(TheTerrainLogic)->ownBridge(bridge);
        auto& layer = pf->m_layers[LAYER_BRIDGE_1];
        layer.init(bridge, LAYER_BRIDGE_1);
        layer.allocateCells(&pf->m_extent);
        layer.classifyCells();
    }
    void bridgeDestroyed(bool destroyed) {
        pf->changeBridgeState(LAYER_BRIDGE_1, !destroyed);
    }

    // Exercise the production map-load entry point without an RHI device.
    // The callback supplies a loaded bridge damage state; the resulting
    // navigation layer and route are still updated by the real pathfinder.
    void loadBridgeDamageWithoutGraphics(bool destroyed) {
        if (Graphics::Frame_Device_Ready()) throw std::logic_error("Unexpected graphics device in navigation fixture");
        const Bool oldHeadless = TheWritableGlobalData->m_headless;
        TheWritableGlobalData->m_headless = true;
        {
            detail::HeadlessHeightMap heightMap;
            detail::BridgeDamageTerrain terrain;
            terrain.synchronize = [&] { bridgeDestroyed(destroyed); };
            heightMap.loadRoadsAndBridges(&terrain, false);
        }
        TheTerrainRenderObject = nullptr;
        TheWritableGlobalData->m_headless = oldHeadless;
    }

    void setGroundClearanceMemoEnabled(bool enabled) { pf->m_groundClearanceMemo->enabled = enabled; }
    std::uint64_t groundClearanceEvaluations() const {
        return pf->m_groundClearanceMemo->evaluations;
    }
    Result ground(int fromX, int fromY, int toX, int toY, int diameter = 1, bool crusher = false) {
        Coord3D from = {fromX * 10.0f + 5, fromY * 10.0f + 5, 0};
        Coord3D to = {toX * 10.0f + 5, toY * 10.0f + 5, 0};
        pf->m_cumulativeCellsAllocated = 0;
        Path* path = pf->findGroundPath(&from, &to, diameter, crusher);
        return capture(path, diameter, crusher);
    }
    // Exercise the ordinary object-facing search with a null object.  The
    // fixture supplies the ground surface directly so this stays independent
    // of the game's INI locomotor store while still traversing findPath.
    bool capturedTerrainMatchesNativePermissions() {
        const auto savedIgnore=pf->m_ignoreObstacleID;
        bool matches=true;
        constexpr unsigned surfaces[]{9,10,12,24,8,8,8};
        for (unsigned kind=0;kind<7;++kind) for (unsigned obstacle : {0u,17u,18u})
            for (bool fence : {false,true}) {
                PathfindCell native;
                native.setType(static_cast<PathfindCell::CellType>(kind));
                native.m_obstacleID=static_cast<ObjectID>(obstacle);
                native.m_obstacleIsFence=fence;
                CellSnapshot captured(0,0,0,0,0,[&](int,int) {
                    CellState state;
                    state.occupancy.valid=true;
                    state.terrain=static_cast<unsigned char>(native.getType());
                    state.obstacle=unsigned(native.getObstacleID());
                    state.fence=native.isObstacleFence()!=0;
                    return state;
                });
                for (unsigned mask=0;mask<32;++mask) for (unsigned ignored : {0u,17u,18u})
                    for (bool crusher : {false,true}) {
                        pf->m_ignoreObstacleID=static_cast<ObjectID>(ignored);
                        const bool expected=native.isObstaclePresent(pf->m_ignoreObstacleID) ||
                            (crusher && native.isObstacleFence()) || ((surfaces[kind]&mask)!=0);
                        matches &= (pf->validMovementPosition(crusher,mask,&native)!=0)==expected;
                        matches &= captured.permits({mask,ignored,crusher},0,0)==expected;
                        matches &= !captured.permits({mask,ignored,crusher},1,0);
                        matches &= !pf->validMovementPosition(crusher,mask,nullptr);
                    }
            }
        pf->m_ignoreObstacleID=savedIgnore;
        return matches;
    }
    bool capturedFootprintsMatchNativeTerrain() {
        MovementValidator movement(*pf);
        const auto cells=movement.captureCells(LAYER_GROUND);
        const auto& logical=pf->m_logicalExtent;
        const auto& bounds=pf->m_extent;
        unsigned accepted=0,rejected=0;
        for (int radius : {0,1,2}) for (bool center : {false,true})
            for (bool human : {false,true}) for (bool corridor : {false,true})
                for (unsigned surfaces : {1u,2u,4u,8u,17u}) {
                    const int above=std::max(1,radius+(center?1:0));
                    const WeightedCellQuery query{{surfaces,unsigned(pf->m_ignoreObstacleID),false},
                        radius,above,logical.lo.x,logical.lo.y,logical.hi.x,logical.hi.y,human,corridor};
                    for (int y=bounds.lo.y;y<=bounds.hi.y;y+=17)
                        for (int x=bounds.lo.x;x<=bounds.hi.x;x+=17) {
                            ICoord2D coordinate{x,y};
                            const auto expected=[&] {
                                if (human && pf->checkCellOutsideExtents(coordinate)) return false;
                                auto* cell=pf->getCell(LAYER_GROUND,x,y);
                                if (!cell || (corridor && cell->getPinched()) || !pf->validMovementPosition(false,surfaces,cell)) return false;
                                // Pinned DX9 examineNeighboringCells validates anchor terrain.
                                // checkForMovement scans footprint occupants, not their terrain.
                                return true; // Native null-object queries have no occupancy restriction.
                            }();
                            OccupancyResult traffic;
                            const bool actual=classifyWeightedCell(query,x,y,traffic,[&](int xx,int yy) {
                                const auto* cell=cells.cell(xx,yy);
                                if (!cell) return WeightedTerrainCell{};
                                return WeightedTerrainCell{{static_cast<TerrainKind>(cell->terrain),
                                    cell->obstacle,cell->occupancy.valid,cell->fence},cell->pinched};
                            },[](OccupancyResult&) { return true; });
                            if (actual!=expected) return false;
                            if (actual) ++accepted; else ++rejected;
                        }
                }
        return accepted>0 && rejected>0;
    }
    bool capturedCorridorWidthsMatchOriginalTerrainRules() {
        if (TheGameLogic->getFirstObject()) throw std::runtime_error("Terrain corridor oracle requires no unit objects");
        MovementValidator movement(*pf);
        const auto captured=movement.captureCells(LAYER_GROUND);
        const OccupantSnapshot units;
        // The former recursive terrain policy, retained only as a test oracle.
        // This fixture has no unit objects; crushable-level cases live in the
        // colocated corridor tests and native occupant-capture regression.
        std::function<int(int,int,int,bool)> original;
        original=[&](int x,int y,int diameter,bool crusher) {
            const int radius=diameter/2,above=radius==0?1:radius;
            bool clear=true;
            for (int xx=x-radius;xx<x+above;++xx) {
                const bool edgeX=xx==x-radius || xx==x+above-1;
                for (int yy=y-radius;yy<y+above;++yy) {
                    const bool edgeY=yy==y-radius || yy==y+above-1;
                    if (edgeX && edgeY && radius>1) continue;
                    const auto* cell=pf->getCell(LAYER_GROUND,xx,yy);
                    if (!cell) return 0;
                    if (cell->getType()!=PathfindCell::CELL_CLEAR) {
                        if (cell->getType()==PathfindCell::CELL_OBSTACLE) {
                            if (cell->isObstacleFence()) { if (!crusher) clear=false; }
                            else clear=false;
                        } else clear=false;
                    }
                    if (!clear) break;
                }
            }
            if (clear) return radius==0?1:2*radius;
            if (diameter<2) return 0;
            return original(x,y,diameter-2,crusher);
        };
        unsigned open=0,blocked=0;
        for (int diameter=0;diameter<=8;++diameter) for (bool crusher : {false,true})
            for (int y=pf->m_extent.lo.y;y<=pf->m_extent.hi.y;y+=19)
                for (int x=pf->m_extent.lo.x;x<=pf->m_extent.hi.x;x+=19) {
                    const auto expected=original(x,y,diameter,crusher);
                    const auto saved=corridorClearance(x,y,diameter,crusher,
                        [&](int xx,int yy) { return captured.corridor(xx,yy); },units);
                    if (saved!=expected || pf->clearCellForDiameter(crusher,x,y,LAYER_GROUND,diameter)!=expected) return false;
                    if (expected) ++open;else ++blocked;
                }
        return open>0 && blocked>0;
    }
    bool weightedEndpointObservationsSurviveYield() {
        GroundRoutePlanner planner(*pf);
        Pathfinder::GroundRouteQuery query;
        const Coord3D from{3125,385,0},to{3225,385,0};
        bool reserved=false;
        unsigned observations=0;
        const auto rank=[&](int x,int y,PathfindLayerEnum layer,unsigned)->std::optional<double> {
            if (x!=321 || y!=38 || layer!=LAYER_GROUND) return {};
            ++observations;
            return reserved?std::nullopt:std::optional<double>{1};
        };
        auto task=planner.weightedTask(query,from,to,false,{},rank,ICoord2D{322,38},1);
        unsigned resumes=0;
        while (!task.done()) {
            task.resume();
            if (observations) reserved=true; // Reservation changes between slices.
            if (++resumes>1000000) throw std::runtime_error("Endpoint observation query did not finish");
        }
        auto result=task.take();
        if (!result.path || observations!=1) return false;
        const auto& end=*result.path->getLastNode()->getPosition();
        return end.x==3215 && end.y==385;
    }
    bool capturedFailureRejectsChangedTerrain() {
        auto& planner=*pf->m_groundPlanner;
        Pathfinder::GroundRouteQuery query;
        query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        const Coord3D from{15,15,0},to{615,615,0};
        setCell(61,61,'#');
        planner.prepareCapturedSnapshot(query,false,true);
        planner.captureDynamicSnapshot();planner.prepareCapturedOccupant(query);
        auto slice=std::make_shared<GroundRoutePlanner::CapturedSlice>();
        auto task=planner.weightedTask(query,from,to,false,{},{},{},4096,slice);
        for (unsigned poll=0;!task.done();++poll) {
            if (poll>100000) throw std::runtime_error("Captured failure did not finish");
            task.resume();
            if (slice->work) { slice->work();slice->work={}; }
        }
        setCell(61,61,'.');
        auto result=task.take();
        return !result.path && result.waypoints.empty() && result.staleTerrain;
    }
    bool capturedRouteHandlesRegionalEdits(bool intersectsRoute,bool withBridge=false) {
        if (withBridge) addBridge();
        auto& planner=*pf->m_groundPlanner;
        Pathfinder::GroundRouteQuery query;
        query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;query.centerInCell=true;
        const Coord3D from{15,15,0},to{615,615,0};
        auto* reference=planner.findWeighted(query,&from,&to);
        if (!reference) return false;
        const auto expected=capture(reference,0,false);
        planner.prepareCapturedSnapshot(query,false,true);
        planner.captureDynamicSnapshot();planner.prepareCapturedOccupant(query);
        auto slice=std::make_shared<GroundRoutePlanner::CapturedSlice>();
        auto task=planner.weightedTask(query,from,to,false,{},{},{},31,slice);
        unsigned slices=0;
        while (!task.done()) {
            task.resume();
            if (slice->work) {
                slice->work();slice->work={};++slices;
                if (slices==1 || (!intersectsRoute && slices%7==0)) {
                    const auto x=intersectsRoute?30:50,y=intersectsRoute?30:5;
                    setCell(x,y,(slices&1)?'#':'.');
                }
            }
            if (slices>10000) throw std::runtime_error("Regional edits starved a captured route");
        }
        auto result=task.take();
        // A changed region must not abort this immutable search. Publication
        // decides whether its completed route still has the same terrain.
        if (result.waypoints.empty() || slices<2)
            throw std::runtime_error("Regional edit cancelled continuation before route publication");
        auto* path=planner.materializeCapturedResult(query,std::move(result));
        if (intersectsRoute) {
            const bool rejected=path==nullptr;
            if (path) deleteInstance(path);
            return rejected;
        }
        if (!path) throw std::runtime_error("Unrelated edit rejected completed route publication");
        const auto actual=capture(path,0,false);
        if (actual.raw!=expected.raw || actual.optimized!=expected.optimized)
            throw std::runtime_error("Regional route mismatch: raw="+actual.raw+" expected="+expected.raw+
                " optimized="+actual.optimized+" expected="+expected.optimized);
        return true;
    }

    bool capturedDispatchServicesRequestsInArrivalOrder(bool continuedArrivals=false,bool boundedScratch=false) {
        GroundRoutePlanner planner(*pf);
        Pathfinder::GroundRouteQuery query;
        query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        query.centerInCell=true;
        const Coord3D from{15,15,0},to{615,615,0};
        planner.prepareCapturedSnapshot(query,false,true);
        planner.captureDynamicSnapshot();
        planner.prepareCapturedOccupant(query);
        // IDs deliberately differ from request arrival order. Include the
        // object-less API in the same service cycle.
        const std::array<ObjectID,4> initial{ObjectID(30),ObjectID(10),ObjectID(20),INVALID_ID};
        std::vector<ObjectID> arrival(initial.begin(),initial.end());
        constexpr unsigned budget=31;
        for (const auto id:arrival) {
            auto slice=std::make_shared<GroundRoutePlanner::CapturedSlice>();
            slice->moverId=id;
            auto task=planner.weightedTask(query,from,to,false,{},{},{},budget,slice);
            if (id==INVALID_ID) planner.state_->standalone.emplace(std::move(task));
            else planner.state_->pending.emplace(id,std::move(task));
        }
        std::vector<std::string> raw(arrival.size()),optimized(arrival.size());
        unsigned served=0;
        for (unsigned round=0;round<10000;++round) {
            bool complete=true;
            for (unsigned i=0;i<arrival.size();++i) {
                auto& task=arrival[i]==INVALID_ID?*planner.state_->standalone:
                    planner.state_->pending.at(arrival[i]);
                if (!task.done()) { task.resume();complete=false; }
                if (boundedScratch && planner.state_->leasedScratch>4)
                    throw std::runtime_error("Waiting captured requests allocated more than four search workspaces");
                if (task.done() && raw[i].empty()) {
                    auto result=task.take();
                    if (!result.path && !result.waypoints.empty())
                        result.path.reset(planner.materializeCapturedResult(query,std::move(result)));
                    if (!result.path) throw std::runtime_error("Fair dispatch lost a captured route");
                    const auto route=capture(result.path.release(),0,false);
                    raw[i]=route.raw;optimized[i]=route.optimized;
                }
            }
            if (complete) {
                if (served<initial.size()*2) return false;
                for (unsigned i=1;i<arrival.size();++i)
                    if (raw[i]!=raw[0] || optimized[i]!=optimized[0]) return false;
                return !raw[0].empty();
            }
            planner.advanceCapturedSlice();
            if (!planner.state_->completedSlice) continue;
            const auto& completed=*planner.state_->completedSlice;
            if (served<initial.size()*2 && completed.moverId!=initial[served%initial.size()])
                throw std::runtime_error("Captured dispatch ignored arrival order or starved a ready request");
            ++served;
            planner.advanceCapturedSlice(); // A second call cannot cross commit.
            const auto before=pf->m_cumulativeCellsAllocated;
            planner.commitCapturedSlice();
            if (unsigned(pf->m_cumulativeCellsAllocated-before)>budget)
                throw std::runtime_error("Captured batch exceeded its single-slice allowance");
            if (continuedArrivals && served<=16) {
                // New orders must not extend the current service cycle forever
                // and deny another slice to already-waiting long searches.
                const auto id=ObjectID(1000+served);
                auto slice=std::make_shared<GroundRoutePlanner::CapturedSlice>();
                slice->moverId=id;
                planner.state_->pending.emplace(id,
                    planner.weightedTask(query,from,to,false,{},{},{},budget,slice));
                arrival.push_back(id);raw.emplace_back();optimized.emplace_back();
            }
        }
        throw std::runtime_error("Captured dispatch did not eventually service every request");
    }
    std::string capturedNativeRoutes() {
        GroundRoutePlanner planner(*pf);

        Pathfinder::GroundRouteQuery query;
        query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        const Coord3D from{1165,3145,0},to{45,2195,0};
        const DestinationRankQuery target{4,219,LAYER_GROUND,true,1.0f,0.0f};
        const auto rank=[&](int x,int y,PathfindLayerEnum layer,unsigned cost) {
            return rankDestination(target,x,y,unsigned(layer),cost,[](int,int,unsigned){return true;});
        };
        planner.prepareCapturedSnapshot(query,false,true);
        planner.prepareCapturedOccupant(query);
        std::vector<GroundRoutePlanner::WeightedTask> tasks;
        // Credits now include every phase sample, not just frontier pops.
        // Tiny-credit kernel coverage lives in captured phase cursor tests;
        // exercise multiple native dispatches here without millions of polls.
        constexpr unsigned sliceBudget=4096;
        for (unsigned i=0;i<8;++i) {
            auto slice=std::make_shared<GroundRoutePlanner::CapturedSlice>();
            slice->rank=target;
            tasks.push_back(planner.weightedTask(query,from,to,false,{},rank,ICoord2D{4,219},sliceBudget,slice));
        }
        unsigned rounds=0;
        for (;;) {
            bool complete=true;
            for (auto& task:tasks) if (!task.done()) {task.resume();complete=false;}
            if (complete) break;
            for (std::size_t i=0;i<tasks.size();++i) {
                if (tasks[i].slice->work) {
                    tasks[i].slice->work();
                    if (tasks[i].slice->workCount>sliceBudget)
                        throw std::runtime_error("Native captured slice exceeded its work budget");
                }
            }
            for (auto& task:tasks) task.slice->work={};
            if (++rounds>100000) throw std::runtime_error("Native slice query did not finish");
        }
        std::string reference;
        if (rounds<2) throw std::runtime_error("Native captured route did not exercise multiple dispatches");
        for (auto& task:tasks) {
            auto result=task.take();
            if (!result.path && !result.waypoints.empty()) {
                auto* materialized=planner.materializeCapturedResult(query,std::move(result));
                result.path.reset(materialized);
            }
            if (!result.path) throw std::runtime_error("Native slice route missing");
            const auto route=capture(result.path.release(),0,false);
            if (reference.empty()) reference=route.raw;
            else if (reference!=route.raw) throw std::runtime_error("Native slice slots diverged");
        }
        auto slice=std::make_shared<GroundRoutePlanner::CapturedSlice>();slice->rank=target;
        auto cancelled=planner.weightedTask(query,from,to,false,{},rank,ICoord2D{4,219},31,slice);
        cancelled.resume();
        if (!slice->work) throw std::runtime_error("Native query did not prepare a captured slice");
        planner.invalidate();
        cancelled.resume();
        if (slice->work || cancelled.take().path) throw std::runtime_error("Stale native slice was not cancelled");
        return reference;
    }
    bool weightedRouteMaterializationStaysWithinSlices() {
        const auto captured=captureWeightedRoute(116,314,4,219);
        const auto& graph=*captured.graph;
        GroundRoutePlanner planner(*pf);
        Pathfinder::GroundRouteQuery query;
        const Coord3D from{1165,3145,0},to{45,2195,0};
        const auto destination=[](int x,int y,PathfindLayerEnum layer) {
            return x==4 && y==219 && layer==LAYER_GROUND;
        };
        std::string raw,optimized;
        std::uint64_t expectedWork=0;
        for (unsigned budget:{50000u,31u,7u}) {
            auto task=planner.weightedTask(query,from,to,false,destination,{},{},budget);
            std::uint64_t total=0,resumes=0;
            while (!task.done()) {
                const auto before=pf->m_cumulativeCellsAllocated;
                task.resume();
                const auto used=std::uint64_t(pf->m_cumulativeCellsAllocated-before);
                if (used>budget) throw std::runtime_error("Native slice exceeded budget: "+std::to_string(used)+"/"+std::to_string(budget));
                if (++resumes>1000000) throw std::runtime_error("Native sliced route did not finish");
                total+=used;
            }
            auto owned=task.take();
            if (!owned.path) throw std::runtime_error("Native sliced route missing");
            unsigned previous=graph.start();
            bool escaping=graph.decode(previous).escaping;
            for (auto* node=owned.path->getFirstNode()->getNext();node;node=node->getNext()) {
                ICoord2D cell;
                pf->worldToCell(node->getPosition(),&cell);
                const unsigned layer=node->getLayer();
                escaping=escaping && !graph.passable(cell.x,cell.y,layer);
                const auto current=graph.encode(cell.x,cell.y,layer,escaping);
                bool legal=false;
                graph.neighbors(previous,[&](unsigned next,unsigned) { legal=legal || next==current; });
                if (!legal) throw std::runtime_error("Native sliced route violates weighted terrain graph");
                previous=current;
            }
            if (previous!=graph.goal()) throw std::runtime_error("Native sliced route missed its destination");
            // capture's legacy ground-diameter oracle has a different contract;
            // weighted edges are validated against the captured graph above.
            auto route=capture(owned.path.release(),0,false);
            if (!route.found || route.rawGroundCells.size()<100)
                throw std::runtime_error("Native sliced route too short");
            if (!expectedWork) { expectedWork=total;raw=route.raw;optimized=route.optimized; }
            else if (total!=expectedWork || raw!=route.raw || optimized!=route.optimized)
                throw std::runtime_error("Native route changed with slice size: "+std::to_string(budget));
        }
        auto* pathPool=TheMemoryPoolFactory->findMemoryPool("PathPool");
        auto* nodePool=TheMemoryPoolFactory->findMemoryPool("PathNodePool");
        const auto pathsBefore=pathPool->getUsedBlockCount(),nodesBefore=nodePool->getUsedBlockCount();
        auto cancelled=planner.weightedTask(query,from,to,false,destination,{},{},7);
        unsigned resumes=0;
        while (!cancelled.done() && pathPool->getUsedBlockCount()==pathsBefore) {
            cancelled.resume();
            if (++resumes>1000000) throw std::runtime_error("Cancellation never reached materialization");
        }
        if (cancelled.done() || nodePool->getUsedBlockCount()<=nodesBefore)
            throw std::runtime_error("Cancellation did not capture a partially materialized path");
        cancelled={};
        return expectedWork>0 && pathPool->getUsedBlockCount()==pathsBefore && nodePool->getUsedBlockCount()==nodesBefore;
    }
    std::array<std::uint64_t,3> weightedContinuationSlices() {
        GroundRoutePlanner planner(*pf);
        Pathfinder::GroundRouteQuery query;
        query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        Coord3D from{45,2195,0},to=from;
        const auto originalIgnore=pf->m_ignoreObstacleID;
        pf->setIgnoreObstacleID(static_cast<ObjectID>(91));
        const auto reject=[this](int,int,PathfindLayerEnum,unsigned)->std::optional<double> {
            if (pf->m_ignoreObstacleID!=static_cast<ObjectID>(91))
                throw std::runtime_error("Suspended query inherited another query's ignored obstacle");
            return {};
        };
        auto task=planner.weightedTask(query,from,to,false,{},reject,{});
        pf->setIgnoreObstacleID(static_cast<ObjectID>(29));
        // Parameters must belong to the continuation, not these caller variables.
        query.radius=-1;from.x=to.x=-1000;
        std::uint64_t total=0,maximum=0,resumes=0;
        while (!task.done()) {
            const auto before=pf->m_cumulativeCellsAllocated;
            task.resume();
            if (pf->m_ignoreObstacleID!=static_cast<ObjectID>(29))
                throw std::runtime_error("Suspended query leaked its ignored obstacle to the caller");
            const auto used=std::uint64_t(pf->m_cumulativeCellsAllocated-before);
            total+=used;maximum=std::max(maximum,used);++resumes;
            if (resumes>100) throw std::runtime_error("Native continuation did not finish");
            if (resumes==1) {
                Pathfinder::GroundRouteQuery other;
                const Coord3D point{45,2195,0};
                auto* path=planner.findWeighted(other,&point,&point);
                if (!path) throw std::runtime_error("Interleaved native query lost its route");
                deleteInstance(path);
            }
        }
        if (task.take().path) throw std::runtime_error("Rejected endpoint produced a route");
        Pathfinder::GroundRouteQuery fresh;
        const Coord3D point{45,2195,0};
        pf->setIgnoreObstacleID(static_cast<ObjectID>(91));
        auto cancelled=planner.weightedTask(fresh,point,point,false,{},reject,{});
        pf->setIgnoreObstacleID(static_cast<ObjectID>(29));
        if (!cancelled.resume()) throw std::runtime_error("Cancellation query did not suspend");
        cancelled={};
        pf->setIgnoreObstacleID(static_cast<ObjectID>(91));
        auto failing=planner.weightedTask(fresh,point,point,false,{},
            [](int,int,PathfindLayerEnum,unsigned)->std::optional<double> {
                throw std::runtime_error("expected weighted exception");
            },{});
        pf->setIgnoreObstacleID(static_cast<ObjectID>(29));
        bool caught=false;
        try { failing.resume(); }
        catch (const std::runtime_error& error) { caught=std::string(error.what())=="expected weighted exception"; }
        if (!caught || pf->m_ignoreObstacleID!=static_cast<ObjectID>(29))
            throw std::runtime_error("Exceptional query did not restore caller policy");
        auto* replacement=planner.findWeighted(fresh,&point,&point);
        if (!replacement) throw std::runtime_error("Cancelled native scratch corrupted replacement");
        deleteInstance(replacement);
        if (pf->m_ignoreObstacleID!=static_cast<ObjectID>(29))
            throw std::runtime_error("Cancellation changed the caller's ignored obstacle");
        pf->setIgnoreObstacleID(originalIgnore);
        return {total,maximum,resumes};
    }
    bool obsoleteWeightedTasksAreCancelled() {
        GroundRoutePlanner planner(*pf);
        Pathfinder::GroundRouteQuery query;
        const Coord3D point{45,2195,0};
        const auto reject=[](int,int,PathfindLayerEnum,unsigned)->std::optional<double> { return {}; };
        auto pending=planner.weightedTask(query,point,point,false,{},reject,{});
        if (!pending.resume()) return false;
        auto* cell=pf->getCell(LAYER_GROUND,4,219);
        const auto oldType=cell->getType();
        cell->setType(PathfindCell::CELL_CLIFF);
        planner.invalidate(IRegion2D{{4,219},{4,219}});
        const auto work=pf->m_cumulativeCellsAllocated;
        const bool stopped=!pending.resume() && !pending.take().path && pf->m_cumulativeCellsAllocated==work;
        cell->setType(oldType);
        planner.invalidate();
        auto ready=planner.weightedTask(query,point,point,false,{},{},{});
        while (ready.resume()) {}
        planner.invalidate();
        const bool rejected=!ready.take().path;
        auto unstarted=planner.weightedTask(query,point,point,false,{},reject,{});
        planner.reset();
        const bool reset=!unstarted.resume() && !unstarted.take().path;
        auto* replacement=planner.findWeighted(query,&point,&point);
        const bool recovered=replacement!=nullptr;
        if (replacement) deleteInstance(replacement);
        return stopped && rejected && reset && recovered;
    }
    std::pair<std::uint64_t,unsigned> weightedEndpointRankingWork() {
        GroundRoutePlanner planner(*pf);
        Pathfinder::GroundRouteQuery query;
        query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        const Coord3D from{45,2195,0},to{45,2195,0};
        unsigned evaluations=0;
        const auto before=pf->m_cumulativeCellsAllocated;
        // Reject every endpoint, including the clear exact goal. With no bound,
        // this must exhaust the real terrain graph and rank its reachable cells.
        auto* path=planner.findWeighted(query,&from,&to,false,{},
            [&](int,int,PathfindLayerEnum,unsigned)->std::optional<double> {
                ++evaluations;return {};
            });
        const bool missing=path==nullptr;
        if (path) deleteInstance(path);
        // One evaluation checks exact-goal eligibility before search begins.
        if (!missing) throw std::runtime_error("Rejected endpoint produced a native route");
        return {std::uint64_t(pf->m_cumulativeCellsAllocated-before),evaluations};
    }
    CapturedRouteCase captureWeightedRoute(int fromX,int fromY,int toX,int toY) {
        MovementValidator movement(*pf);
        std::vector<CapturedLayer> layers;
        for (int layer=LAYER_GROUND;layer<=LAYER_LAST;++layer) {
            auto cells=movement.captureCells(static_cast<PathfindLayerEnum>(layer));
            if (!cells.width()) continue;
            layers.push_back({std::make_shared<const CellSnapshot>(std::move(cells)),{}});
        }
        CapturedGraphQuery query;
        query.movement.terrain.surfaces=LOCOMOTORSURFACE_GROUND;
        query.movement.terrain.ignoredObstacle=unsigned(pf->m_ignoreObstacleID);
        query.movement.left=pf->m_logicalExtent.lo.x;query.movement.top=pf->m_logicalExtent.lo.y;
        query.movement.right=pf->m_logicalExtent.hi.x;query.movement.bottom=pf->m_logicalExtent.hi.y;
        query.movement.restrictToBounds=true;
        query.occupancy.cellsAbove=1;
        query.checkOccupants=false;
        query.startX=fromX;query.startY=fromY;query.goalX=toX;query.goalY=toY;
        query.startLayer=query.goalLayer=LAYER_GROUND;
        CapturedRouteCase result;
        result.graph=std::make_unique<CapturedWeightedGraph>(std::move(layers),
            std::make_shared<const OccupantSnapshot>(),query);
        GroundRoutePlanner planner(*pf);
        Pathfinder::GroundRouteQuery native;
        native.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        Coord3D from{fromX*10.0f+5,fromY*10.0f+5,0},to{toX*10.0f+5,toY*10.0f+5,0};
        auto* path=planner.findWeighted(native,&from,&to);
        if (!path) throw std::runtime_error("Native weighted reference route missing");
        result.hierarchy=planner.prepareLiveHierarchy(native);
        bool escaping=result.graph->decode(result.graph->start()).escaping;
        for (auto* node=path->getFirstNode();node;node=node->getNext()) {
            ICoord2D cell;pf->worldToCell(node->getPosition(),&cell);
            const unsigned layer=node->getLayer();
            escaping=escaping && !result.graph->passable(cell.x,cell.y,layer);
            result.reference.push_back(result.graph->encode(cell.x,cell.y,layer,escaping));
        }
        deleteInstance(path);
        return result;
    }
    bool groundWaypointsAllowLookAhead() {
        GroundRoutePlanner planner(*pf);
        Pathfinder::GroundRouteQuery query;
        query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        query.centerInCell=true;
        Coord3D from{25,25,0},to{95,95,0};
        auto* path=planner.findWeighted(query,&from,&to);
        if (!path) return false;
        unsigned count=0;
        bool allowed=true;
        for (auto* node=path->getFirstNode()->getNext();node;node=node->getNext()) {
            ++count;
            allowed=allowed && node->getCanOptimize();
        }
        deleteInstance(path);
        return allowed && count>2;
    }
    Result reconstructedWaypointMetadata(bool cliff,bool captured) {
        GroundRoutePlanner planner(*pf);
        Pathfinder::GroundRouteQuery query;
        query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND |
            (cliff?LOCOMOTORSURFACE_CLIFF:LOCOMOTORSURFACE_RUBBLE);
        query.centerInCell=true;
        const Coord3D from{15,25,0},to{125,25,0};
        if (!captured) return capture(planner.findWeighted(query,&from,&to),0,false);
        planner.prepareCapturedSnapshot(query,false,true);
        planner.captureDynamicSnapshot();planner.prepareCapturedOccupant(query);
        auto slice=std::make_shared<GroundRoutePlanner::CapturedSlice>();
        auto task=planner.weightedTask(query,from,to,false,{},{},{},31,slice);
        for (unsigned poll=0;!task.done();++poll) {
            if (poll>10000) throw std::runtime_error("Waypoint metadata query did not complete");
            task.resume();
            if (slice->work) { slice->work();slice->work={}; }
        }
        auto result=task.take();
        if (!result.path && !result.waypoints.empty())
            result.path.reset(planner.materializeCapturedResult(query,std::move(result)));
        return capture(result.path.release(),0,false);
    }
    Result normal(int fromX, int fromY, int toX, int toY, bool allowRubble = false) {
        Coord3D from = {fromX * 10.0f + 5, fromY * 10.0f + 5, 0};
        Coord3D to = {toX * 10.0f + 5, toY * 10.0f + 5, 0};
        LocomotorSet groundLoco;
        groundLoco.m_validLocomotorSurfaces = LOCOMOTORSURFACE_GROUND;
        if (allowRubble) groundLoco.m_validLocomotorSurfaces |= LOCOMOTORSURFACE_RUBBLE;
        pf->m_cumulativeCellsAllocated = 0;
        Path* path = pf->findPath(nullptr, groundLoco, &from, &to);
        return capture(path, 1, false);
    }
    std::array<std::uint64_t,2> hierarchyStats() const {
        return {pf->m_groundPlanner->state_->hierarchySearches,
            pf->m_groundPlanner->state_->hierarchyRefinements};
    }
    std::vector<unsigned> coarseForFootprint(int fromX,int fromY,int toX,int toY,int radius) {
        Pathfinder::GroundRouteQuery query;
        query.radius=radius;query.centerInCell=true;
        HpaClusterSearch search;
        search.begin(pf->m_groundPlanner->prepareLiveHierarchy(query),fromX,fromY,toX,toY);
        while (search.status()==RouteSearchStatus::Searching) search.advance(128);
        return search.result().nodes;
    }
    std::vector<std::uint8_t> cachedTerrainForFootprint(int radius,bool centered,unsigned surfaces) {
        Pathfinder::GroundRouteQuery query;
        query.radius=radius;query.centerInCell=centered;query.acceptableSurfaces=surfaces;
        std::span<const std::uint8_t> terrain;
        pf->m_groundPlanner->prepareLiveHierarchy(query,&terrain);
        return {terrain.begin(),terrain.end()};
    }
    Result hierarchicalForFootprint(int fromX,int fromY,int toX,int toY,int radius,unsigned budget) {
        Pathfinder::GroundRouteQuery query;
        query.radius=radius;query.centerInCell=true;
        Coord3D from{fromX*10.0f+5,fromY*10.0f+5,0},to{toX*10.0f+5,toY*10.0f+5,0};
        auto task=pf->m_groundPlanner->weightedTask(query,from,to,false,{},{},{},budget);
        pf->m_cumulativeCellsAllocated=0;
        unsigned resumes=0;
        while (!task.done()) {
            const auto before=pf->m_cumulativeCellsAllocated;
            task.resume();
            if (pf->m_cumulativeCellsAllocated-before>budget)
                throw std::runtime_error("HPA refinement exceeded its deterministic slice");
            if (++resumes>1000000) throw std::runtime_error("HPA refinement did not finish");
        }
        return capture(task.take().path.release(),2*radius+1,false);
    }
    Result boundedRejoin(int toX,int toY,unsigned neighborLimit,unsigned budget) {
        Pathfinder::GroundRouteQuery query;
        query.radius=0;query.centerInCell=true;
        const Coord3D from{165,165,0};
        const auto destination=[=](int x,int y,PathfindLayerEnum layer) {
            return x==toX && y==toY && layer==LAYER_GROUND;
        };
        auto task=pf->m_groundPlanner->weightedTask(query,from,from,false,destination,{},{},budget,{},neighborLimit);
        pf->m_cumulativeCellsAllocated=0;
        while (task.resume()) {}
        return capture(task.take().path.release(),1,false);
    }
    Result closestForFootprint(int fromX,int fromY,int toX,int toY,int radius) {
        Pathfinder::GroundRouteQuery query;
        query.radius=radius;query.centerInCell=true;
        Coord3D from{fromX*10.0f+5,fromY*10.0f+5,0},to{toX*10.0f+5,toY*10.0f+5,0};
        pf->m_cumulativeCellsAllocated=0;
        auto* path=pf->m_groundPlanner->findWeighted(query,&from,&to,false,{},
            [=](int x,int y,PathfindLayerEnum,unsigned)->std::optional<double> {
                const double dx=x-toX,dy=y-toY;
                return dx*dx+dy*dy;
            },ICoord2D{toX,toY});
        return capture(path,2*radius+1,false);
    }
    bool individualRoutesMayCrossPinchedGround() {
        setPinched(20, 0, true);
        const auto result=normal(2, 0, 36, 0);
        return result.found &&
            std::find(result.rawGroundCells.begin(), result.rawGroundCells.end(),
                std::array<int,2>{20,0}) != result.rawGroundCells.end();
    }
    bool zeroCoordinateRequestsAreRejected() {
        Coord3D from{15,15,0}, zero{0,0,0};
        LocomotorSet groundLoco;
        groundLoco.m_validLocomotorSurfaces=LOCOMOTORSURFACE_GROUND;
        auto* normalPath=pf->findPath(nullptr,groundLoco,&from,&zero);
        auto* groundPath=pf->findGroundPath(&from,&zero,1,false);
        const bool rejected=!normalPath && !groundPath;
        if (normalPath) deleteInstance(normalPath);
        if (groundPath) deleteInstance(groundPath);
        return rejected;
    }
    bool invalidPublicQueriesReturnSafely() {
        LocomotorSet groundLoco;
        groundLoco.m_validLocomotorSurfaces=LOCOMOTORSURFACE_GROUND;
        Coord3D from{15,15,0},to{25,25,0},unchanged{1,2,3};
        ObjectID bridge=static_cast<ObjectID>(42);
        const bool quickFrom=!pf->clientSafeQuickDoesPathExist(groundLoco,nullptr,&to);
        const bool quickTo=!pf->clientSafeQuickDoesPathExist(groundLoco,&from,nullptr);
        const bool uiFrom=!pf->clientSafeQuickDoesPathExistForUI(groundLoco,nullptr,&to);
        const bool uiTo=!pf->clientSafeQuickDoesPathExistForUI(groundLoco,&from,nullptr);
        const bool bridgeFrom=!pf->findBrokenBridge(groundLoco,nullptr,&to,&bridge) && bridge==INVALID_ID;
        const bool bridgeTo=!pf->findBrokenBridge(groundLoco,&from,nullptr,&bridge);
        const bool bridgeOutput=!pf->findBrokenBridge(groundLoco,&from,&to,nullptr);
        const bool adjust=!pf->adjustDestination(nullptr,groundLoco,&unchanged);
        const bool landing=!pf->adjustToLandingDestination(nullptr,&unchanged);
        const bool aircraft=!pf->getAircraftPath(nullptr,&to);
        const bool attack=!pf->findAttackPath(nullptr,groundLoco,&from,nullptr,&to,nullptr);
        const bool goal=!pf->goalPosition(nullptr,&unchanged);
        const bool safe=quickFrom && quickTo && uiFrom && uiTo && bridgeFrom && bridgeTo &&
            bridgeOutput && adjust && landing && aircraft && attack && goal;
        pf->snapClosestGoalPosition(nullptr,&unchanged);
        return safe && unchanged.x==1 && unchanged.y==2 && unchanged.z==3;
    }
private:
    struct SegmentContract {
        int pathDiameter;
        bool crusher;
        bool requireFullClearance;
        bool legal = true;
    };

    static Int validateGroundSegment(Pathfinder* pathfinder, PathfindCell*, PathfindCell* to,
                                     Int toX, Int toY, void* userData) {
        auto* contract = static_cast<SegmentContract*>(userData);
        if (!to) {
            contract->legal = false;
            return 1;
        }
        const Int clearDiameter = pathfinder->clearCellForDiameter(
            contract->crusher, toX, toY, to->getLayer(), contract->pathDiameter);
        if ((contract->requireFullClearance && clearDiameter != contract->pathDiameter) ||
            (!contract->requireFullClearance && clearDiameter <= 0) || to->getPinched()) {
            contract->legal = false;
            return 1;
        }
        return 0;
    }

    bool macroSegmentLegal(const Coord3D& from, PathfindLayerEnum layer,
                           const Coord3D& to, int pathDiameter, bool crusher) {
        SegmentContract contract{pathDiameter, crusher, true};
        const Int result = pf->iterateCellsAlongLine(
            from, to, layer, &World::validateGroundSegment, &contract);
        return result == 0 && contract.legal;
    }

    bool groundNeighborZoneLegal(int x, int y, PathfindLayerEnum layer) const {
        if (layer != LAYER_GROUND) return true;
        const auto* cell = pf->getCell(layer, x, y);
        return cell && cell->getType() != PathfindCell::CELL_IMPASSABLE;
    }

    bool adjacentSegmentLegal(const ICoord2D& from, const ICoord2D& to,
                              PathfindLayerEnum layer, int pathDiameter, bool crusher, bool escaping=false) {
        auto* destination = pf->getCell(layer, to.x, to.y);
        const bool invalidDestination=!destination || destination->getType()!=PathfindCell::CELL_CLEAR ||
            !groundNeighborZoneLegal(to.x,to.y,layer) ||
            pf->clearCellForDiameter(crusher,to.x,to.y,layer,pathDiameter)<=0;
        if (invalidDestination) return false;

        const int dx = to.x - from.x;
        const int dy = to.y - from.y;
        if (escaping && destination->getPinched()) {
            const auto* source=pf->getCell(layer,from.x,from.y);
            return source && source->getType()==PathfindCell::CELL_CLEAR && source->getPinched() &&
                std::abs(dx)+std::abs(dy)==1;
        }
        if (dx != 0 && dy != 0) {
            // Ground A* permits a diagonal when at least one of its two
            // cardinal neighbors is a valid movement cell.  Mirror that
            // neighborFlags contract instead of imposing a stricter corner
            // rule than the production search.
            const ICoord2D sideA{from.x + dx, from.y};
            const ICoord2D sideB{from.x, from.y + dy};
            const auto sideLegal = [&](const ICoord2D& side) {
                auto* cell = pf->getCell(layer, side.x, side.y);
                return cell && cell->getType() == PathfindCell::CELL_CLEAR &&
                    groundNeighborZoneLegal(side.x, side.y, layer);
            };
            if (!sideLegal(sideA) && !sideLegal(sideB))
                return false;
        }
        return true;
    }

    Result capture(Path* path, int pathDiameter, bool crusher) {
        Result result;
        result.clean = true;
        result.found = path != nullptr;
        result.legal = path == nullptr;
        result.cells = pf->m_cumulativeCellsAllocated;
        if (path) {
            auto serialize = [](PathNode* node, bool optimized) {
                std::ostringstream out;
                out.imbue(std::locale::classic());
                out << std::setprecision(std::numeric_limits<Real>::max_digits10);
                int count = 0;
                while (node) {
                    if (++count > 10000) throw std::logic_error("Cyclic path");
                    if (count > 1) out << ' ';
                    const auto& p = *node->getPosition();
                    out << p.x << ',' << p.y << ',' << p.z << ':' << int(node->getLayer()) << ':' << node->getCanOptimize();
                    node = optimized ? node->getNextOptimized() : node->getNext();
                }
                return out.str();
            };
            result.raw = serialize(path->getFirstNode(), false);
            result.optimized = serialize(path->getFirstNode(), true);
            for (auto* node = path->getFirstNode(); node; node = node->getNext()) {
                result.rawOptimizationFlags.push_back(node->getCanOptimize()!=false);
                if (node->getLayer() == LAYER_GROUND) {
                    ICoord2D cell;
                    pf->worldToCell(node->getPosition(), &cell);
                    result.rawGroundCells.push_back({cell.x, cell.y});
                }
            }
            for (auto* node = path->getFirstNode(); node && node->getNextOptimized();
                 node = node->getNextOptimized()) {
                ICoord2D start, end;
                pf->worldToCell(node->getPosition(), &start);
                pf->worldToCell(node->getNextOptimized()->getPosition(), &end);
                result.optimizedEdges = result.optimizedEdges ||
                    abs(end.x - start.x) > 1 || abs(end.y - start.y) > 1;
            }
            result.legal = true;
            ICoord2D initialCell;
            pf->worldToCell(path->getFirstNode()->getPosition(),&initialCell);
            const auto* initial=pf->getCell(path->getFirstNode()->getLayer(),initialCell.x,initialCell.y);
            bool escaping=initial && initial->getType()==PathfindCell::CELL_CLEAR && initial->getPinched();
            for (auto* node = path->getFirstNode(); node && node->getNext(); node = node->getNext()) {
                auto* next = node->getNext();
                if (node->getLayer() != next->getLayer())
                    continue; // A same-coordinate bridge layer transition is legal by construction.
                ICoord2D startCell{};
                ICoord2D endCell{};
                pf->worldToCell(node->getPosition(), &startCell);
                pf->worldToCell(next->getPosition(), &endCell);
                if (!pf->getCell(node->getLayer(), startCell.x, startCell.y) ||
                    !pf->getCell(next->getLayer(), endCell.x, endCell.y))
                    continue; // The first node can intentionally be clipped outside the map.
                const int deltaX = endCell.x - startCell.x;
                const int deltaY = endCell.y - startCell.y;
                const int dx = deltaX < 0 ? -deltaX : deltaX;
                const int dy = deltaY < 0 ? -deltaY : deltaY;
                const bool macroEdge = dx > 1 || dy > 1;
                result.macroEdges = result.macroEdges || macroEdge;
                const bool legal = macroEdge
                    ? macroSegmentLegal(*node->getPosition(), node->getLayer(),
                                        *next->getPosition(), pathDiameter, crusher)
                    : adjacentSegmentLegal(startCell, endCell, node->getLayer(),
                                           pathDiameter, crusher, escaping);
                if (const auto* destination=pf->getCell(next->getLayer(),endCell.x,endCell.y);
                    !destination || !destination->getPinched()) escaping=false;
                if (!legal) {
                    result.legal = false;
                    break;
                }
            }
            deleteInstance(path);
        }
        for (int x = 0; x <= pf->m_extent.hi.x; ++x)
            for (int y = 0; y <= pf->m_extent.hi.y; ++y) {
                auto& cell = pf->m_map[x][y];
                result.clean = result.clean && (!cell.hasInfo() ||
                    cell.getPosUnit() != INVALID_ID ||
                    cell.getGoalUnit() != INVALID_ID ||
                    cell.getGoalAircraft() != INVALID_ID);
                auto* bridgeCell = pf->m_layers[LAYER_BRIDGE_1].getCell(x, y);
                if (bridgeCell) result.clean = result.clean && !bridgeCell->hasInfo();
            }


        return result;
    }
};
}
