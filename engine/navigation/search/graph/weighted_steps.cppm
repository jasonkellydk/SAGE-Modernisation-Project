module;
#include <algorithm>
#include <cstdint>

export module engine.navigation.search.graph.weighted_steps;
import engine.navigation.movement.occupancy_policy;
import engine.navigation.costs;

export namespace navigation {
struct WeightedStepQuery {
    int left=0,top=0,right=0,bottom=0,startX=0,startY=0,pathDiameter=0;
    bool escaping=false,pinchedStart=false,downhillOnly=false,corridor=false;
    int incomingX=0,incomingY=0;
    bool hasIncoming=false;
};
struct WeightedStepCell {
    bool exists=false,legal=false,terrainAllowed=false;
    OccupancyResult traffic;
    bool pinched=false, cornerOpen=true;
    unsigned terrainCost=0;
};

// Canonical edge order and integer costs are shared by live and captured graphs.
// Layer connections and destination terminal edges are supplied by the graph.
template<class ReadCell,class Height,class Clearance,class Emit>
void forEachWeightedStep(const WeightedStepQuery& query,int x,int y,
    ReadCell readCell,Height height,Clearance clearance,Emit emit) {
    constexpr int offsets[8][2]={{1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}};
    bool cardinal[4]{};
    bool haveParentHeight=false;
    float parentHeight=0;
    for (unsigned direction=0;direction<8;++direction) {
        const auto wideX=std::int64_t(x)+offsets[direction][0];
        const auto wideY=std::int64_t(y)+offsets[direction][1];
        if (wideX<query.left || wideX>query.right || wideY<query.top || wideY>query.bottom) continue;
        const int xx=int(wideX),yy=int(wideY);
        // A radius-zero unit beginning in a pinched cell follows the native
        // local escape contract: it may leave through a cardinal neighbour,
        // but never diagonally through the pinch. This is part of the graph
        // transition policy, so captured and live searches stay identical.
        if (query.escaping && query.pinchedStart && direction>=4) continue;
        const auto cell=readCell(xx,yy);
        if (!cell.exists) continue;
        if (direction>=4 && !cardinal[direction-4] && !cardinal[(direction-3)%4]) continue;
        if (query.downhillOnly) {
            const float nextHeight=height(xx,yy);
            if (!haveParentHeight) { parentHeight=height(x,y);haveParentHeight=true; }
            if (nextHeight>parentHeight) continue;
        }
        if (direction<4) cardinal[direction]=cell.cornerOpen &&
            (query.corridor ? cell.legal : (cell.terrainAllowed || query.escaping));
        if (!cell.legal && !query.escaping) continue;
        if (!cell.legal && query.pinchedStart && !cell.terrainAllowed) continue;
        unsigned cost=(direction<4?10u:14u)+cell.terrainCost;
        if (query.hasIncoming)
            cost+=turnCost(query.incomingX,query.incomingY,offsets[direction][0],offsets[direction][1]);
        if (!cell.legal) cost+=100;
        if (!query.corridor && cell.pinched) cost+=24;
        if (query.corridor) {
            const auto deficit=std::max<std::int64_t>(0,std::int64_t(query.pathDiameter)-clearance(xx,yy));
            cost+=6u*std::uint32_t(deficit);
        }
        if (cell.traffic.allyFixedCount) cost+=42;
        const auto dx=std::int64_t(xx)-query.startX,dy=std::int64_t(yy)-query.startY;
        if (cell.traffic.allyMoving && dx>-10 && dx<10 && dy>-10 && dy<10) cost+=42;
        emit(xx,yy,cost,query.escaping && (!cell.legal || cell.pinched));
    }
}
}
