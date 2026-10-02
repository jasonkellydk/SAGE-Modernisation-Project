export module Graphics.Scene.StaticDrawOrder;
import std;

namespace Graphics {

// Return the input indices belonging to authored static layers, from back to
// front (higher levels first), retaining encounter order within each layer.
// Zero bypasses this queue: the caller submits its ordinary opaque or sorted
// transparent geometry separately, and flushes transparency after static draws.
// This frame-local permutation borrows no renderer objects and owns no GPU state.
// Negative levels are invalid; rejection leaves the previous result intact.
export bool Build_Static_Draw_Order(std::span<const std::int32_t> levels,
    std::vector<std::size_t>& result)
{
    std::vector<std::size_t> ordered;
    ordered.reserve(levels.size());
    for(std::size_t index=0;index<levels.size();++index) {
        if(levels[index]<0) return false;
        if(levels[index]>0) ordered.push_back(index);
    }
    std::stable_sort(ordered.begin(),ordered.end(),[levels](std::size_t left,std::size_t right) {
        return levels[left]>levels[right];
    });
    result=std::move(ordered);
    return true;
}

}
