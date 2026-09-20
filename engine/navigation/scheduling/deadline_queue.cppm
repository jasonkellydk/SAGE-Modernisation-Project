module;
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

export module engine.navigation.scheduling.deadline_queue;

export namespace navigation::scheduling {
// Only distinct deadlines participate in the heap. Equal-deadline entries
// run in insertion order, independent of addresses and hash-table iteration.
template<class T>
class DeadlineQueue {
    static_assert(std::is_trivially_copyable_v<T>);
    static constexpr auto none=std::numeric_limits<std::size_t>::max();
    struct Bucket {
        std::uint32_t priority;
        std::size_t head=none,tail=none,heapIndex=none;
    };
    struct Node {
        T value{};
        Bucket* owner=nullptr;
        std::size_t previous=none,next=none;
    };
    std::unordered_map<std::uint32_t,Bucket> buckets_;
    std::vector<Bucket*> heap_;
    std::vector<Node> nodes_;
    std::size_t free_=none,count_=0;

    std::size_t up(std::size_t index) {
        auto* moving=heap_[index];
        while (index) {
            const auto parent=(index-1)/2;
            if (heap_[parent]->priority<=moving->priority) break;
            heap_[index]=heap_[parent]; heap_[index]->heapIndex=index;
            index=parent;
        }
        heap_[index]=moving; moving->heapIndex=index;
        return index;
    }
    void down(std::size_t index) {
        auto* moving=heap_[index];
        while (index<heap_.size()/2) {
            auto child=index*2+1;
            if (child+1<heap_.size() && heap_[child+1]->priority<heap_[child]->priority) ++child;
            if (moving->priority<=heap_[child]->priority) break;
            heap_[index]=heap_[child]; heap_[index]->heapIndex=index;
            index=child;
        }
        heap_[index]=moving; moving->heapIndex=index;
    }
    Bucket& bucket(std::uint32_t priority) {
        const auto [entry,created]=buckets_.try_emplace(priority,Bucket{priority});
        auto& result=entry->second;
        if (created) {
            try { heap_.push_back(&result); }
            catch (...) { buckets_.erase(entry); throw; }
            up(heap_.size()-1);
        }
        return result;
    }
    void removeBucket(Bucket& bucket) {
        const auto index=bucket.heapIndex;
        auto* last=heap_.back(); heap_.pop_back();
        if (index<heap_.size()) { heap_[index]=last; down(up(index)); }
        buckets_.erase(bucket.priority);
    }
    void append(std::size_t index,Bucket& bucket) {
        auto& node=nodes_[index];
        node.owner=&bucket; node.previous=bucket.tail; node.next=none;
        if (bucket.tail!=none) nodes_[bucket.tail].next=index;
        else bucket.head=index;
        bucket.tail=index;
    }
    void unlink(std::size_t index) {
        auto& node=nodes_[index]; auto& bucket=*node.owner;
        if (node.previous!=none) nodes_[node.previous].next=node.next;
        else bucket.head=node.next;
        if (node.next!=none) nodes_[node.next].previous=node.previous;
        else bucket.tail=node.previous;
        node.owner=nullptr;
        if (bucket.head==none) removeBucket(bucket);
    }
    const Node& checked(std::size_t index) const {
        if (index>=nodes_.size() || !nodes_[index].owner)
            throw std::out_of_range("Unregistered scheduled entry");
        return nodes_[index];
    }
public:
    DeadlineQueue()=default;
    DeadlineQueue(const DeadlineQueue&)=delete;
    DeadlineQueue& operator=(const DeadlineQueue&)=delete;
    bool empty() const { return count_==0; }
    std::size_t size() const { return count_; }
    std::size_t deadlineCount() const { return buckets_.size(); }
    bool contains(std::size_t index,T value) const {
        return index<nodes_.size() && nodes_[index].owner && nodes_[index].value==value;
    }
    T at(std::size_t index) const { return checked(index).value; }
    std::uint32_t priority(std::size_t index) const { return checked(index).owner->priority; }
    T front() const {
        if (empty()) throw std::out_of_range("Empty update schedule");
        return nodes_[heap_.front()->head].value;
    }
    std::size_t insert(T value,std::uint32_t priority) {
        // Allocate before publishing either the entry or its deadline bucket.
        const auto index=free_;
        if (index==none) nodes_.push_back({});
        Bucket* target;
        try { target=&bucket(priority); }
        catch (...) { if (index==none) nodes_.pop_back(); throw; }
        const auto result=index==none?nodes_.size()-1:index;
        if (index!=none) free_=nodes_[index].next;
        nodes_[result].value=value; append(result,*target); ++count_;
        return result;
    }
    void reschedule(std::size_t index,T expected,std::uint32_t priority) {
        if (!contains(index,expected)) throw std::out_of_range("Scheduled owner mismatch");
        if (nodes_[index].owner->priority==priority) return;
        auto& destination=bucket(priority);
        unlink(index); append(index,destination);
    }
    bool erase(std::size_t index,T expected) {
        if (!contains(index,expected)) return false;
        unlink(index); nodes_[index].value={}; nodes_[index].next=free_;
        nodes_[index].previous=none; free_=index; --count_;
        return true;
    }
    // Enumeration is for ownership/reset; callbacks must not mutate the queue.
    template<class Visit> void forEach(Visit visit) const {
        for (std::size_t index=0;index<nodes_.size();++index)
            if (nodes_[index].owner) visit(index,nodes_[index].value);
    }
    void clear() {
        nodes_.clear(); heap_.clear(); buckets_.clear(); free_=none; count_=0;
    }
};
}
