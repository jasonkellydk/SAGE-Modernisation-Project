module;
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

export module engine.navigation.spatial.contact_pairs;
export namespace navigation {
// Membership only: clients retain their own processing order. Keys are opaque
// identities and are never dereferenced, including during removal callbacks.
class ContactPairs {
    struct Pair { std::uintptr_t a=0,b=0; bool operator==(const Pair&) const = default; };
    static constexpr unsigned char Empty=0,Deleted=1;
    std::vector<Pair> entries_;
    std::vector<unsigned char> controls_;
    std::size_t count_=0,deleted_=0;
    static Pair canonical(std::uintptr_t a,std::uintptr_t b) { return a<b?Pair{a,b}:Pair{b,a}; }
    static std::uint64_t hash(Pair key) noexcept {
        // Partition data is pointer-aligned. Keep the useful address bits and
        // mix the two identities with shifts/rotations; this table is only a
        // duplicate filter, so it must not spend eight 64-bit multiplies per
        // candidate on a full splitmix avalanche.
        const auto a=static_cast<std::uint64_t>(key.a)>>4;
        const auto b=static_cast<std::uint64_t>(key.b)>>4;
        auto value=a^std::rotl(b,23)^std::rotr(a,17);
        value^=value>>29;
        value^=value<<17;
        value^=value>>31;
        return value;
    }
    static unsigned char fingerprint(std::uint64_t hash) { return static_cast<unsigned char>((hash>>57)|0x80u); }
    std::size_t locate(Pair key) const {
        const auto hashed=hash(key);
        const auto tag=fingerprint(hashed);
        auto slot=static_cast<std::size_t>(hashed)&(entries_.size()-1);
        while (controls_[slot]!=Empty && !(controls_[slot]==tag && entries_[slot]==key))
            slot=(slot+1)&(entries_.size()-1);
        return slot;
    }
    std::size_t insertionSlot(Pair key,std::uint64_t hashed) const {
        const auto tag=fingerprint(hashed);
        auto slot=static_cast<std::size_t>(hashed)&(entries_.size()-1);
        auto vacancy=entries_.size();
        for (;;) {
            const auto control=controls_[slot];
            if (control==Empty) return vacancy==entries_.size()?slot:vacancy;
            if (control==tag && entries_[slot]==key) return slot;
            // Search beyond a deleted slot: the key may already occur later
            // in this probe chain. Retain its first vacancy for a new key.
            if (control==Deleted && vacancy==entries_.size()) vacancy=slot;
            slot=(slot+1)&(entries_.size()-1);
        }
    }
    void place(Pair key) {
        const auto hashed=hash(key);
        auto slot=static_cast<std::size_t>(hashed)&(entries_.size()-1);
        while (controls_[slot]>=0x80u) slot=(slot+1)&(entries_.size()-1);
        if (controls_[slot]==Deleted) --deleted_;
        entries_[slot]=key;controls_[slot]=fingerprint(hashed);++count_;
    }
    void rehash(std::size_t size) {
        auto previous=std::move(entries_);
        auto previousControls=std::move(controls_);
        entries_.assign(size,{});controls_.assign(size,Empty);count_=deleted_=0;
        for (std::size_t i=0;i<previous.size();++i) if (previousControls[i]>=0x80u) place(previous[i]);
    }
public:
    bool insert(std::uintptr_t a,std::uintptr_t b) {
        const auto key=canonical(a,b);
        if (entries_.empty()) rehash(128);
        const auto hashed=hash(key);
        auto slot=insertionSlot(key,hashed);
        if (controls_[slot]>=0x80u) return false;
        // Repeated collision candidates favor short probes over dense storage.
        if ((count_+deleted_+1)*2>=entries_.size()) {
            rehash(count_*4>=entries_.size()?entries_.size()*2:entries_.size());
            slot=insertionSlot(key,hashed);
        }
        if (controls_[slot]==Deleted) --deleted_;
        entries_[slot]=key;controls_[slot]=fingerprint(hashed);++count_;
        return true;
    }
    bool erase(std::uintptr_t a,std::uintptr_t b) {
        if (entries_.empty()) return false;
        const auto slot=locate(canonical(a,b));
        if (controls_[slot]<0x80u) return false;
        controls_[slot]=Deleted;--count_;++deleted_;return true;
    }
    void clear() {
        if (!count_ && !deleted_) return;
        std::fill(controls_.begin(),controls_.end(),Empty);
        count_=deleted_=0;
    }
    std::size_t size() const { return count_; }
    std::size_t capacity() const { return entries_.size(); }
};
}
