module;
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

export module engine.navigation.movement.snapshot.occupants;

export namespace navigation {
// Answers are relative to one mover at capture time, not universal unit traits.
struct OccupantState {
    std::uint32_t id=0;
    bool allied=false, infantry=false, movable=false, crushable=false;
    unsigned char crushableLevel=0;
    bool dozerPassage=false;
};

class OccupantSnapshot {
    std::vector<OccupantState> table_;
    static std::uint32_t hash(std::uint32_t id) {
        id^=id>>16;id*=0x7feb352du;id^=id>>15;id*=0x846ca68bu;return id^(id>>16);
    }
public:
    OccupantSnapshot()=default;
    explicit OccupantSnapshot(std::span<const OccupantState> units) {
        if (units.empty()) return;
        if (units.size()>table_.max_size()/2) throw std::length_error("Too many occupant states");
        std::size_t capacity=1;
        while (capacity<units.size()*2) {
            if (capacity>table_.max_size()/2) throw std::length_error("Occupant table overflow");
            capacity*=2;
        }
        table_.resize(capacity);
        for (const auto& unit:units) {
            if (!unit.id) throw std::invalid_argument("Invalid occupant identity");
            auto slot=std::size_t(hash(unit.id))&(capacity-1);
            while (table_[slot].id) {
                if (table_[slot].id==unit.id) throw std::invalid_argument("Duplicate occupant identity");
                slot=(slot+1)&(capacity-1);
            }
            table_[slot]=unit;
        }
    }
    const OccupantState* find(std::uint32_t id) const {
        if (!id || table_.empty()) return nullptr;
        auto slot=std::size_t(hash(id))&(table_.size()-1);
        while (table_[slot].id) {
            if (table_[slot].id==id) return &table_[slot];
            slot=(slot+1)&(table_.size()-1);
        }
        return nullptr;
    }
    bool allied(const OccupantState* unit) const { return unit->allied; }
    bool permitsDozerPassage(const OccupantState* unit) const { return unit->dozerPassage; }
    bool infantry(const OccupantState* unit) const { return unit->infantry; }
    bool canMoveAside(const OccupantState* unit) const { return unit->movable; }
    bool canCrush(const OccupantState* unit) const { return unit->crushable; }
    unsigned crushableLevel(const OccupantState* unit) const { return unit->crushableLevel; }
};
}
