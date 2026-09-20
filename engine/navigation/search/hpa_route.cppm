module;

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <queue>
#include <span>
#include <functional>
#include <vector>

export module engine.navigation.search.hpa_route;

export namespace navigation {

struct HpaGridPoint {
    int x=0;
    int y=0;
    auto operator<=>(const HpaGridPoint&) const = default;
};

struct HpaRoute {
    std::vector<HpaGridPoint> clusters;
    std::vector<std::uint32_t> distanceToGoal;
    // Cluster membership of the deterministic abstract route.  The fine
    // search uses this only as a refinement corridor; it never replaces the
    // cell-level movement validator.
    std::vector<std::uint8_t> corridor;
    // Optional static component mask for the exact search's start cluster.
    // HPA is an over-approximation of movement policy, so a cluster outside
    // this mask cannot contain a route reachable from the captured start.
    std::vector<std::uint8_t> startComponent;
    unsigned clusterSize=16;
    unsigned width=0;
    unsigned height=0;
    bool found=false;
    // Pair-specific corridors can share the goal reverse field. This keeps
    // the per-query guide compact even when a large batch has many distinct
    // starts and destinations.

    bool valid() const noexcept { return found && !clusters.empty(); }
    std::uint32_t lowerBound(int x,int y) const noexcept {
        if (!width || !height || x<0 || y<0 || unsigned(x)>=width || unsigned(y)>=height)
            return std::numeric_limits<std::uint32_t>::max();
        const auto cx=unsigned(x)/clusterSize,cy=unsigned(y)/clusterSize;
        const auto index=cy*((width+clusterSize-1)/clusterSize)+cx;
        if (index>=distanceToGoal.size()) return std::numeric_limits<std::uint32_t>::max();
        return distanceToGoal[index];
    }
    bool inStartComponent(int x,int y) const noexcept {
        if (!width || !height || x<0 || y<0 || unsigned(x)>=width || unsigned(y)>=height)
            return false;
        const auto cx=unsigned(x)/clusterSize,cy=unsigned(y)/clusterSize;
        const auto index=cy*((width+clusterSize-1)/clusterSize)+cx;
        return index<startComponent.size() && startComponent[index]!=0;
    }
    bool hasStartComponent() const noexcept { return !startComponent.empty(); }
    bool inCorridor(int x,int y) const noexcept {
        if (!width || !height || x<0 || y<0 || unsigned(x)>=width || unsigned(y)>=height)
            return false;
        const auto cx=unsigned(x)/clusterSize,cy=unsigned(y)/clusterSize;
        const auto index=cy*((width+clusterSize-1)/clusterSize)+cx;
        return index<corridor.size() && corridor[index]!=0;
    }
    bool inCorridor(int x,int y,int startX,int startY) const noexcept {
        if (!width || !height || x<0 || y<0 || startX<0 || startY<0 ||
            unsigned(x)>=width || unsigned(y)>=height || unsigned(startX)>=width ||
            unsigned(startY)>=height) return false;
        if (!corridor.empty()) return inCorridor(x,y);
        const auto startBound=lowerBound(startX,startY);
        const auto bound=lowerBound(x,y);
        if (bound==std::numeric_limits<std::uint32_t>::max()) return false;
        constexpr std::uint32_t detourMargin=10000;
        return startBound==std::numeric_limits<std::uint32_t>::max() ||
            bound<=startBound || bound-startBound<=detourMargin;
    }
};

// Static HPA* hierarchy. The expensive terrain scan and portal extraction are
// performed once for a captured terrain snapshot and shared by every query.
struct HpaHierarchy {
    unsigned clusterSize=16,width=0,height=0,clustersWide=0,clustersHigh=0;
    std::vector<std::uint8_t> reachable;
    std::vector<std::vector<std::uint32_t>> edges;
    // Fine components make negative proofs precise inside a cluster. The
    // abstract graph intentionally remains an over-approximation for search
    // guidance, but a cluster can otherwise join unrelated pockets merely
    // because each pocket contains a portal.
    std::vector<std::uint32_t> fineComponent;

