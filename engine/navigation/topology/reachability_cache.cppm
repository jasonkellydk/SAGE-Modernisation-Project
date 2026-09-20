module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>

export module engine.navigation.topology.reachability_cache;

export namespace navigation {
struct ReachabilityCell {
    bool passable = false;
    std::uint8_t portal = 0;
};

// Prove only small, enclosed goal components. Reaching the edge of the probe,
// the start, or a layer portal leaves the answer to the full pathfinder.
// The caller supplies current full-footprint passability, including units.
struct OptimisticDiagonalConnection {
    bool operator()(int,int,int,int) const { return true; }
};
// Query-owned, fixed-size reverse probe. Each visited anchor costs one work
// credit, so even a tiny slice can suspend before a closed-pocket proof ends.
template<int Radius=8> class LocalGoalProbe {
    static constexpr int Width=2*Radius+1,Count=Width*Width;
    std::array<std::uint8_t,Count> seen_{};
    std::array<std::array<int,2>,Count> queue_{};
    int startX_=0,startY_=0,goalX_=0,goalY_=0;
    unsigned head_=0,tail_=1;
    bool complete_=false,enclosed_=true;
public:
    LocalGoalProbe(int startX,int startY,int goalX,int goalY):
        startX_(startX),startY_(startY),goalX_(goalX),goalY_(goalY) {
        static_assert(Radius>0);
        seen_[Radius*Width+Radius]=1;
    }
    template<class Read,class Diagonal=OptimisticDiagonalConnection>
    unsigned advance(unsigned budget,Read read,Diagonal diagonal={}) {
        unsigned used=0;
        while (!complete_ && used<budget && head_<tail_) {
            const auto p=queue_[head_++];++used;
            const int x=goalX_+p[0],y=goalY_+p[1];
            if (x==startX_ && y==startY_) { enclosed_=false;complete_=true;break; }
            const auto cell=read(x,y);
            if (cell.portal) { enclosed_=false;complete_=true;break; }
            if (!cell.passable) continue;
            if (std::abs(p[0])==Radius || std::abs(p[1])==Radius) {
                enclosed_=false;complete_=true;break;
            }
            constexpr int directions[8][2]={{1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}};
            for (const auto& d:directions) {
                const int nx=p[0]+d[0],ny=p[1]+d[1];
                if (d[0] && d[1] && !diagonal(goalX_+nx,goalY_+ny,x,y)) continue;
                auto& seen=seen_[(ny+Radius)*Width+nx+Radius];
                if (!seen) { seen=1;queue_[tail_++]={nx,ny}; }
            }
        }
        if (head_==tail_) complete_=true;
        return used;
    }
    bool complete() const { return complete_; }
    bool enclosed() const { return complete_ && enclosed_; }
};
// Certify all enclosed pockets near an endpoint together. Every passable edge
// anchor, layer portal, and the start is a seed. Eight-way connectivity is an
// overestimate of legal movement, so unmarked cells cannot be reached by the
// fine graph. Outside this bounded window the answer remains optimistic.
template<int Radius=12> class LocalGoalRegion {
    static constexpr int Width=2*Radius+1,Count=Width*Width;
    std::array<std::uint8_t,Count> passable_{},connected_{};
    std::array<unsigned,Count> queue_{};
    int left_,top_,startX_,startY_;
    unsigned cursor_=0,head_=0,tail_=0;
public:
    LocalGoalRegion(int startX,int startY,int goalX,int goalY):
        left_(goalX-Radius),top_(goalY-Radius),startX_(startX),startY_(startY) { static_assert(Radius>0); }
    template<class Read> unsigned advance(unsigned budget,Read read) {
        unsigned used=0;
        while (used<budget && cursor_<Count) {
            const auto i=cursor_++;++used;
            const int x=int(i)%Width,y=int(i)/Width;
            const auto cell=read(left_+x,top_+y);
            passable_[i]=cell.passable || cell.portal;
            if (passable_[i] && (cell.portal || x==0 || y==0 || x==Width-1 || y==Width-1 ||
                    (left_+x==startX_ && top_+y==startY_))) {
                connected_[i]=1;queue_[tail_++]=i;
            }
        }
        while (used<budget && cursor_==Count && head_<tail_) {
            const auto i=queue_[head_++];++used;
            const int x=int(i)%Width,y=int(i)/Width;
            for (int yy=std::max(0,y-1);yy<=std::min(Width-1,y+1);++yy)
                for (int xx=std::max(0,x-1);xx<=std::min(Width-1,x+1);++xx) {
                    const auto next=unsigned(yy*Width+xx);
                    if (passable_[next] && !connected_[next]) {
                        connected_[next]=1;queue_[tail_++]=next;
                    }
                }
        }
        return used;
    }
    bool complete() const { return cursor_==Count && head_==tail_; }
    bool potentiallyReachable(int x,int y) const {
        if (!complete()) throw std::logic_error("Incomplete local endpoint proof");
        x-=left_;y-=top_;
        return x<0 || y<0 || x>=Width || y>=Width || connected_[y*Width+x];
    }
};
template<int Radius = 8, class ReadCell,class DiagonalConnection=OptimisticDiagonalConnection>
bool localGoalIsEnclosed(int startX, int startY, int goalX, int goalY, ReadCell read,
    DiagonalConnection diagonalConnection={},std::vector<std::array<int,2>>* enclosedCells=nullptr) {
    static_assert(Radius > 0);
    constexpr int Width = Radius * 2 + 1;
    std::array<bool, Width * Width> seen{};
    std::array<bool, Width * Width> passable{};
    std::array<std::array<int, 2>, Width * Width> queue{};
    std::size_t begin = 0, end = 1;
    queue[0] = {0, 0}; seen[Radius * Width + Radius] = true;
    while (begin < end) {
        const auto offset = queue[begin++];
        const int x = goalX + offset[0], y = goalY + offset[1];
        if (x == startX && y == startY) return false;
        const auto cell = read(x, y);
        if (cell.portal) return false;
        if (!cell.passable) continue;
        passable[(offset[1]+Radius)*Width+offset[0]+Radius]=true;
        if (offset[0] == -Radius || offset[0] == Radius ||
            offset[1] == -Radius || offset[1] == Radius) return false;
        // This is an optimistic rejection proof. DX9 can admit a diagonal
        // whose terrain side is open even when that side's full footprint
        // fails occupancy checks, so allow all eight connections here.
        constexpr int adjacent[8][2]={{1,0},{0,1},{-1,0},{0,-1},
            {1,1},{-1,1},{-1,-1},{1,-1}};
        for (const auto& delta:adjacent) {
            const int nx = offset[0] + delta[0], ny = offset[1] + delta[1];
            // Traverse predecessors: the supplied predicate describes entry
            // from this neighbor into the current cell on a forward route.
            if (delta[0] && delta[1] && !diagonalConnection(goalX+nx,goalY+ny,x,y)) continue;
            const auto index = (ny + Radius) * Width + nx + Radius;
            if (!seen[index]) {
                seen[index] = true;
                queue[end++] = {nx, ny};
            }
        }
    }
    if (enclosedCells) {
        for (std::size_t i=0;i<end;++i) {
            const auto offset=queue[i];
            if (passable[(offset[1]+Radius)*Width+offset[0]+Radius])
                enclosedCells->push_back({goalX+offset[0],goalY+offset[1]});
        }
    }
    return true;
}

// An optimistic, terrain-only rejection test. Eight-way connectivity and
// portal unions may admit a route that the exact movement policy rejects,
// but never reject a route simply because of transient unit occupancy.
class ReachabilityCache {
    int width_ = 0, height_ = 0;
    bool dirty_ = true;
    bool fullDirty_ = true;
    int left_ = 0, top_ = 0, right_ = -1, bottom_ = -1;
    std::vector<std::uint16_t> cells_;
    std::vector<std::uint32_t> labels_, parents_;
    std::uint64_t rebuilds_ = 0;

