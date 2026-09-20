module;
#include <utility>

export module engine.navigation.spatial.storage.contact_pair_pool;
import engine.navigation.spatial.contact_pairs;
export import engine.navigation.spatial.storage.contact_queue;

export namespace navigation {
// Retain allocation, never membership. Each active lease owns its table, so a
// nested operation cannot clear or overwrite an outer operation's contacts.
class ContactPairPool {
    ContactPairs cached_;
    ContactQueue cachedQueue_;
public:
    ContactPairPool() = default;
    ContactPairPool(const ContactPairPool&) = delete;
    ContactPairPool& operator=(const ContactPairPool&) = delete;
    class Lease {
        friend class ContactPairPool;
        ContactPairPool& owner_;
        ContactPairs pairs_;
        ContactQueue queue_;
        explicit Lease(ContactPairPool& owner)
            : owner_(owner), pairs_(std::exchange(owner.cached_,{})),
              queue_(std::exchange(owner.cachedQueue_,{})) {}
    public:
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease() {
            pairs_.clear();
            queue_.clear();
            if (pairs_.capacity()>=owner_.cached_.capacity())
                std::swap(pairs_,owner_.cached_);
            if (queue_.capacity()>=owner_.cachedQueue_.capacity())
                std::swap(queue_,owner_.cachedQueue_);
        }
        ContactPairs& pairs() { return pairs_; }
        ContactQueue& queue() { return queue_; }
    };
    // The pool must outlive all its leases.
    Lease acquire() { return Lease(*this); }
};
}
