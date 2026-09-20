module;

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

export module engine.navigation.search_workspace;

// C++ linkage permits forward declarations in the remaining game-facing header.
export extern "C++" {
namespace navigation {

using SearchIndex = std::uint32_t;
inline constexpr SearchIndex NoSearchIndex = std::numeric_limits<SearchIndex>::max();

// Priorities and IDs are separate contiguous arrays. Heap comparisons never
// dereference a game cell. The inverse array makes arbitrary removal O(log N).
template<unsigned Arity = 4>
class IndexedMinHeap {
    static_assert(Arity >= 2);
    std::vector<std::uint64_t> keys_;
    std::vector<SearchIndex> ids_;
    std::vector<SearchIndex> positions_;
    std::uint32_t ticket_ = 0;

    std::uint64_t nextKey(std::uint32_t cost) {
        if (ticket_ == std::numeric_limits<std::uint32_t>::max())
            throw std::overflow_error("Navigation insertion sequence overflow");
        return (std::uint64_t(cost) << 32) | ticket_++;
    }

    void place(SearchIndex position, SearchIndex id, std::uint64_t key) {
        ids_[position] = id;
        keys_[position] = key;
        positions_[id] = position;
    }
    void siftUp(SearchIndex position, SearchIndex id, std::uint64_t key) {
        while (position != 0) {
            const SearchIndex parent = (position - 1) / Arity;
            if (keys_[parent] <= key) break;
            place(position, ids_[parent], keys_[parent]);
            position = parent;
        }
        place(position, id, key);
    }
    void siftDown(SearchIndex position, SearchIndex id, std::uint64_t key) {
        for (;;) {
            const std::size_t first = std::size_t(position) * Arity + 1;
            if (first >= ids_.size()) break;
            SearchIndex best = static_cast<SearchIndex>(first);
            const auto end = std::min(first + Arity, ids_.size());
            if constexpr (Arity == 4) {
                if (end == first + 4) {
                    // Independent pairs avoid three serial, unpredictable
                    // branches for the overwhelmingly common full child set.
                    const SearchIndex left = keys_[first] < keys_[first + 1]
                        ? best : best + 1;
                    const SearchIndex right = keys_[first + 2] < keys_[first + 3]
                        ? best + 2 : best + 3;
                    best = keys_[left] < keys_[right] ? left : right;
                } else {
                    for (std::size_t child = first + 1; child < end; ++child)
                        if (keys_[child] < keys_[best]) best = static_cast<SearchIndex>(child);
                }
            } else {
                for (std::size_t child = first + 1; child < end; ++child)
                    if (keys_[child] < keys_[best]) best = static_cast<SearchIndex>(child);
            }
            if (key <= keys_[best]) break;
            place(position, ids_[best], keys_[best]);
            position = best;
        }
        place(position, id, key);
    }
public:
    void reserveIndices(std::size_t count) {
        if (count > positions_.size()) positions_.resize(count, NoSearchIndex);
    }
    bool empty() const { return ids_.empty(); }
    std::size_t size() const { return ids_.size(); }
    bool contains(SearchIndex id) const {
        return id < positions_.size() && positions_[id] < ids_.size() && ids_[positions_[id]]==id;
    }
    SearchIndex top() const { return empty() ? NoSearchIndex : ids_.front(); }
    SearchIndex at(std::size_t i) const { return ids_[i]; }
    SearchIndex next(SearchIndex id) const {
        assert(contains(id));
        const auto i = positions_[id] + 1;
        return i < ids_.size() ? ids_[i] : NoSearchIndex;
    }
    void push(SearchIndex id, std::uint32_t cost) {
        pushRanked(id,cost,static_cast<std::uint32_t>(nextKey(cost)));
    }
    // Explicit secondary priorities let A* prefer lower remaining distance
    // on equal-cost plateaus without changing the primary path cost.
    void pushRanked(SearchIndex id, std::uint32_t cost, std::uint32_t rank) {
        assert(!contains(id));
        reserveIndices(std::size_t(id)+1);
        const auto key=(std::uint64_t(cost)<<32)|rank;
        const auto position=static_cast<SearchIndex>(ids_.size());
        ids_.push_back(id);
        keys_.push_back(key);
        siftUp(position,id,key);
    }
    void updateRanked(SearchIndex id, std::uint32_t cost, std::uint32_t rank) {
        assert(contains(id));
        const auto position=positions_[id];
        const auto oldKey=keys_[position];
        const auto key=(std::uint64_t(cost)<<32)|rank;
        if (key<oldKey) siftUp(position,id,key);
        else if (key>oldKey) siftDown(position,id,key);
    }
    void erase(SearchIndex id) {
        assert(contains(id));
        const auto position = positions_[id];
        const auto lastId = ids_.back();
        const auto lastKey = keys_.back();
        ids_.pop_back();
        keys_.pop_back();
        positions_[id] = NoSearchIndex;
        if (position == ids_.size()) return;
        if (position && lastKey < keys_[(position - 1) / Arity])
            siftUp(position, lastId, lastKey);
        else
            siftDown(position, lastId, lastKey);
    }
    void update(SearchIndex id, std::uint32_t cost, bool renewInsertionOrder = false) {
        assert(contains(id));
        const auto position = positions_[id];
        const auto oldKey = keys_[position];
        const auto key = renewInsertionOrder ? nextKey(cost) :
            (std::uint64_t(cost) << 32) | std::uint32_t(oldKey);
        if (key < oldKey) siftUp(position, id, key);
        else if (key > oldKey) siftDown(position, id, key);
    }
    SearchIndex pop() {
        const auto id = top();
        if (id != NoSearchIndex) erase(id);
        return id;
    }
    // Cleanup does not need sorted order. Drain in O(N), not N heap removals.
    SearchIndex dropBack() {
        assert(!empty());
        const auto id = ids_.back();
        positions_[id] = NoSearchIndex;
        ids_.pop_back();
        keys_.pop_back();
        return id;
    }
    void clear() {
        // Membership validates both directions, so stale inverse positions
        // cannot survive clearing or alias a different ID after slot reuse.
        ids_.clear();
        keys_.clear();
        ticket_ = 0;
    }
    std::size_t storageBytes() const {
        return keys_.capacity() * sizeof(keys_[0]) + ids_.capacity() * sizeof(ids_[0])
             + positions_.capacity() * sizeof(positions_[0]);
    }
};

// Navigation priorities are integer costs with FIFO ties. Common costs use
// direct buckets, so removing the best cell does not sift a large heap. The
// overflow heap preserves the full 32-bit cost range on unusually large maps.
class SearchFrontier {
    static constexpr std::uint32_t BucketCount = 65536;
    struct Links {
        SearchIndex previous = NoSearchIndex, next = NoSearchIndex;
        SearchIndex position = NoSearchIndex;
        std::uint32_t cost = 0;
    };
    struct Bucket { SearchIndex first = NoSearchIndex, last = NoSearchIndex; };
    std::vector<Bucket> buckets_{BucketCount};
    std::array<std::uint64_t, BucketCount / 64> occupied_{};
    std::array<std::uint64_t, BucketCount / 4096> groups_{};
    std::uint64_t root_ = 0;
    std::vector<Links> links_;
    std::vector<SearchIndex> active_;
    IndexedMinHeap<4> overflow_;

    void attach(SearchIndex id, std::uint32_t cost) {
        auto& node = links_[id];
        node.cost = cost;
        if (cost >= BucketCount) { overflow_.push(id, cost); return; }
        auto& bucket = buckets_[cost];
        node.previous = bucket.last;
        node.next = NoSearchIndex;
        if (bucket.last != NoSearchIndex) links_[bucket.last].next = id;
        else {
            bucket.first = id;
            occupied_[cost / 64] |= std::uint64_t{1} << (cost % 64);
            groups_[cost / 4096] |= std::uint64_t{1} << (cost / 64 % 64);
            root_ |= std::uint64_t{1} << (cost / 4096);
        }
        bucket.last = id;
    }
    void detach(SearchIndex id) {
        const auto& node = links_[id];
        if (node.cost >= BucketCount) { overflow_.erase(id); return; }
        auto& bucket = buckets_[node.cost];
        if (node.previous != NoSearchIndex) links_[node.previous].next = node.next;
        else bucket.first = node.next;
        if (node.next != NoSearchIndex) links_[node.next].previous = node.previous;
        else bucket.last = node.previous;
        if (bucket.first == NoSearchIndex) {
            const auto word = node.cost / 64;
            occupied_[word] &= ~(std::uint64_t{1} << (node.cost % 64));
            if (!occupied_[word]) {
                const auto group = word / 64;
                groups_[group] &= ~(std::uint64_t{1} << (word % 64));
                if (!groups_[group]) root_ &= ~(std::uint64_t{1} << group);
            }
        }
    }
public:
    void reserveIndices(std::size_t count) {
        if (count > links_.size()) links_.resize(count);
    }
    bool empty() const { return active_.empty(); }
    std::size_t size() const { return active_.size(); }
    bool contains(SearchIndex id) const {
        return id < links_.size() && links_[id].position != NoSearchIndex;
    }
    SearchIndex top() const {
        if (!root_) return overflow_.top();
        const auto group = std::countr_zero(root_);
        const auto word = group * 64 + std::countr_zero(groups_[group]);
        return buckets_[word * 64 + std::countr_zero(occupied_[word])].first;
    }
    SearchIndex at(std::size_t index) const { return active_[index]; }
    SearchIndex next(SearchIndex id) const {
        assert(contains(id));
        const auto position = (links_[id].position + 1) % active_.size();
        const auto nextId = active_[position];
        return nextId == top() ? NoSearchIndex : nextId;
    }
    void push(SearchIndex id, std::uint32_t cost) {
        assert(!contains(id));
        reserveIndices(std::size_t(id) + 1);
        links_[id].position = static_cast<SearchIndex>(active_.size());
        active_.push_back(id);
        attach(id, cost);
    }
    void erase(SearchIndex id) {
        assert(contains(id));
        detach(id);
        const auto position = links_[id].position;
        const auto last = active_.back();
        active_[position] = last;
        links_[last].position = position;
        active_.pop_back();
        links_[id].position = NoSearchIndex;
    }
    void reinsert(SearchIndex id, std::uint32_t cost) {
        assert(contains(id));
        detach(id);
        attach(id, cost);
    }
    SearchIndex pop() {
        const auto id = top();
        if (id != NoSearchIndex) erase(id);
        return id;
    }
    SearchIndex dropBack() {
        assert(!empty());
        const auto id = active_.back();
        erase(id);
        return id;
    }
    void clear() {
        while (!empty()) dropBack();
        overflow_.clear();
    }
    std::size_t storageBytes() const {
        return buckets_.capacity() * sizeof(Bucket) + sizeof(occupied_) + sizeof(groups_)
            + links_.capacity() * sizeof(Links) + active_.capacity() * sizeof(SearchIndex)
            + overflow_.storageBytes();
    }
};

// Slots grow geometrically and remain valid across vector reallocations. Only
// the cold owner array contains pointers; costs, parents and queue entries use IDs.
template<class Owner>
class SearchWorkspace {
public:
    enum State : std::uint8_t { Unseen, Open, Closed };
    std::vector<std::uint32_t> g, f;
    std::vector<SearchIndex> parent;
    std::vector<std::uint16_t> x, y;
    std::vector<std::uint32_t> generation;
    std::vector<State> state;
    std::vector<Owner*> owner;
    std::vector<std::uint32_t> goalUnit, positionUnit, goalAircraft;
    SearchFrontier open;
private:
    std::vector<SearchIndex> free_;
    std::vector<std::uint8_t> allocated_;
    std::vector<SearchIndex> closed_;
    std::vector<SearchIndex> closedPosition_;
    std::uint32_t epoch_ = 1;

public:
    std::size_t capacity() const { return owner.size(); }
    std::uint32_t epoch() const noexcept { return epoch_; }
    std::size_t allocatedCount() const { return capacity() - free_.size(); }
    bool allocated(SearchIndex id) const { return id < capacity() && allocated_[id]; }
    void reserveSlots(std::size_t count) {
        if (count <= capacity()) return;
        if (count >= NoSearchIndex) throw std::length_error("Navigation index space exhausted");
        const auto old = capacity();
        g.resize(count); f.resize(count); parent.resize(count, NoSearchIndex);
        x.resize(count); y.resize(count); generation.resize(count); state.resize(count, Unseen);
        owner.resize(count); goalUnit.resize(count); positionUnit.resize(count); goalAircraft.resize(count);
        allocated_.resize(count); closedPosition_.resize(count, NoSearchIndex);
        open.reserveIndices(count);
        free_.reserve(count);
        for (auto i = count; i > old; --i) free_.push_back(static_cast<SearchIndex>(i - 1));
    }
    SearchIndex acquire(Owner* value, std::uint16_t px, std::uint16_t py) {
        if (free_.empty()) reserveSlots(std::max<std::size_t>(64, capacity() * 2));
        const auto id = free_.back();
        free_.pop_back();
        allocated_[id] = 1;
        owner[id] = value; x[id] = px; y[id] = py;
        goalUnit[id] = positionUnit[id] = goalAircraft[id] = 0;
        generation[id] = 0;
        touch(id);
        return id;
    }
    void release(SearchIndex id) {
        assert(allocated(id) && !isOpen(id) && !isClosed(id));
        allocated_[id] = 0;
        owner[id] = nullptr;
        generation[id] = 0;
        free_.push_back(id);
    }
    void beginSearch() {
        assert(open.empty() && closed_.empty());
        open.clear();
        if (++epoch_ == 0) {
            std::fill(generation.begin(), generation.end(), 0);
            epoch_ = 1;
        }
    }
    void touch(SearchIndex id) {
        assert(allocated(id));
        if (generation[id] == epoch_) return;
        generation[id] = epoch_;
        state[id] = Unseen;
        parent[id] = NoSearchIndex;
        g[id] = f[id] = 0;
    }
    bool isOpen(SearchIndex id) const {
        return generation[id] == epoch_ && state[id] == Open;
    }
    bool isClosed(SearchIndex id) const {
        return generation[id] == epoch_ && state[id] == Closed;
    }
    SearchIndex parentOf(SearchIndex id) const {
        return generation[id] == epoch_ ? parent[id] : NoSearchIndex;
    }
    void pushOpen(SearchIndex id) {
        touch(id);
        if (state[id] == Open) {
            // Preserve the planner's re-insertion tie policy with one sift,
            // without an erase followed by a second heap insertion.
            open.reinsert(id, f[id]);
            return;
        }
        assert(state[id] == Unseen);
        open.push(id, f[id]);
        state[id] = Open;
    }
    void eraseOpen(SearchIndex id) {
        open.erase(id);
        state[id] = Unseen;
    }
    SearchIndex drainOpen() {
        const auto id = open.dropBack();
        state[id] = Unseen;
        return id;
    }
    void close(SearchIndex id) {
        touch(id);
        assert(state[id] == Unseen);
        closedPosition_[id] = static_cast<SearchIndex>(closed_.size());
        closed_.push_back(id);
        state[id] = Closed;
    }
    void eraseClosed(SearchIndex id) {
        assert(isClosed(id));
        const auto position = closedPosition_[id];
        const auto last = closed_.back();
        closed_[position] = last;
        closedPosition_[last] = position;
        closed_.pop_back();
        closedPosition_[id] = NoSearchIndex;
        state[id] = Unseen;
    }
    SearchIndex closedHead() const { return closed_.empty() ? NoSearchIndex : closed_.back(); }
    SearchIndex nextClosed(SearchIndex id) const {
        assert(isClosed(id));
        const auto position = closedPosition_[id];
        return position == 0 ? NoSearchIndex : closed_[position - 1];
    }
    void releaseStorage() { *this = SearchWorkspace{}; }
    std::size_t storageBytes() const {
        return (g.capacity() + f.capacity() + generation.capacity() + goalUnit.capacity()
              + positionUnit.capacity() + goalAircraft.capacity()) * sizeof(std::uint32_t)
             + (parent.capacity() + free_.capacity() + closed_.capacity() + closedPosition_.capacity()) * sizeof(SearchIndex)
             + (x.capacity() + y.capacity()) * sizeof(std::uint16_t)
             + state.capacity() * sizeof(State) + allocated_.capacity()
             + owner.capacity() * sizeof(Owner*) + open.storageBytes();
    }
};
} // namespace navigation
} // extern "C++"