    bool valid() const noexcept {
        return width && height && clustersWide && clustersHigh &&
            reachable.size()==std::size_t(clustersWide)*clustersHigh && edges.size()==reachable.size();
    }
    bool fineConnected(HpaGridPoint start,HpaGridPoint goal) const noexcept {
        if (start.x<0 || start.y<0 || goal.x<0 || goal.y<0 ||
            unsigned(start.x)>=width || unsigned(start.y)>=height ||
            unsigned(goal.x)>=width || unsigned(goal.y)>=height) return false;
        const auto startIndex=std::size_t(start.y)*width+unsigned(start.x);
        const auto goalIndex=std::size_t(goal.y)*width+unsigned(goal.x);
        if (startIndex>=fineComponent.size() || goalIndex>=fineComponent.size()) return false;
        const auto invalid=std::numeric_limits<std::uint32_t>::max();
        return fineComponent[startIndex]!=invalid && fineComponent[startIndex]==fineComponent[goalIndex];
    }
    HpaRoute lowerBounds(HpaGridPoint start,HpaGridPoint goal) const {
        HpaRoute result;
        result.width=width; result.height=height; result.clusterSize=clusterSize;
        if (!valid() || start.x<0 || start.y<0 || goal.x<0 || goal.y<0 ||
            unsigned(start.x)>=width || unsigned(start.y)>=height ||
            unsigned(goal.x)>=width || unsigned(goal.y)>=height) return result;
        const auto index=[this](unsigned x,unsigned y) { return y*clustersWide+x; };
        const auto startCluster=HpaGridPoint{start.x/int(clusterSize),start.y/int(clusterSize)};
        const auto goalCluster=HpaGridPoint{goal.x/int(clusterSize),goal.y/int(clusterSize)};
        const auto startIndex=index(unsigned(startCluster.x),unsigned(startCluster.y));
        const auto goalIndex=index(unsigned(goalCluster.x),unsigned(goalCluster.y));
        if (!reachable[startIndex] || !reachable[goalIndex]) return result;
        const auto infinity=std::numeric_limits<std::uint32_t>::max();
        result.distanceToGoal.assign(edges.size(),infinity);
        struct Entry {
            std::uint32_t cost=0,node=0;
            bool operator>(const Entry& other) const noexcept {
                return cost!=other.cost ? cost>other.cost : node>other.node;
            }
        };
        std::priority_queue<Entry,std::vector<Entry>,std::greater<>> pending;
        result.distanceToGoal[goalIndex]=0; pending.push({0,goalIndex});
        while (!pending.empty()) {
            const auto current=pending.top(); pending.pop();
            if (current.cost!=result.distanceToGoal[current.node]) continue;
            for (const auto next:edges[current.node]) {
                const auto fromX=current.node%clustersWide,fromY=current.node/clustersWide;
                const auto toX=next%clustersWide,toY=next/clustersWide;
                const auto edgeCost=(fromX!=toX && fromY!=toY) ? 1414u : 1000u;
                const auto candidate=current.cost>infinity-edgeCost ? infinity :
                    current.cost+edgeCost;
                if (candidate<result.distanceToGoal[next]) {
                    result.distanceToGoal[next]=candidate;
                    pending.push({candidate,next});
                }
            }
        }
    if (result.distanceToGoal[startIndex]==infinity) {
            // The requested goal is statically disconnected. Preserve the
            // start-side component so the exact closest-endpoint search can
            // avoid expanding terrain that no route from the start can reach.
            result.startComponent.assign(edges.size(),0);
            result.startComponent[startIndex]=1;
            std::queue<std::uint32_t> componentPending;
            componentPending.push(startIndex);
            while (!componentPending.empty()) {
                const auto node=componentPending.front(); componentPending.pop();
                for (const auto next:edges[node]) if (!result.startComponent[next]) {
                    result.startComponent[next]=1;
                    componentPending.push(next);
                }
            }
            result.distanceToGoal.clear();
            return result;
        }
    result.startComponent.resize(edges.size(),0);
        for (std::size_t i=0;i<result.distanceToGoal.size();++i)
            result.startComponent[i]=result.distanceToGoal[i]!=infinity;
        // CapturedWeightedGraph only consumes valid()/lowerBound(). Keep a
        // compact marker so this lower-bound-only representation has the same
        // validity contract as route(), without retaining an unused path.
    result.clusters.push_back(startCluster);
        result.found=true;
        return result;
    }
    HpaRoute route(HpaGridPoint start,HpaGridPoint goal) const {
        HpaRoute result;
        result.width=width; result.height=height; result.clusterSize=clusterSize;
        if (!valid() || start.x<0 || start.y<0 || goal.x<0 || goal.y<0 ||
            unsigned(start.x)>=width || unsigned(start.y)>=height ||
            unsigned(goal.x)>=width || unsigned(goal.y)>=height) return result;
        const auto index=[this](unsigned x,unsigned y) { return y*clustersWide+x; };
        const auto startCluster=HpaGridPoint{start.x/int(clusterSize),start.y/int(clusterSize)};
        const auto goalCluster=HpaGridPoint{goal.x/int(clusterSize),goal.y/int(clusterSize)};
        const auto startIndex=index(unsigned(startCluster.x),unsigned(startCluster.y));
        const auto goalIndex=index(unsigned(goalCluster.x),unsigned(goalCluster.y));
        if (!reachable[startIndex] || !reachable[goalIndex]) return result;
        const auto infinity=std::numeric_limits<std::uint32_t>::max();
        std::vector<std::uint32_t> distance(edges.size(),infinity);
        std::vector<std::int32_t> parent(edges.size(),-1);
        struct Entry { std::uint32_t f=0,g=0,node=0;
            bool operator>(const Entry& other) const noexcept { return f!=other.f ? f>other.f : node>other.node; } };
        std::priority_queue<Entry,std::vector<Entry>,std::greater<>> frontier;
        distance[startIndex]=0; frontier.push({0,0,startIndex});
        const auto heuristic=[&](std::uint32_t node) {
            return std::uint32_t(std::abs(int(node%clustersWide)-goalCluster.x)+
                std::abs(int(node/clustersWide)-goalCluster.y));
        };
        while (!frontier.empty()) {
            const auto current=frontier.top(); frontier.pop();
            if (current.g!=distance[current.node]) continue;
            if (current.node==goalIndex) break;
            for (const auto next:edges[current.node]) {
                const auto candidate=current.g+1;
                if (candidate<distance[next] || (candidate==distance[next] &&
                    std::int32_t(current.node)<parent[next])) {
                    distance[next]=candidate; parent[next]=std::int32_t(current.node);
                    frontier.push({candidate+heuristic(next),candidate,next});
                }
            }
        }
        if (distance[goalIndex]==infinity) return result;
        for (std::int32_t node=std::int32_t(goalIndex);node>=0;node=parent[unsigned(node)])
            result.clusters.push_back({node%std::int32_t(clustersWide),node/std::int32_t(clustersWide)});
        std::reverse(result.clusters.begin(),result.clusters.end());
        result.corridor.assign(edges.size(),0);
        for (const auto cluster:result.clusters)
            result.corridor[std::size_t(cluster.y)*clustersWide+unsigned(cluster.x)]=1;
        result.distanceToGoal.assign(edges.size(),infinity);
        struct DistanceEntry {
            std::uint32_t cost=0,node=0;
            bool operator>(const DistanceEntry& other) const noexcept {
                return cost!=other.cost ? cost>other.cost : node>other.node;
            }
        };
        std::priority_queue<DistanceEntry,std::vector<DistanceEntry>,std::greater<>> pending;
        result.distanceToGoal[goalIndex]=0; pending.push({0,goalIndex});
        while (!pending.empty()) {
            const auto current=pending.top(); pending.pop();
            if (current.cost!=result.distanceToGoal[current.node]) continue;
            for (const auto next:edges[current.node]) {
                const auto fromX=current.node%clustersWide,fromY=current.node/clustersWide;
                const auto toX=next%clustersWide,toY=next/clustersWide;
                const auto edgeCost=(fromX!=toX && fromY!=toY) ? 1414u : 1000u;
                const auto candidate=current.cost>infinity-edgeCost ? infinity :
                    current.cost+edgeCost;
                if (candidate<result.distanceToGoal[next]) {
                    result.distanceToGoal[next]=candidate;
                    pending.push({candidate,next});
                }
            }
        }
        result.found=true;
        return result;
    }
};

// A static layered abstraction. Each plane contributes its ordinary HPA
// cluster graph and captured layer connections add zero-cost portal edges.
// The reverse distance field is a lower bound for the exact weighted graph;
// dynamic occupants and footprint policy remain the responsibility of the
// captured fine search.
struct HpaLayerInput {
    unsigned layer=0;
    std::shared_ptr<const HpaHierarchy> hierarchy;
    std::function<unsigned(int,int)> connection;
};

struct LayeredHpaHierarchy {
    struct Plane {
        unsigned layer=0;
        std::shared_ptr<const HpaHierarchy> hierarchy;
        unsigned offset=0;
    };
    std::vector<Plane> planes;
    unsigned width=0,height=0,clusterSize=16;
    std::vector<std::vector<std::uint32_t>> edges;
    // Reverse adjacency is part of the immutable hierarchy. Rebuilding it
    // for every layer-transition goal made two otherwise tiny requests scan
    // the complete map-sized abstraction on the owner thread.
    std::vector<std::vector<std::pair<std::uint32_t,std::uint32_t>>> reverseEdges;

