module;
#include <cstddef>
#include <cstdint>
#include <vector>

export module engine.navigation.path.reconstruction;

export namespace navigation {
// DX9 8fd97e8e, Pathfinder::prependCells. Walk goal-to-start, excluding
// the exact starting position. Only non-cliff waypoints beside a cliff are
// protected. Same-position layer transitions keep the goalward cell's rule.
// Callbacks must read immutable inputs for the lifetime of this cursor.
class ReconstructionFlags {
    std::vector<std::uint8_t> flags_;
    std::size_t cursor_,next_;
    bool nextCliff_=false,propagating_=false,done_;
public:
    explicit ReconstructionFlags(std::size_t count)
        :flags_(count,0),cursor_(count),next_(count),done_(count<=1) {}
    bool done() const { return done_; }
    bool at(std::size_t index) const { return flags_[index]!=0; }
    template<class Cliff,class SamePosition>
    unsigned advance(unsigned budget,Cliff cliff,SamePosition samePosition) {
        unsigned used=0;
        while (used<budget && !done_) {
            const auto index=--cursor_;
            ++used;
            if (!propagating_) {
                if (next_==flags_.size() || !samePosition(index,next_)) {
                    const bool currentCliff=cliff(index);
                    flags_[index]=currentCliff || !nextCliff_;
                    if (currentCliff && next_<flags_.size() && !nextCliff_) flags_[next_]=0;
                    next_=index;nextCliff_=currentCliff;
                }
                if (cursor_==1) {
                    propagating_=true;cursor_=flags_.size()-1;
                    done_=cursor_<=1;
                }
            } else {
                // Both adapters may retain a different member of a collapsed
                // layer run. Propagate its final flag to every member.
                if (samePosition(index,index+1)) flags_[index]=flags_[index+1];
                done_=cursor_==1;
            }
        }
        return used;
    }
};
}
