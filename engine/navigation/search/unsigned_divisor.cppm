export module engine.navigation.search.unsigned_divisor;
import std;

export namespace navigation {
// Exact division for a divisor reused across many grid-index decodes.
class UnsignedDivisor {
    std::uint32_t divisor_;
    std::uint64_t reciprocal_;
public:
    explicit UnsignedDivisor(std::uint32_t divisor) : divisor_(divisor) {
        if (!divisor) throw std::invalid_argument("Zero navigation index divisor");
        reciprocal_=((std::uint64_t{1}<<32)+divisor-1)/divisor;
    }
    std::uint32_t quotient(std::uint32_t numerator) const {
        // ceil(2^32/d) overestimates the reciprocal by less than one. For a
        // 32-bit numerator, the quotient estimate is exact or one too high.
        // Both products fit uint64_t, including d=1 and std::numeric_limits<std::uint32_t>::max() inputs.
        const auto estimate=static_cast<std::uint32_t>((numerator*reciprocal_)>>32);
        return estimate-static_cast<std::uint32_t>(std::uint64_t(estimate)*divisor_>numerator);
    }
};
}