    bool valid() const noexcept { return !planes.empty() && !edges.empty(); }

    const Plane* plane(unsigned layer) const noexcept {
        for (const auto& value:planes) if (value.layer==layer) return &value;
        return nullptr;
    }

    unsigned node(int x,int y,unsigned layer) const noexcept {
        const auto* value=plane(layer);
        if (!value || !value->hierarchy || x<0 || y<0 || unsigned(x)>=width || unsigned(y)>=height)
            return std::numeric_limits<unsigned>::max();
        const auto& hpa=*value->hierarchy;
        const auto cx=unsigned(x)/hpa.clusterSize,cy=unsigned(y)/hpa.clusterSize;
        if (cx>=hpa.clustersWide || cy>=hpa.clustersHigh) return std::numeric_limits<unsigned>::max();
        return value->offset+cy*hpa.clustersWide+cx;
    }

};

struct LayeredHpaRoute {
    std::shared_ptr<const LayeredHpaHierarchy> hierarchy;
    std::vector<std::uint32_t> distanceToGoal;
    int goalX=0,goalY=0;
    unsigned goalLayer=0;
    bool found=false;

    bool valid() const noexcept { return found && hierarchy && hierarchy->valid() && !distanceToGoal.empty(); }
    std::uint32_t lowerBound(int x,int y,unsigned layer) const noexcept {
        if (!hierarchy) return std::numeric_limits<std::uint32_t>::max();
        const auto id=hierarchy->node(x,y,layer);
        return id<distanceToGoal.size()?distanceToGoal[id]:std::numeric_limits<std::uint32_t>::max();
    }
};

inline std::shared_ptr<const LayeredHpaHierarchy> buildLayeredHpaHierarchy(
    std::span<const HpaLayerInput> inputs)
{
    auto result=std::make_shared<LayeredHpaHierarchy>();
    for (const auto& input:inputs) {
        if (!input.hierarchy || !input.hierarchy->valid()) continue;
        if (!result->width) {
            result->width=input.hierarchy->width;
            result->height=input.hierarchy->height;
            result->clusterSize=input.hierarchy->clusterSize;
        }
        if (input.hierarchy->width!=result->width || input.hierarchy->height!=result->height ||
            input.hierarchy->clusterSize!=result->clusterSize) continue;
        result->planes.push_back({input.layer,input.hierarchy,static_cast<unsigned>(result->edges.size())});
        result->edges.resize(result->edges.size()+input.hierarchy->edges.size());
    }
    if (result->planes.empty()) return result;
    for (std::size_t planeIndex=0;planeIndex<result->planes.size();++planeIndex) {
        const auto& plane=result->planes[planeIndex];
        const auto& hpa=*plane.hierarchy;
        for (std::size_t local=0;local<hpa.edges.size();++local) {
            const auto source=plane.offset+static_cast<unsigned>(local);
            for (const auto target:hpa.edges[local]) {
                const auto sx=unsigned(local%hpa.clustersWide)*hpa.clusterSize;
                const auto sy=unsigned(local/hpa.clustersWide)*hpa.clusterSize;
                const auto tx=unsigned(target%hpa.clustersWide)*hpa.clusterSize;
                const auto ty=unsigned(target/hpa.clustersWide)*hpa.clusterSize;
                const auto cost=(sx!=tx && sy!=ty)?1414u:1000u;
                result->edges[source].push_back(plane.offset+target);
                (void)cost;
            }
        }
    }
    // Portal edges are generated from captured static connection anchors. A
    // callback is deliberately supplied by the owner, keeping this module
    // independent from CellSnapshot and native game types.
    for (const auto& input:inputs) {
        const auto* source=result->plane(input.layer);
        if (!source || !input.connection) continue;
        for (unsigned y=0;y<result->height;++y) for (unsigned x=0;x<result->width;++x) {
            const auto targetLayer=input.connection(int(x),int(y));
            if (!targetLayer) continue;
            const auto from=result->node(int(x),int(y),input.layer);
            const auto to=result->node(int(x),int(y),targetLayer);
            if (from>=result->edges.size() || to>=result->edges.size()) continue;
            result->edges[from].push_back(to);
        }
    }
    for (auto& edges:result->edges) {
        std::sort(edges.begin(),edges.end());
        edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
    }
    result->reverseEdges.resize(result->edges.size());
    for (std::size_t source=0;source<result->edges.size();++source) {
        for (const auto target:result->edges[source]) {
            std::uint32_t cost=0;
            for (const auto& plane:result->planes) {
                if (source<plane.offset || source>=plane.offset+plane.hierarchy->edges.size()) continue;
                const auto& hpa=*plane.hierarchy;
                if (target>=plane.offset && target<plane.offset+hpa.edges.size()) {
                    const auto local=unsigned(source-plane.offset);
                    const auto targetLocal=unsigned(target-plane.offset);
                    const auto sx=local%hpa.clustersWide,sy=local/hpa.clustersWide;
                    const auto tx=targetLocal%hpa.clustersWide,ty=targetLocal/hpa.clustersWide;
                    cost=(sx!=tx && sy!=ty)?1414u:1000u;
                }
                break;
            }
            result->reverseEdges[target].push_back({static_cast<std::uint32_t>(source),cost});
        }
    }
    for (auto& edges:result->reverseEdges)
        std::sort(edges.begin(),edges.end(),[](const auto& left,const auto& right) {
            return left.first!=right.first ? left.first<right.first : left.second<right.second;
        });
    return result;
}

inline std::shared_ptr<const LayeredHpaRoute> buildLayeredHpaRoute(
    std::shared_ptr<const LayeredHpaHierarchy> hierarchy,
    int goalX,int goalY,unsigned goalLayer)
{
    auto result=std::make_shared<LayeredHpaRoute>();
    if (!hierarchy || !hierarchy->valid()) return result;
    const auto goal=hierarchy->node(goalX,goalY,goalLayer);
    if (goal>=hierarchy->edges.size()) return result;
    const auto infinity=std::numeric_limits<std::uint32_t>::max();
    if (hierarchy->reverseEdges.size()!=hierarchy->edges.size()) return result;
    result->distanceToGoal.assign(hierarchy->edges.size(),infinity);
    struct Entry { std::uint32_t cost=0,node=0;
        bool operator>(const Entry& other) const noexcept {
            return cost!=other.cost?cost>other.cost:node>other.node;
        }
    };
    std::priority_queue<Entry,std::vector<Entry>,std::greater<>> open;
    result->distanceToGoal[goal]=0;open.push({0,goal});
    while (!open.empty()) {
        const auto current=open.top();open.pop();
        if (current.cost!=result->distanceToGoal[current.node]) continue;
        for (const auto [source,cost]:hierarchy->reverseEdges[current.node]) {
            const auto next=current.cost>infinity-cost?infinity:current.cost+cost;
            if (next<result->distanceToGoal[source]) {
                result->distanceToGoal[source]=next;open.push({next,source});
            }
        }
    }
    result->hierarchy=std::move(hierarchy);
    result->goalLayer=goalLayer;result->goalX=goalX;result->goalY=goalY;
    result->found=result->distanceToGoal[goal]!=infinity;
    return result;
}

template<class Passable>
std::shared_ptr<const HpaHierarchy> buildHpaHierarchy(unsigned width,unsigned height,
    Passable passable,unsigned clusterSize=16) {
    auto result=std::make_shared<HpaHierarchy>();
    result->width=width; result->height=height; result->clusterSize=std::max(1u,clusterSize);
    if (!width || !height) return result;
    result->clustersWide=(width+result->clusterSize-1)/result->clusterSize;
    result->clustersHigh=(height+result->clusterSize-1)/result->clusterSize;
    const auto count=std::size_t(result->clustersWide)*result->clustersHigh;
    result->reachable.assign(count,0); result->edges.resize(count);
    const auto cellCount=std::size_t(width)*height;
    const auto invalidComponent=std::numeric_limits<std::uint32_t>::max();
    result->fineComponent.assign(cellCount,invalidComponent);
    std::vector<std::uint8_t> open(cellCount,0);
    const auto cellIndex=[width](unsigned x,unsigned y) { return std::size_t(y)*width+x; };
    for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x)
        open[cellIndex(x,y)]=passable(int(x),int(y))?1:0;
    const auto index=[&](unsigned x,unsigned y) { return y*result->clustersWide+x; };
    for (unsigned cy=0;cy<result->clustersHigh;++cy) for (unsigned cx=0;cx<result->clustersWide;++cx) {
        const unsigned left=cx*result->clusterSize,top=cy*result->clusterSize;
        const unsigned right=std::min(width,left+result->clusterSize),bottom=std::min(height,top+result->clusterSize);
        for (unsigned y=top;y<bottom && !result->reachable[index(cx,cy)];++y)
            for (unsigned x=left;x<right;++x) if (open[cellIndex(x,y)]) {
                result->reachable[index(cx,cy)]=1; break;
            }
    }
    constexpr int componentDx[]={1,0,-1,0,1,-1,-1,1};
    constexpr int componentDy[]={0,1,0,-1,1,1,-1,-1};
    std::queue<std::uint32_t> componentQueue;
    std::uint32_t component=0;
    for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
        const auto start=cellIndex(x,y);
        if (!open[start] || result->fineComponent[start]!=invalidComponent) continue;
        result->fineComponent[start]=component;
        componentQueue.push(static_cast<std::uint32_t>(start));
        while (!componentQueue.empty()) {
            const auto current=componentQueue.front(); componentQueue.pop();
            const unsigned currentX=current%width,currentY=current/width;
            for (unsigned direction=0;direction<8;++direction) {
                const int nx=int(currentX)+componentDx[direction],ny=int(currentY)+componentDy[direction];
                if (nx<0 || ny<0 || unsigned(nx)>=width || unsigned(ny)>=height) continue;
                if (direction>=4) {
                    const int ax=int(currentX)+componentDx[direction-4],ay=int(currentY)+componentDy[direction-4];
                    const unsigned perpendicular=(direction-3)%4;
                    const int bx=int(currentX)+componentDx[perpendicular],by=int(currentY)+componentDy[perpendicular];
                    if (ax<0 || ay<0 || bx<0 || by<0 || unsigned(ax)>=width || unsigned(ay)>=height ||
                        unsigned(bx)>=width || unsigned(by)>=height ||
                        !open[cellIndex(unsigned(ax),unsigned(ay))] ||
                        !open[cellIndex(unsigned(bx),unsigned(by))]) continue;
                }
                const auto next=cellIndex(unsigned(nx),unsigned(ny));
                if (open[next] && result->fineComponent[next]==invalidComponent) {
                    result->fineComponent[next]=component;
                    componentQueue.push(static_cast<std::uint32_t>(next));
                }
            }
        }
        ++component;
    }
    const auto boundaryOpen=[&](unsigned ax,unsigned ay,unsigned bx,unsigned by) {
        if (ax!=bx && ay==by) {
            const auto left=std::min(ax,bx),border=(left+1)*result->clusterSize;
            const auto top=ay*result->clusterSize,bottom=std::min(height,top+result->clusterSize);
            for (unsigned y=top;y<bottom;++y) if (border && border<width &&
                open[cellIndex(border-1,y)] && open[cellIndex(border,y)]) return true;
        } else if (ax==bx && ay!=by) {
            const auto top=std::min(ay,by),border=(top+1)*result->clusterSize;
            const auto left=ax*result->clusterSize,right=std::min(width,left+result->clusterSize);
            for (unsigned x=left;x<right;++x) if (border && border<height &&
                open[cellIndex(x,border-1)] && open[cellIndex(x,border)]) return true;
        } else {
            const unsigned xBorder=(std::min(ax,bx)+1u)*result->clusterSize;
            const unsigned yBorder=(std::min(ay,by)+1u)*result->clusterSize;
            if (!xBorder || !yBorder || xBorder>=width || yBorder>=height) return false;
            const unsigned xA=bx>ax?xBorder-1u:xBorder;
            const unsigned xB=bx>ax?xBorder:xBorder-1u;
            const unsigned yA=by>ay?yBorder-1u:yBorder;
            const unsigned yB=by>ay?yBorder:yBorder-1u;
            return open[cellIndex(xA,yA)] && open[cellIndex(xA,yB)] &&
                open[cellIndex(xB,yA)] && open[cellIndex(xB,yB)];
        }
        return false;
    };
    constexpr int dx[]={1,0,-1,0,1,-1,-1,1},dy[]={0,1,0,-1,1,1,-1,-1};
    for (unsigned y=0;y<result->clustersHigh;++y) for (unsigned x=0;x<result->clustersWide;++x) {
        if (!result->reachable[index(x,y)]) continue;
        for (int direction=0;direction<8;++direction) {
            const int nx=int(x)+dx[direction],ny=int(y)+dy[direction];
            if (nx<0 || ny<0 || unsigned(nx)>=result->clustersWide || unsigned(ny)>=result->clustersHigh ||
                !result->reachable[index(unsigned(nx),unsigned(ny))] || !boundaryOpen(x,y,unsigned(nx),unsigned(ny))) continue;
            result->edges[index(x,y)].push_back(index(unsigned(nx),unsigned(ny)));
        }
    }
    return result;
}

