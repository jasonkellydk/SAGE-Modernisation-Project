module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

export module engine.navigation.search.cell_query_cache;

export namespace navigation {
// First observations for one search, including its suspended slices.
// begin() invalidates every value;
// storage is retained, but world state never survives into the next query.
template<class Value, class Generation = std::uint32_t>
class CellQueryCache {
    struct Entry { Generation generation = 0; Value value{}; };
    std::vector<Entry> entries_;
    std::size_t size_ = 0;
    Generation generation_ = 0;
public:
    void begin(std::size_t size) {
        if (entries_.size() < size) entries_.resize(size);
        size_ = size;
        if (generation_ == std::numeric_limits<Generation>::max()) {
            for (auto& entry : entries_) entry.generation = 0;
            generation_ = 1;
        } else ++generation_;
    }
    template<class Compute>
    const Value& get(std::size_t index, Compute compute) {
        if (index >= size_) throw std::out_of_range("Search cell index");
        auto& entry = entries_[index];
        if (entry.generation != generation_) {
            entry.value = compute();
            entry.generation = generation_;
        }
        return entry.value;
    }
    // Keep the common hit independent of the potentially large compute
    // callback, so fine-search line walks can inline this small lookup.
    const Value* find(std::size_t index) const {
        if (index>=size_ || entries_[index].generation!=generation_) return nullptr;
        return &entries_[index].value;
    }
};

}
