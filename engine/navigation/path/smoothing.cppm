module;
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

export module engine.navigation.path.smoothing;

export namespace navigation {
struct SmoothingView {
    std::span<const float> x,y;
    std::span<const unsigned> layer;
    std::span<const std::uint8_t> canOptimize;
};

// Characterized against DX9 8fd97e8e, Path::optimize/optimizeGroundPath.
// The adapter owns positions and supplies terrain/occupancy visibility. The
// kernel works on contiguous value arrays and returns only waypoint indices.
template<class Visible>
std::vector<unsigned> smoothRoute(SmoothingView route,bool group,Visible visible) {
    const auto count=route.x.size();
    if (route.y.size()!=count || route.layer.size()!=count || route.canOptimize.size()!=count)
        throw std::invalid_argument("Inconsistent route arrays");
    if (!count) return {};
    std::vector<unsigned> next(count,unsigned(count));
    unsigned anchor=0;
    bool first=true;
    while (anchor+1<count) {
        unsigned layer=route.layer[anchor],currentLayer=layer;
        unsigned candidate=anchor+1,steps=0;
        for (;candidate+1<count;++candidate) {
            ++steps;
            if (currentLayer==1) {
                if (route.layer[candidate]!=currentLayer) {
                    layer=currentLayer=route.layer[candidate];
                    if (steps>3) break;
                }
            } else if (route.layer[candidate+1]!=currentLayer && steps>3) break;
            currentLayer=route.layer[candidate];
            if (!group && !route.canOptimize[candidate]) break;
        }
        if (!group && first) layer=route.layer.front();
        first=false;
        unsigned selected=anchor+1;
        for (;candidate>anchor;--candidate) {
            bool passable=visible(anchor,candidate,layer);
            if (!passable) {
                // Preserve native integer conversion and the order of the
                // alignment checks, including their updates to dx and dy.
                int dx=int(route.x[candidate]-route.x[anchor]);
                int dy=int(route.y[candidate]-route.y[anchor]);
                if (!group && std::abs(dx)==10 && std::abs(dy)==10) passable=true;
                bool aligned=false;
                if (dx==0) {
                    aligned=true;
                    for (auto node=candidate-1;node>anchor;--node) {
                        dx=int(route.x[node+1]-route.x[node]);
                        if (dx!=0) aligned=false;
                    }
                }
                if (dy==0) {
                    aligned=true;
                    for (auto node=candidate-1;node>anchor;--node) {
                        dy=int(route.y[node+1]-route.y[node]);
                        if (dy!=0) aligned=false;
                    }
                }
                if (dx==dy) {
                    aligned=true;
                    for (auto node=candidate-1;node>anchor;--node) {
                        dx=int(route.x[node+1]-route.x[node]);
                        dy=int(route.y[node+1]-route.y[node]);
                        if (dy!=dx) aligned=false;
                    }
                }
                if (dx==-dy) {
                    aligned=true;
                    for (auto node=candidate-1;node>anchor;--node) {
                        dx=int(route.x[node+1]-route.x[node]);
                        dy=int(route.y[node+1]-route.y[node]);
                        if (dy!=-dx) aligned=false;
                    }
                }
                passable=passable || aligned;
            }
            if (passable) { selected=candidate;break; }
        }
        next[anchor]=selected;
        anchor=selected;
    }
    if (group) {
        for (anchor=0;anchor<count;anchor=next[anchor]) {
            const auto node=next[anchor];
            if (node<count && next[node]<count) {
                const float dx=route.x[node]-route.x[anchor],dy=route.y[node]-route.y[anchor];
                if (dx*dx+dy*dy<100.0f*3.9f) next[anchor]=next[node];
            }
        }
    }
    std::vector<unsigned> result;
    for (anchor=0;anchor<count;anchor=next[anchor]) result.push_back(anchor);
    return result;
}
}
