#pragma once
#include <memory>
#include <array>
#include <vector>
#include <span>
#include <unordered_map>
#include "engine/navigation/movement/movement_validator.h"
#include "engine/navigation/path/ground_path_builder.h"
namespace navigation {
template<class T> class Continuation;
namespace testing { class World; class Simulation; }
// Owns weighted ground searches, immutable captures and serial slice accounting.
// All query kinds share the same production search and movement policies.
class GroundRoutePlanner {
    friend class testing::World;
    friend class testing::Simulation;
    struct State;
    std::unique_ptr<State> state_;
    Pathfinder& world_;
    MovementValidator movement_;
    GroundPathBuilder builder_;
    void smooth(Path*,const Pathfinder::GroundRouteQuery&);
    Pathfinder::NavigationStats stats_;
    std::shared_ptr<const HpaClusterGraph> prepareLiveHierarchy(const Pathfinder::GroundRouteQuery&,
        std::span<const std::uint8_t>* terrain = nullptr);
    void prepareCapturedSnapshot(const Pathfinder::GroundRouteQuery&,Bool downhillOnly,
        Bool refreshDynamic=false);
    void captureDynamicSnapshotFull();
    void captureDynamicSnapshotIncremental();
    void captureDynamicSnapshot();
    void prepareCapturedOccupant(const Pathfinder::GroundRouteQuery&);
    struct CapturedSlice {
        Pathfinder::GroundRouteQuery query{};
        ObjectID moverId=INVALID_ID;
        DestinationRankQuery rank;
        DestinationQuery destination;
        std::optional<CapturedFleePolicy> flee;
        bool useFallback=false,downhillOnly=false;
        bool playerCommand=false;
        unsigned playerCommandFrame=0;
        std::uint64_t playerCommandSequence=0;
        bool retainAcrossRegionalEdits=false;
        std::uint64_t worldEpoch=0,terrainEpoch=0;
        std::vector<CapturedLayer> capturedTerrain;
        unsigned startLayer=LAYER_GROUND,goalLayer=LAYER_GROUND;
        int startCellX=0,startCellY=0;
        int goalCellX=0,goalCellY=0;
        std::uint64_t workCount=0;
        std::uint64_t sequence=0;
        unsigned dispatchBudget=10000;
        std::uint64_t elapsedNanoseconds=0;
        std::function<bool(int,int,PathfindLayerEnum)> destinationPredicate;
        std::function<void()> work;
    };
    struct WeightedResult {
        struct DeletePath { void operator()(Path*) const; };
        struct Waypoint {
            Coord3D position{};
            PathfindLayerEnum layer=LAYER_GROUND;
            bool canOptimize=false;
            bool blockedByAlly=false;
        };
        std::unique_ptr<Path,DeletePath> path;
        std::vector<Waypoint> waypoints;
        std::vector<unsigned> optimizedLinks;
        bool fallback=false;
        bool staleTerrain=false;
        std::uint64_t terrainEpoch=0;
        std::vector<CapturedLayer> capturedTerrain;
        WeightedResult(Path* value=nullptr,bool usedFallback=false):path(value),fallback(usedFallback) {}
    };
    struct WeightedTask {
        Continuation<WeightedResult> continuation;
        Pathfinder* world=nullptr;
        ObjectID ignoredObstacle=INVALID_ID;
        State* owner=nullptr;
        std::uint64_t epoch=0;
        const Object* mover=nullptr;
        ObjectID moverId=INVALID_ID;
        std::uint64_t requestRevision=0;
        bool cancelled=false;
        bool started=false;
        std::shared_ptr<CapturedSlice> slice;
        bool obsolete() const;
        bool canStart() const;
        bool discardObsolete();
        bool done() const;
        bool resume();
        WeightedResult take();
    };
    Path* materializeCapturedResult(const Pathfinder::GroundRouteQuery&,WeightedResult&&);
    bool capturedTerrainUnchanged(const Pathfinder::GroundRouteQuery&,const WeightedResult&);
    WeightedTask weightedTask(Pathfinder::GroundRouteQuery, Coord3D, Coord3D, bool,
        std::function<bool(int,int,PathfindLayerEnum)>,
        std::function<std::optional<double>(int,int,PathfindLayerEnum,unsigned)>,
        std::optional<ICoord2D>, unsigned sliceBudget=0,std::shared_ptr<CapturedSlice> slice={},unsigned neighborLimit=0);
    Continuation<WeightedResult> weightedContinuation(Pathfinder::GroundRouteQuery, Coord3D, Coord3D, bool,
        std::function<bool(int,int,PathfindLayerEnum)>,
        std::function<std::optional<double>(int,int,PathfindLayerEnum,unsigned)>,
        std::optional<ICoord2D>, unsigned sliceBudget,std::shared_ptr<CapturedSlice> slice,unsigned neighborLimit);
public:
    explicit GroundRoutePlanner(Pathfinder&);
    ~GroundRoutePlanner();
    void reset();
    bool hasPending(ObjectID) const;
    bool pendingUsesFallback(ObjectID) const;
    void discardStalePending();
    void discardUnqueuedRequests();
    void cancelRequest(ObjectID);
    bool advanceCapturedSlice();
    void commitCapturedSlice();
    bool canAdmitRequest(bool playerCommand) const;
    void preemptOlderPlayerRequest(unsigned frame,std::uint64_t sequence);
    bool isPlayerCommand(ObjectID) const;
    std::vector<ObjectID> pollableRequests() const;
    // Capture the immutable map/occupant view before the first live request so
    // HPA construction and full-grid copying never become a gameplay-frame
    // dependency. The call is owner-thread-only.
    void warmStaticSnapshot();
    void invalidate();
    void invalidate(const IRegion2D&);
    void markDynamicRegion(const IRegion2D&);
    Pathfinder::NavigationStats stats() const;
    Bool definitelyDisconnected(const Pathfinder::GroundRouteQuery&, const Coord3D*, const Coord3D*);
    Bool goalIsEnclosed(const Pathfinder::GroundRouteQuery&, const Coord3D*, const Coord3D*,
        std::vector<std::array<int,2>>* enclosedCells=nullptr);
    Path* findWeighted(const Pathfinder::GroundRouteQuery&, const Coord3D*, const Coord3D*, bool downhillOnly=false,
        const std::function<bool(int,int,PathfindLayerEnum)>& destination = {},
        const std::function<std::optional<double>(int,int,PathfindLayerEnum,unsigned)>& fallback = {},
        std::optional<ICoord2D> distanceOnlyOrigin = {}, bool* usedFallback = nullptr, bool deferred = false,
        const DestinationRankQuery* capturedRank=nullptr,const DestinationQuery* capturedDestination=nullptr,
        const FleeQuery* capturedFlee=nullptr,
        const std::function<bool(int,int,PathfindLayerEnum)>* capturedDestinationPredicate=nullptr,unsigned neighborLimit=0);
};
}