// Deterministic HPA* abstraction. Each abstract edge is backed by at least
// one passable pair of cells across the cluster boundary. The returned macro
// route is a lower-bound guide for the exact footprint/occupancy search; the
// exact search remains authoritative for dynamic blockers and narrow portals.
template<class Passable>
HpaRoute buildHpaRoute(unsigned width,unsigned height,HpaGridPoint start,HpaGridPoint goal,
    Passable passable,unsigned clusterSize=16) {
    HpaRoute result;
    result.width=width;result.height=height;result.clusterSize=std::max(1u,clusterSize);
    if (!width || !height || start.x<0 || start.y<0 || goal.x<0 || goal.y<0 ||
        unsigned(start.x)>=width || unsigned(start.y)>=height ||
        unsigned(goal.x)>=width || unsigned(goal.y)>=height) return result;
    const unsigned cw=(width+result.clusterSize-1)/result.clusterSize;
    const unsigned ch=(height+result.clusterSize-1)/result.clusterSize;
    const auto index=[cw](unsigned x,unsigned y) { return y*cw+x; };
    std::vector<std::uint8_t> reachable(cw*ch);
    auto clusterHasCell=[&](unsigned cx,unsigned cy) {
        const unsigned left=cx*result.clusterSize,top=cy*result.clusterSize;
        const unsigned right=std::min(width,left+result.clusterSize),bottom=std::min(height,top+result.clusterSize);
        for(unsigned y=top;y<bottom;++y) for(unsigned x=left;x<right;++x)
            if (passable(int(x),int(y))) return true;
        return false;
    };
    for(unsigned y=0;y<ch;++y) for(unsigned x=0;x<cw;++x)
        reachable[index(x,y)]=clusterHasCell(x,y);
    const auto startCluster=HpaGridPoint{start.x/int(result.clusterSize),start.y/int(result.clusterSize)};
    const auto goalCluster=HpaGridPoint{goal.x/int(result.clusterSize),goal.y/int(result.clusterSize)};
    if (!reachable[index(unsigned(startCluster.x),unsigned(startCluster.y))] ||
        !reachable[index(unsigned(goalCluster.x),unsigned(goalCluster.y))]) return result;
    const auto boundaryOpen=[&](unsigned ax,unsigned ay,unsigned bx,unsigned by) {
        if (ax!=bx && ay==by) {
            const auto left=std::min(ax,bx);
            const auto border=(left+1)*result.clusterSize;
            const auto row=ay;
            const auto top=row*result.clusterSize;
            const auto bottom=std::min(height,top+result.clusterSize);
            for(unsigned y=top;y<bottom;++y) if (border && border<width && passable(int(border-1),int(y)) && passable(int(border),int(y))) return true;
        } else if (ax==bx && ay!=by) {
            const auto topCluster=std::min(ay,by);
            const auto border=(topCluster+1)*result.clusterSize;
            const auto column=ax;
            const auto left=column*result.clusterSize;
            const auto right=std::min(width,left+result.clusterSize);
            for(unsigned x=left;x<right;++x) if (border && border<height && passable(int(x),int(border-1)) && passable(int(x),int(border))) return true;
        } else {
            const unsigned xBorder=(std::min(ax,bx)+1u)*result.clusterSize;
            const unsigned yBorder=(std::min(ay,by)+1u)*result.clusterSize;
            if (!xBorder || !yBorder || xBorder>=width || yBorder>=height) return false;
            const unsigned xA=bx>ax?xBorder-1u:xBorder;
            const unsigned xB=bx>ax?xBorder:xBorder-1u;
            const unsigned yA=by>ay?yBorder-1u:yBorder;
            const unsigned yB=by>ay?yBorder:yBorder-1u;
            // A diagonal cluster transition is legal only when the two
            // diagonal endpoints and both orthogonal corner guards are open.
            // This mirrors the fine graph's no-corner-cutting rule.
            return passable(int(xA),int(yA)) && passable(int(xA),int(yB)) &&
                passable(int(xB),int(yA)) && passable(int(xB),int(yB));
        }
        return false;
    };
    const auto neighbours=[&](HpaGridPoint p,auto emit) {
        constexpr int dx[]={1,0,-1,0,1,-1,-1,1};
        constexpr int dy[]={0,1,0,-1,1,1,-1,-1};
        for(int i=0;i<8;++i) {
            const int nx=p.x+dx[i],ny=p.y+dy[i];
            if(nx<0||ny<0||unsigned(nx)>=cw||unsigned(ny)>=ch) continue;
            if(reachable[index(unsigned(nx),unsigned(ny))] && boundaryOpen(unsigned(p.x),unsigned(p.y),unsigned(nx),unsigned(ny))) emit(HpaGridPoint{nx,ny});
        }
    };
    const auto startIndex=index(unsigned(startCluster.x),unsigned(startCluster.y));
    const auto goalIndex=index(unsigned(goalCluster.x),unsigned(goalCluster.y));
    std::vector<std::uint32_t> distance(cw*ch,std::numeric_limits<std::uint32_t>::max());
    std::vector<int> parent(cw*ch,-1);
    struct Entry { std::uint32_t f,g,node; auto operator>(const Entry& other) const { return f!=other.f?f>other.f:node>other.node; } };
    std::priority_queue<Entry,std::vector<Entry>,std::greater<>> frontier;
    distance[startIndex]=0;frontier.push({0,0,std::uint32_t(startIndex)});
    const auto heuristic=[goalCluster](HpaGridPoint p) { return std::uint32_t(std::abs(p.x-goalCluster.x)+std::abs(p.y-goalCluster.y)); };
    while(!frontier.empty()) {
        const auto current=frontier.top();frontier.pop();
        if(current.g!=distance[current.node]) continue;
        const auto point=HpaGridPoint{int(current.node%cw),int(current.node/cw)};
        if(current.node==goalIndex) break;
        neighbours(point,[&](HpaGridPoint next) {
            const auto nextIndex=index(unsigned(next.x),unsigned(next.y));
            const auto nextCost=current.g+1;
            if(nextCost<distance[nextIndex] || (nextCost==distance[nextIndex] && int(current.node)<parent[nextIndex])) {
                distance[nextIndex]=nextCost;parent[nextIndex]=int(current.node);
                frontier.push({nextCost+heuristic(next),nextCost,std::uint32_t(nextIndex)});
            }
        });
    }
    if(distance[goalIndex]==std::numeric_limits<std::uint32_t>::max()) return result;
    for(int node=int(goalIndex);node>=0;node=parent[unsigned(node)]) result.clusters.push_back({node%int(cw),node/int(cw)});
    std::reverse(result.clusters.begin(),result.clusters.end());
    result.corridor.assign(cw*ch,0);
    for (const auto cluster:result.clusters)
        result.corridor[std::size_t(cluster.y)*cw+unsigned(cluster.x)]=1;
    // Recompute the abstract lower bound from the goal. The abstract graph is
    // undirected, while the fine search may still have directed portal rules.
    result.distanceToGoal.assign(cw*ch,std::numeric_limits<std::uint32_t>::max());
    std::queue<std::uint32_t> distanceQueue;
    result.distanceToGoal[goalIndex]=0;distanceQueue.push(std::uint32_t(goalIndex));
    while(!distanceQueue.empty()) {
        const auto node=distanceQueue.front();distanceQueue.pop();
        const auto point=HpaGridPoint{int(node%cw),int(node/cw)};
        neighbours(point,[&](HpaGridPoint next) {
            const auto nextIndex=index(unsigned(next.x),unsigned(next.y));
            if (result.distanceToGoal[nextIndex]==std::numeric_limits<std::uint32_t>::max()) {
                result.distanceToGoal[nextIndex]=result.distanceToGoal[node]+1;
                distanceQueue.push(std::uint32_t(nextIndex));
            }
        });
    }
    result.found=true;
    return result;
}

} // namespace navigation