    std::uint32_t root(std::uint32_t label) {
        while (parents_[label] != label) {
            parents_[label] = parents_[parents_[label]];
            label = parents_[label];
        }
        return label;
    }
    void join(std::uint32_t first, std::uint32_t second) {
        first = root(first); second = root(second);
        if (first != second) parents_[std::max(first, second)] = std::min(first, second);
    }
public:
    void invalidate() { dirty_ = fullDirty_ = true; }
    void invalidate(int left, int top, int right, int bottom) {
        if (!width_ || !height_) { invalidate(); return; }
        left = std::max(0, left); top = std::max(0, top);
        right = std::min(width_ - 1, right); bottom = std::min(height_ - 1, bottom);
        if (left > right || top > bottom) return;
        if (!dirty_ || right_ < left_) {
            left_ = left; top_ = top; right_ = right; bottom_ = bottom;
        } else {
            left_ = std::min(left_, left); top_ = std::min(top_, top);
            right_ = std::max(right_, right); bottom_ = std::max(bottom_, bottom);
        }
        dirty_ = true;
    }
    void reset() {
        labels_.clear(); parents_.clear(); cells_.clear(); width_ = height_ = 0;
        dirty_ = fullDirty_ = true; right_ = bottom_ = -1; rebuilds_ = 0;
    }
    std::uint64_t rebuilds() const { return rebuilds_; }
    template<class ReadCell>
    void prepare(int width, int height, ReadCell read) {
        if (width <= 0 || height <= 0 ||
            std::uint64_t(width) * height >= std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("Invalid reachability grid");
        if (!dirty_ && width == width_ && height == height_) return;
        if (width != width_ || height != height_) {
            fullDirty_ = true;
            cells_.assign(std::size_t(width) * height, 0);
        }
        width_ = width; height_ = height;
        if (fullDirty_) { left_ = top_ = 0; right_ = width - 1; bottom_ = height - 1; }
        bool changed = fullDirty_;
        for (int y = top_; y <= bottom_; ++y) for (int x = left_; x <= right_; ++x) {
            const auto cell = read(x, y);
            const auto encoded = std::uint16_t(cell.passable) | (std::uint16_t(cell.portal) << 1);
            auto& cached = cells_[std::size_t(y) * width + x];
            changed |= cached != encoded;
            cached = encoded;
        }
        dirty_ = fullDirty_ = false; right_ = bottom_ = -1;
        if (!changed) return;
        labels_.assign(std::size_t(width) * height, 0);
        parents_.clear(); parents_.push_back(0);
        std::array<std::uint32_t, 256> portals{};
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const auto index = std::size_t(y) * width + x;
            const auto encoded = cells_[index];
            if (!encoded) continue;
            const auto portal = encoded >> 1;
            std::uint32_t label = 0;
            const auto merge = [&](std::uint32_t other) {
                if (!other) return;
                if (!label) label = other;
                else join(label, other);
            };
            if (x) merge(labels_[index - 1]);
            if (y) {
                if (x) merge(labels_[index - width - 1]);
                merge(labels_[index - width]);
                if (x + 1 < width) merge(labels_[index - width + 1]);
            }
            if (!label) {
                label = static_cast<std::uint32_t>(parents_.size());
                parents_.push_back(label);
            }
            labels_[index] = label;
            if (portal) {
                if (portals[portal]) join(label, portals[portal]);
                else portals[portal] = label;
            }
        }
        for (auto& label : labels_) if (label) label = root(label);
        dirty_ = false; ++rebuilds_;
    }
    bool connected(int x1, int y1, int x2, int y2) const {
        if (dirty_) throw std::logic_error("Unsynchronized reachability cache");
        if (x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0 ||
            x1 >= width_ || x2 >= width_ || y1 >= height_ || y2 >= height_) return false;
        const auto first = labels_[std::size_t(y1) * width_ + x1];
        return first && first == labels_[std::size_t(y2) * width_ + x2];
    }
};
}
