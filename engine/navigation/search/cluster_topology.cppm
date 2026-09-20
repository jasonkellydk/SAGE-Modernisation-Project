module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <span>
#include <vector>

export module engine.navigation.search.cluster_topology;
import engine.navigation.search.route_search;

export namespace navigation {
inline constexpr std::array<std::array<int,2>,8> ClusterDirections{{
    {1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}}};

// A compact, optimistic HPA abstraction. Each bit denotes a crossing into an
// adjacent cluster. Fine refinement remains authoritative inside a cluster.
struct HpaClusterGraph {
    int width=0,height=0,clusterSize=16,columns=0,rows=0;
    std::vector<std::uint8_t> crossings,occupied;
    std::uint64_t revision=0;
    unsigned node(int x,int y) const {
        if (x<0 || y<0 || x>=width || y>=height) return UINT32_MAX;
        return unsigned((y/clusterSize)*columns+x/clusterSize);
    }
    template<class Emit> void neighbors(unsigned node,Emit emit) const {
        if (node>=crossings.size()) return;
        const int x=int(node)%columns,y=int(node)/columns;
        const auto mask=crossings[node];
        for (unsigned i=0;i<8;++i) if (mask&(1u<<i)) {
            const auto direction=ClusterDirections[i];
            emit(unsigned((y+direction[1])*columns+x+direction[0]),i<4?10u:14u);
        }
    }
};

class HpaClusterTopology {
    std::shared_ptr<HpaClusterGraph> graph_;
    std::vector<std::uint8_t> cells_,dirtyFlags_;
    std::vector<unsigned> dirty_;
    std::uint64_t cellsRead_=0;
    void mark(int x,int y) {
        if (!graph_ || x<0 || y<0 || x>=graph_->columns || y>=graph_->rows) return;
        const auto index=unsigned(y*graph_->columns+x);
        if (!dirtyFlags_[index]) { dirtyFlags_[index]=1;dirty_.push_back(index); }
    }
public:
    void configure(int width,int height,int clusterSize=16) {
        if (width<=0 || height<=0 || clusterSize<=0 ||
            std::uint64_t(width)*height>=UINT32_MAX)
            throw std::invalid_argument("Invalid HPA cluster dimensions");
        if (graph_ && graph_->width==width && graph_->height==height && graph_->clusterSize==clusterSize) return;
        graph_=std::make_shared<HpaClusterGraph>();
        graph_->width=width;graph_->height=height;graph_->clusterSize=clusterSize;
        graph_->columns=(width-1)/clusterSize+1;graph_->rows=(height-1)/clusterSize+1;
        const auto count=std::size_t(graph_->columns)*graph_->rows;
        graph_->crossings.assign(count,0);graph_->occupied.assign(count,0);
        cells_.assign(std::size_t(width)*height,0);
        dirtyFlags_.assign(count,0);dirty_.clear();cellsRead_=0;
        invalidate(0,0,width-1,height-1);
    }
    void invalidate(int left,int top,int right,int bottom) {
        if (!graph_ || left>right || top>bottom || right<0 || bottom<0 ||
            left>=graph_->width || top>=graph_->height) return;
        const int size=graph_->clusterSize;
        const int x0=std::max(0,left)/size,x1=std::min(graph_->width-1,right)/size;
        const int y0=std::max(0,top)/size,y1=std::min(graph_->height-1,bottom)/size;
        // Neighbors own the reverse crossing and must observe boundary edits.
        for (int y=y0-1;y<=y1+1;++y) for (int x=x0-1;x<=x1+1;++x) mark(x,y);
    }
    template<class Read> std::shared_ptr<const HpaClusterGraph> prepare(Read read) {
        if (!graph_) throw std::logic_error("HPA cluster topology is not configured");
        if (dirty_.empty()) return graph_;
        if (graph_.use_count()!=1) graph_=std::make_shared<HpaClusterGraph>(*graph_);
        const auto& g=*graph_;
        for (const auto node:dirty_) {
            const int left=int(node)%g.columns*g.clusterSize,top=int(node)/g.columns*g.clusterSize;
            for (int y=top;y<std::min(g.height,top+g.clusterSize);++y)
                for (int x=left;x<std::min(g.width,left+g.clusterSize);++x) {
                    cells_[std::size_t(y)*g.width+x]=read(x,y)?1:0;
                    ++cellsRead_;
                }
        }
        for (const auto node:dirty_) {
            const int cx=int(node)%g.columns,cy=int(node)/g.columns;
            const int left=cx*g.clusterSize,top=cy*g.clusterSize;
            const int right=std::min(g.width,left+g.clusterSize),bottom=std::min(g.height,top+g.clusterSize);
            std::uint8_t crossings=0,occupied=0;
            for (int y=top;y<bottom;++y) for (int x=left;x<right;++x) {
                if (!cells_[std::size_t(y)*g.width+x]) continue;
                occupied=1;
                if (x>left && x+1<right && y>top && y+1<bottom) continue;
                for (const auto delta:ClusterDirections) {
                    const int nx=x+delta[0],ny=y+delta[1];
                    if (nx<0 || ny<0 || nx>=g.width || ny>=g.height ||
                        !cells_[std::size_t(ny)*g.width+nx]) continue;
                    const int dx=nx/g.clusterSize-cx,dy=ny/g.clusterSize-cy;
                    if (!dx && !dy) continue;
                    for (unsigned i=0;i<8;++i)
                        if (ClusterDirections[i][0]==dx && ClusterDirections[i][1]==dy)
                            crossings|=std::uint8_t(1u<<i);
                }
            }
            graph_->crossings[node]=crossings;graph_->occupied[node]=occupied;
            dirtyFlags_[node]=0;
        }
        dirty_.clear();++graph_->revision;
        return graph_;
    }
    std::uint64_t cellsRead() const { return cellsRead_; }
    // Synchronous fine refinement can reuse the prepared terrain plane.
    // The view belongs to this topology and must not cross a world mutation.
    std::span<const std::uint8_t> preparedCells() const {
        if (!dirty_.empty()) throw std::logic_error("Unprepared HPA terrain view");
        return cells_;
    }
};

// The coarse search uses the same reusable SoA workspace as fine refinement.
// It can yield after a fixed number of operations without consulting a clock.
class HpaClusterSearch {
    std::shared_ptr<const HpaClusterGraph> graph_;
    RouteSearchWorkspace search_;
    unsigned goal_=UINT32_MAX;
    unsigned heuristic(unsigned node) const {
        const auto dx=unsigned(std::abs(int(node)%graph_->columns-int(goal_)%graph_->columns));
        const auto dy=unsigned(std::abs(int(node)/graph_->columns-int(goal_)/graph_->columns));
        return 10u*std::max(dx,dy)+4u*std::min(dx,dy);
    }
public:
    void begin(std::shared_ptr<const HpaClusterGraph> graph,int fromX,int fromY,int toX,int toY) {
        if (!graph) throw std::invalid_argument("Missing HPA cluster graph");
        graph_=std::move(graph);goal_=graph_->node(toX,toY);
        const auto start=graph_->node(fromX,fromY);
        const auto count=unsigned(graph_->crossings.size());
        const bool valid=start<count && goal_<count && graph_->occupied[start] && graph_->occupied[goal_];
        search_.useReferenceOrder();
        search_.begin(count,valid?start:UINT32_MAX,goal_,[this](unsigned node) { return heuristic(node); },true);
    }
    RouteSearchStatus advance(unsigned budget) {
        return search_.advance(budget,[this](unsigned node) { return heuristic(node); },
            [this](unsigned node,auto emit) { graph_->neighbors(node,emit); });
    }
    RouteSearchStatus status() const { return search_.status(); }
    const RouteSearchResult& result() const { return search_.result(); }
    std::vector<std::uint8_t> corridor(unsigned padding) const {
        std::vector<std::uint8_t> result(graph_->crossings.size(),0);
        for (const auto node:search_.result().nodes) {
            const int x=int(node)%graph_->columns,y=int(node)/graph_->columns;
            const int distance=int(std::min(padding,unsigned(std::max(graph_->columns,graph_->rows))));
            for (int yy=std::max(0,y-distance);yy<=std::min(graph_->rows-1,y+distance);++yy)
                for (int xx=std::max(0,x-distance);xx<=std::min(graph_->columns-1,x+distance);++xx)
                    result[std::size_t(yy)*graph_->columns+xx]=1;
        }
        return result;
    }
};
}
