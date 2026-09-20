module;
#include <cstdint>

export module engine.navigation.movement.relationship_query;

export namespace navigation {
// Owned by one synchronous movement query. Never reuse after world updates:
// diplomacy can change even when both team identities stay the same.
class RelationshipQueryCache {
    std::uintptr_t source_=0, target_=0;
    bool valid_=false, allied_=false;
public:
    template<class Resolve>
    bool allied(std::uintptr_t source, std::uintptr_t target,
        bool sourceUndetectedDefector, bool targetUndetectedDefector, Resolve resolve) {
        if (!source || sourceUndetectedDefector) return false;
        if (targetUndetectedDefector) return true;
        if (!valid_ || source_!=source || target_!=target) {
            allied_=resolve();
            source_=source;
            target_=target;
            valid_=true;
        }
        return allied_;
    }
};
}
