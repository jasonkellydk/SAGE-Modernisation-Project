module;
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

export module engine.navigation.search.graph.captured_flee;
import engine.navigation.search.graph.captured_weighted;
import engine.navigation.movement.destination.flee_goal;
import engine.navigation.movement.destination.reservations;
import engine.navigation.search.route_search;

export namespace navigation {
// Per-search policy: threats and the progress counter belong to the request,
// so neither slice scheduling nor later threat movement changes its search.
class CapturedFleePolicy {
    FleeQuery threat_;
    FleeGoal goal_;
    bool center_;
    static float position(int cell,bool center) {
        return center ? (float(cell)+0.5f)*10.f : float((float(cell)+0.05)*10.f);
    }
public:
    CapturedFleePolicy(FleeQuery threat,bool center):threat_(threat),goal_(threat),center_(center) {}
    // Admissible lower bound for reaching any geometrically safe cell.  A
    // route must leave both repulsor circles, so the larger boundary distance
    // is a lower bound on the travelled world distance.  Flooring keeps the
    // integer edge-cost estimate conservative at diagonal and fractional
    // cell boundaries.
    unsigned heuristic(const CapturedWeightedGraph& graph,unsigned id) const {
        // The native safe-path contract is a ranked reachable-endpoint
        // search. Its destination predicate is supplied to the weighted
        // kernel, which therefore uses Dijkstra ordering (zero remaining
        // estimate). A geometric flee estimate is admissible, but changes
        // equal-cost visitation order and can select a different endpoint;
        // that is observable in replays and in the legacy edge-case oracle.
        // Keep the captured and synchronous kernels on the same deterministic
        // ordering. The requests are still independent jobs in the batch.
        (void)graph;
        (void)id;
        return 0;
    }
    bool hasAnyDestination(const CapturedWeightedGraph& graph,DestinationQuery destination) const {
        return graph.anyDestination(destination,[](int,int,unsigned) { return true; });
    }
    template<class Emit>
    void neighbors(const CapturedWeightedGraph& graph,unsigned id,DestinationQuery destination,Emit emit,
        unsigned parent=std::numeric_limits<unsigned>::max(),const RouteSearchWorkspace* observed=nullptr) {
        if (id==graph.terminal()) return;
        const auto cell=graph.decode(id);
        // Native flee searches evaluate the current cell before expanding any
        // neighbour.  Preserve that zero-displacement result explicitly: it
        // is both a common case and the determinism anchor for an already-safe
        // unit.  The stateful distance-progress policy below is only needed
        // for cells reached after the start.
        const auto startX=position(cell.x,center_),startY=position(cell.y,center_);
        if (!cell.escaping && id==graph.start() &&
            FleeGoal::distanceSquared(threat_,startX,startY) > threat_.radius*threat_.radius &&
            graph.destinationAllowed(cell.x,cell.y,cell.layer,destination)) {
            emit(graph.terminal(),0);
            return;
        }
        if (!cell.escaping && goal_.consider(position(cell.x,center_),position(cell.y,center_)) &&
            graph.destinationAllowed(cell.x,cell.y,cell.layer,destination)) {
            emit(graph.terminal(),0);
            return;
        }
        graph.neighbors(id,emit,parent,observed);
    }
    std::optional<double> rank(const CapturedWeightedGraph& graph,unsigned id,DestinationQuery destination) const {
        if (id==graph.terminal()) return {};
        const auto cell=graph.decode(id),start=graph.decode(graph.start());
        if (cell.escaping || (cell.x==start.x && cell.y==start.y && cell.layer==start.layer) ||
            !graph.destinationAllowed(cell.x,cell.y,cell.layer,destination)) return {};
        return -double(FleeGoal::distanceSquared(threat_,position(cell.x,center_),position(cell.y,center_)));
    }
};
}
