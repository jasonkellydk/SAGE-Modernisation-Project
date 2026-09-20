module;
#include <cstddef>
#include <cstdint>
#include <vector>

export module engine.navigation.spatial.storage.contact_queue;

export namespace navigation {
struct QueuedContact { std::uintptr_t first=0,second=0; };
// Opaque identities, with zero reserved for invalidated contacts. Callbacks
// receive values so removal and vector growth cannot invalidate their inputs.
class ContactQueue {
    std::vector<QueuedContact> entries_;
    std::uint64_t generation_=0;
public:
    std::size_t size() const { return entries_.size(); }
    std::size_t capacity() const { return entries_.capacity(); }
    void clear() { entries_.clear(); ++generation_; }
    void append(std::uintptr_t first,std::uintptr_t second) { entries_.push_back({first,second}); }
    void invalidate(std::size_t index) { entries_.at(index)={}; }
    template<class Remove>
    void removeIdentity(std::uintptr_t identity,Remove remove) {
        const auto generation=generation_;
        for (std::size_t index=entries_.size();index && generation==generation_;) {
            --index;
            const auto pair=entries_[index];
            if (pair.first && pair.second && (pair.first==identity || pair.second==identity)) {
                entries_[index]={};
                remove(pair);
            }
        }
    }
    template<class Visit>
    void visitReverse(Visit visit) {
        // Contacts appended by callbacks are not part of the active traversal,
        // matching insertion at the head of the former linked list.
        const auto generation=generation_;
        for (std::size_t index=entries_.size();index && generation==generation_;) {
            --index;
            const auto pair=entries_[index];
            if (pair.first && pair.second) visit(index,pair);
        }
    }
};
}
