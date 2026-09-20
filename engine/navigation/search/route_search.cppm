module;
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>
#include <utility>
#include <optional>
#include <cmath>
#include <functional>
#include <stdexcept>

export module engine.navigation.search.route_search;
import engine.navigation.search_workspace;

export namespace navigation {
struct RouteEdge { std::uint32_t target, cost; };
struct RouteNeighborProgress { std::uint32_t work=0; bool complete=false; };
struct RouteSearchResult {
    std::vector<std::uint32_t> nodes;
    std::uint32_t cost=0, expanded=0;
    std::uint64_t work=0;
    bool usedFallback=false;
    bool found() const { return !nodes.empty(); }
};

enum class RouteSearchStatus { Idle, Searching, Found, Unreachable, Cancelled };

// Integer A* for weighted terrain and layer portals. Search state is owned by
// the query workspace, never embedded in terrain/occupancy cells. Reusing it
// clears the frontier in constant time; generation tags avoid a map-wide reset.
class RouteSearchWorkspace {
    static constexpr std::uint32_t Infinity=std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> generations_, costs_, parents_;
    IndexedMinHeap<4> frontier_;
    std::vector<RouteEdge> edges_;
    std::vector<std::uint32_t> fallbackNodes_;
    std::optional<double> fallbackMinimum_;
    std::function<double(std::uint32_t,std::uint32_t)> fallbackRemainingBound_;
    std::function<std::optional<double>(std::uint32_t,std::uint32_t)> fallbackRankLowerBound_;
    std::uint32_t fallbackBest_=Infinity;
    double fallbackScore_=std::numeric_limits<double>::infinity();
    std::size_t fallbackCursor_=0;
    bool rankingFallback_=false;
    std::uint32_t generation_=0, nodeCount_=0, goal_=0;
    RouteSearchResult result_;
    RouteSearchStatus status_=RouteSearchStatus::Idle;
    std::vector<std::uint32_t> buildingNodes_;
    std::vector<std::uint32_t> settled_;
    bool consistentHeuristic_=false;
    bool referenceOrder_=false;
    std::uint32_t parentCursor_=Infinity;
    std::size_t reverseCursor_=0;
    bool finishing_=false;
    std::function<bool(std::uint32_t)> expansionFilter_;
    std::vector<std::uint32_t> deferred_,deferredGenerations_;
    std::size_t deferredCursor_=0,deferredWrite_=0;
    bool widening_=false;
    std::uint32_t incrementalCurrent_=Infinity;
    std::size_t incrementalEdgeCursor_=0;
    bool incrementalNeighborsComplete_=false;
    void startFinishing(std::uint32_t end) {
        buildingNodes_.clear();
        parentCursor_=end;
        reverseCursor_=0;
        finishing_=true;
        status_=RouteSearchStatus::Searching;
        result_.cost=costs_[end];
    }
    RouteSearchStatus finish(std::uint32_t budget) {
        while (budget && parentCursor_!=Infinity) {
            buildingNodes_.push_back(parentCursor_);
            parentCursor_=parents_[parentCursor_];
            --budget;++result_.work;
        }
        if (parentCursor_!=Infinity) return status_;
        while (budget && reverseCursor_<buildingNodes_.size()/2) {
            std::swap(buildingNodes_[reverseCursor_],buildingNodes_[buildingNodes_.size()-1-reverseCursor_]);
            ++reverseCursor_;--budget;++result_.work;
        }
        if (reverseCursor_<buildingNodes_.size()/2) return status_;
        result_.nodes=std::move(buildingNodes_);
        finishing_=false;
        status_=RouteSearchStatus::Found;
        return status_;
    }
public:
    // DX9 uses the caller's E,N,W,S,NE,NW,SW,SE order and FIFO total-cost ties.
    void useReferenceOrder(bool enabled=true) { referenceOrder_=enabled; }
    bool observed(std::uint32_t node) const {
        return node<generations_.size() && generations_[node]==generation_;
    }
    std::uint32_t costOf(std::uint32_t node) const {
        return observed(node) ? costs_[node] : Infinity;
    }
    template<class Heuristic>
    void relax(std::uint32_t current,std::uint32_t next,std::uint32_t edgeCost,const Heuristic& heuristic) {
        if (next>=nodeCount_ || !observed(current)) return;
        const auto candidate=std::uint64_t(costs_[current])+edgeCost;
        relaxToCost(current,next,candidate,heuristic);
    }
    template<class Heuristic>
    void relaxToCost(std::uint32_t current,std::uint32_t next,std::uint64_t candidate,const Heuristic& heuristic) {
        if (!observed(current)) return;
        relaxFromObserved(current,next,candidate,heuristic);
    }
    // The caller already owns an observed predecessor. Return the target's
    // actual best cost even if this candidate loses: DX9 phase traversal must
    // continue from that cheaper cost rather than stop or carry the candidate.
    template<class Heuristic>
    std::uint32_t relaxFromObserved(std::uint32_t current,std::uint32_t next,std::uint64_t candidate,const Heuristic& heuristic) {
        if (next>=nodeCount_) return Infinity;
        const auto existing=generations_[next]==generation_ ? costs_[next] : Infinity;
        if (candidate>=existing) return existing;
        generations_[next]=generation_;
        costs_[next]=static_cast<std::uint32_t>(candidate);
        parents_[next]=current;
        const auto remaining=heuristic(next);
        const auto priority=static_cast<std::uint32_t>(std::min<std::uint64_t>(candidate+remaining,Infinity));
        if (referenceOrder_) {
            if (frontier_.contains(next)) frontier_.update(next,priority,true);
            else frontier_.push(next,priority);
        } else {
            if (frontier_.contains(next)) frontier_.updateRanked(next,priority,remaining);
            else frontier_.pushRanked(next,priority,remaining);
        }
        return costs_[next];
    }
    std::uint32_t parentOf(std::uint32_t node) const {
        return node<generations_.size() && generations_[node]==generation_ ? parents_[node] : Infinity;
    }
    RouteSearchStatus status() const { return status_; }
    const RouteSearchResult& result() const { return result_; }
    void setExpansionFilter(std::function<bool(std::uint32_t)> filter) { expansionFilter_=std::move(filter); }
    void beginWidening() {
        deferredCursor_=deferredWrite_=0;widening_=true;
    }
    bool widening() const { return widening_; }
    // Reactivate suspended frontier entries in their original deferral order.
    // Costs, parents and generations survive corridor growth. Requeueing is
    // itself sliced so a long corridor boundary cannot monopolize a frame.
    template<class Heuristic> unsigned advanceWidening(unsigned budget,Heuristic heuristic) {
        unsigned used=0;
        while (widening_ && used<budget && deferredCursor_<deferred_.size()) {
            const auto node=deferred_[deferredCursor_++];++used;++result_.work;
            if (expansionFilter_ && !expansionFilter_(node)) deferred_[deferredWrite_++]=node;
            else if (!frontier_.contains(node)) {
                const auto remaining=heuristic(node);
                const auto priority=std::uint32_t(std::min<std::uint64_t>(std::uint64_t(costs_[node])+remaining,Infinity));
                if (referenceOrder_) frontier_.push(node,priority);
                else frontier_.pushRanked(node,priority,remaining);
            }
        }
        if (widening_ && deferredCursor_==deferred_.size()) {
            deferred_.resize(deferredWrite_);widening_=false;
            status_=frontier_.empty()?RouteSearchStatus::Unreachable:RouteSearchStatus::Searching;
        }
        return used;
    }
    void cancel() {
        incrementalCurrent_=Infinity;
        incrementalEdgeCursor_=0;
        incrementalNeighborsComplete_=false;
        expansionFilter_={};deferred_.clear();widening_=false;
        frontier_.clear();
        edges_.clear();
        fallbackNodes_.clear();
        fallbackMinimum_.reset();
        fallbackRemainingBound_={};
        fallbackRankLowerBound_={};
        fallbackBest_=Infinity;
        fallbackScore_=std::numeric_limits<double>::infinity();
        fallbackCursor_=0;
        rankingFallback_=false;
        buildingNodes_.clear();
        parentCursor_=Infinity;
        reverseCursor_=0;
        finishing_=false;
        result_={};
        status_=RouteSearchStatus::Cancelled;
    }
    // A paused query requires the same graph and heuristic for every advance.
    // Callbacks are borrowed only during calls; no game objects are retained.
    template<class Heuristic>
    RouteSearchStatus begin(std::uint32_t nodeCount, std::uint32_t start,
        std::uint32_t goal, Heuristic heuristic,bool consistentHeuristic=false) {
        cancel();
        nodeCount_=nodeCount;
        goal_=goal;
        status_=RouteSearchStatus::Unreachable;
        if (!nodeCount || start>=nodeCount || goal>=nodeCount) return status_;
        if (generations_.size()<nodeCount) {
            generations_.resize(nodeCount,0);
            costs_.resize(nodeCount);
            parents_.resize(nodeCount);
        }
        if (settled_.size()<nodeCount) settled_.resize(nodeCount,0);
        if (deferredGenerations_.size()<nodeCount) deferredGenerations_.resize(nodeCount,0);
        consistentHeuristic_=consistentHeuristic;
        frontier_.reserveIndices(nodeCount);
        if (++generation_==0) {
            std::fill(deferredGenerations_.begin(),deferredGenerations_.end(),0);
            std::fill(generations_.begin(),generations_.end(),0);
            std::fill(settled_.begin(),settled_.end(),0);
            generation_=1;
        }
        generations_[start]=generation_; costs_[start]=0; parents_[start]=Infinity;
        if (referenceOrder_) frontier_.push(start,heuristic(start));
        else frontier_.pushRanked(start,heuristic(start),heuristic(start));
        status_=RouteSearchStatus::Searching;
        return status_;
    }
    template<class Heuristic, class Neighbors>
    RouteSearchStatus advance(std::uint32_t expansionBudget, Heuristic heuristic, Neighbors neighbors) {
        if (status_!=RouteSearchStatus::Searching) return status_;
        if (finishing_) return finish(expansionBudget);
        for (std::uint32_t used=0;used<expansionBudget && !frontier_.empty();++used) {
            const auto current=frontier_.pop();
            if (expansionFilter_ && !expansionFilter_(current)) {
                ++result_.work;
                if (deferredGenerations_[current]!=generation_) {
                    deferredGenerations_[current]=generation_;deferred_.push_back(current);
                }
                continue;
            }
            if (consistentHeuristic_ && settled_[current]==generation_) continue;
            if (consistentHeuristic_) settled_[current]=generation_;
            ++result_.expanded;
            ++result_.work;
            if (current==goal_) {
                frontier_.clear();
                startFinishing(current);
                return finish(expansionBudget-used-1);
            }
            edges_.clear();
            neighbors(current,[&](std::uint32_t target,std::uint32_t cost) {
                if (target<nodeCount_ && cost<Infinity) edges_.push_back({target,cost});
            });
            // Canonical successor order makes equal-cost choices independent
            // of occupancy container order or the caller's enumeration order.
            // Weighted navigation emits only a handful of successors per
            // cell; insertion ordering avoids invoking the general-purpose
            // sort machinery millions of times while retaining the exact
            // comparator and deterministic result.
            for (std::size_t i=1;!referenceOrder_ && i<edges_.size();++i) {
                const auto value=edges_[i];
                std::size_t j=i;
                while (j && (value.target<edges_[j-1].target ||
                    (value.target==edges_[j-1].target && value.cost<edges_[j-1].cost))) {
                    edges_[j]=edges_[j-1];
                    --j;
                }
                edges_[j]=value;
            }
            for (const auto& edge:edges_) {
                relax(current,edge.target,edge.cost,heuristic);
            }
        }
        if (frontier_.empty()) status_=RouteSearchStatus::Unreachable;
        return status_;
    }
    template<class Heuristic, class Neighbors>
    RouteSearchResult find(std::uint32_t nodeCount, std::uint32_t start,
        std::uint32_t goal, Heuristic heuristic, Neighbors neighbors) {
        begin(nodeCount,start,goal,heuristic);
        while (status_==RouteSearchStatus::Searching)
            advance(Infinity,heuristic,neighbors);
        auto finished=std::move(result_);
        result_={};
        status_=RouteSearchStatus::Idle;
        return finished;
    }
    template<class Heuristic>
    RouteSearchStatus beginWithFallback(std::uint32_t nodeCount, std::uint32_t start,
        std::uint32_t goal, Heuristic heuristic,
        std::optional<double> provenMinimum = {},
        std::function<double(std::uint32_t,std::uint32_t)> remainingBound = {},
        bool consistentHeuristic=false,
        std::function<std::optional<double>(std::uint32_t,std::uint32_t)> rankLowerBound = {}) {
        begin(nodeCount,start,goal,heuristic,consistentHeuristic);
        fallbackMinimum_=provenMinimum;
        // Optional bound for every still-unsettled endpoint, evaluated with the
        // popped node's cost and consistent heuristic. Only use it when the
        // exact goal is unavailable. Equal ranks still use canonical node order.
        fallbackRemainingBound_=std::move(remainingBound);
        fallbackRankLowerBound_=std::move(rankLowerBound);
        return status_;
    }
    // Fixed-goal reference-order search with cooperative neighbor expansion.
    // The callback owns its resumable state and reports consumed work. It may
    // relax phase-line cells directly, but ordinary emitted edges stay queued
    // until expansion completes, exactly as in advance(). Do not mix advance
    // APIs within a query. Graph/policy inputs must remain immutable between
    // calls; this API alone does not make live-world queries safe to suspend.
    template<class Heuristic,class StepNeighbors>
    std::uint32_t advanceReferenceSlice(std::uint32_t budget,const Heuristic& heuristic,StepNeighbors&& stepNeighbors) {
        if (!referenceOrder_) throw std::logic_error("Sliced expansion requires reference order");
        if (!budget || status_!=RouteSearchStatus::Searching) return 0;
        std::uint32_t used=0;
        while (used<budget && status_==RouteSearchStatus::Searching) {
            if (finishing_) {
                const auto before=result_.work;
                finish(budget-used);
                used+=static_cast<std::uint32_t>(result_.work-before);
                break;
            }
            if (incrementalCurrent_==Infinity) {
                if (frontier_.empty()) { status_=RouteSearchStatus::Unreachable;break; }
                const auto current=frontier_.pop();
                ++used;++result_.work;
                if (expansionFilter_ && !expansionFilter_(current)) {
                    if (deferredGenerations_[current]!=generation_) {
                        deferredGenerations_[current]=generation_;deferred_.push_back(current);
                    }
                    continue;
                }
                if (consistentHeuristic_ && settled_[current]==generation_) continue;
                if (consistentHeuristic_) settled_[current]=generation_;
                ++result_.expanded;
                if (current==goal_) {
                    frontier_.clear();startFinishing(current);continue;
                }
                incrementalCurrent_=current;
                incrementalEdgeCursor_=0;
                incrementalNeighborsComplete_=false;
                edges_.clear();
            }
            if (used==budget) break;
            if (!incrementalNeighborsComplete_) {
                const auto progress=stepNeighbors(incrementalCurrent_,budget-used,
                    [&](std::uint32_t target,std::uint32_t cost) {
                        if (target<nodeCount_ && cost<Infinity) edges_.push_back({target,cost});
                    });
                if (progress.work>budget-used || (!progress.work && !progress.complete))
                    throw std::logic_error("Neighbor slice exceeded its budget or made no progress");
                used+=progress.work;result_.work+=progress.work;
                incrementalNeighborsComplete_=progress.complete;
                if (!progress.complete) continue;
            }
            while (used<budget && incrementalEdgeCursor_<edges_.size()) {
                const auto edge=edges_[incrementalEdgeCursor_++];
                relax(incrementalCurrent_,edge.target,edge.cost,heuristic);
                ++used;++result_.work;
            }
            if (incrementalEdgeCursor_==edges_.size()) incrementalCurrent_=Infinity;
        }
        if (status_==RouteSearchStatus::Searching && !finishing_ &&
            incrementalCurrent_==Infinity && frontier_.empty()) status_=RouteSearchStatus::Unreachable;
        return used;
    }
    // Budget counts expansions, deferred endpoint evaluations and reconstruction.
    // Keep the graph, heuristic and endpoint policy fixed across advances.
    template<class Heuristic, class Neighbors, class Rank>
    RouteSearchStatus advanceWithFallback(std::uint32_t budget,
        Heuristic heuristic, Neighbors neighbors, Rank rank) {
        if (status_!=RouteSearchStatus::Searching || !budget) return status_;
        if (finishing_) return finish(budget);
        const auto consider=[&](std::uint32_t node) {
            if (fallbackRankLowerBound_ && fallbackBest_!=Infinity) {
                const auto lower=fallbackRankLowerBound_(node,costs_[node]);
                if (!lower || *lower>fallbackScore_) return;
            }
            const auto score=rank(node,costs_[node]);
            if (score && std::isfinite(*score) &&
                (*score<fallbackScore_ || (*score==fallbackScore_ && node<fallbackBest_))) {
                fallbackBest_=node; fallbackScore_=*score;
            }
        };
        if (!rankingFallback_) {
            const auto before=result_.work;
            advance(budget,heuristic,[&](std::uint32_t node,auto emit) {
                if (fallbackMinimum_ || fallbackRemainingBound_) consider(node);
                else fallbackNodes_.push_back(node);
                // Only supply this bound when the exact goal is known to be
                // unavailable and every eligible endpoint scores at least it.
                if (fallbackMinimum_ && fallbackScore_<=*fallbackMinimum_) {
                    frontier_.clear();
                    return;
                }
                if (fallbackRemainingBound_ && fallbackScore_<fallbackRemainingBound_(costs_[node],heuristic(node))) {
                    frontier_.clear();
                    return;
                }
                neighbors(node,emit);
            });
            budget-=static_cast<std::uint32_t>(result_.work-before);
            if (status_!=RouteSearchStatus::Unreachable) return status_;
            rankingFallback_=true;
        }
        if (!fallbackMinimum_ && !fallbackRemainingBound_) {
            while (budget && fallbackCursor_<fallbackNodes_.size()) {
                consider(fallbackNodes_[fallbackCursor_++]);
                ++result_.work;
                --budget;
            }
            if (fallbackCursor_<fallbackNodes_.size()) {
                status_=RouteSearchStatus::Searching;
                return status_;
            }
        }
        status_=RouteSearchStatus::Unreachable;
        if (fallbackBest_!=Infinity) {
            result_.usedFallback=true;
            startFinishing(fallbackBest_);
            return finish(budget);
        }
        return status_;
    }
    // Prefer the exact goal, otherwise rank the entire reachable graph.
    // Endpoint rank must not increase when its shortest-path cost decreases.
    template<class Heuristic, class Neighbors, class Rank>
    RouteSearchResult findWithFallback(std::uint32_t nodeCount, std::uint32_t start,
        std::uint32_t goal, Heuristic heuristic, Neighbors neighbors, Rank rank,
        std::optional<double> provenMinimum = {}) {
        beginWithFallback(nodeCount,start,goal,heuristic,provenMinimum);
        while (status_==RouteSearchStatus::Searching)
            advanceWithFallback(Infinity,heuristic,neighbors,rank);
        auto finished=std::move(result_);
        result_={};
        status_=RouteSearchStatus::Idle;
        return finished;
    }
};
}
