module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

export module engine.navigation.movement.start_escape;
import engine.navigation.movement.goal_projection;

export namespace navigation {
enum class EscapeCell { Blocked, Transit, Exit };
struct StartEscape {
    std::vector<DestinationCell> path;
    unsigned examined=0;
};

// A small, bounded breadth-first search out of newly cramped terrain. Only
// cardinal edges are used, so a diagonal cannot cut through a blocked corner.
// Equal-length exits prefer progress toward the destination deterministically.
template<class Classify>
StartEscape findStartEscape(DestinationCell start, DestinationCell goal, Classify classify) {
    constexpr int radius=7, width=2*radius+1, count=width*width, center=count/2;
    StartEscape result;
    std::array<int,count> parents;
    parents.fill(-1);
    std::array<int,count> queue{};
    int head=0,tail=0;
    auto point=[&](int id) {
        return DestinationCell{int(std::int64_t(start.x)+id%width-radius),
            int(std::int64_t(start.y)+id/width-radius)};
    };
    auto distance=[&](DestinationCell p) {
        const auto dx=std::int64_t(p.x)-goal.x,dy=std::int64_t(p.y)-goal.y;
        return (dx<0?-dx:dx)+(dy<0?-dy:dy);
    };
    ++result.examined;
    const auto initial=classify(start.x,start.y);
    if (initial==EscapeCell::Blocked) return result;
    if (initial==EscapeCell::Exit) { result.path.push_back(start); return result; }
    parents[center]=center; queue[tail++]=center;
    while (head<tail) {
        const int current=queue[head++],x=current%width,y=current/width;
        std::array<int,4> candidates{{x+1<width?current+1:-1,y+1<width?current+width:-1,
            x?current-1:-1,y?current-width:-1}};
        auto valid=[&](int id) {
            if (id<0) return false;
            const auto xx=std::int64_t(start.x)+id%width-radius,yy=std::int64_t(start.y)+id/width-radius;
            return xx>=std::numeric_limits<int>::min() && xx<=std::numeric_limits<int>::max() &&
                yy>=std::numeric_limits<int>::min() && yy<=std::numeric_limits<int>::max();
        };
        std::sort(candidates.begin(),candidates.end(),[&](int a,int b) {
            if (!valid(a)) return false;
            if (!valid(b)) return true;
            const auto da=distance(point(a)),db=distance(point(b));
            return da!=db ? da<db : a<b;
        });
        for (const auto next : candidates) {
            if (!valid(next) || parents[next]!=-1) continue;
            parents[next]=current;
            const auto p=point(next);
            ++result.examined;
            const auto state=classify(p.x,p.y);
            if (state==EscapeCell::Blocked) continue;
            if (state==EscapeCell::Exit) {
                for (int id=next;;id=parents[id]) {
                    result.path.push_back(point(id));
                    if (id==center) break;
                }
                std::reverse(result.path.begin(),result.path.end());
                return result;
            }
            queue[tail++]=next;
        }
    }
    return result;
}
}
