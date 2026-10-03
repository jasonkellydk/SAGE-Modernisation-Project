export module engine.time.frame_accumulator;
import std;
export import engine.time.simulation_time;

export namespace engine::time {
// Wall time belongs to the host. This driver turns elapsed durations into
// fixed steps, independent of render frequency, without truncating 1/rate.
class FrameAccumulator {
public:
    explicit constexpr FrameAccumulator(FixedStep step) noexcept : m_step(step) {}
    constexpr void AddElapsed(Duration elapsed) {
        if(elapsed.count()<0) throw std::invalid_argument("Frame duration cannot be negative");
        constexpr std::uint64_t nanos=1'000'000'000;
        const auto value=static_cast<std::uint64_t>(elapsed.count());
        const auto seconds=value/nanos;
        const auto fraction=m_fraction+(value%nanos)*m_step.TicksPerSecond();
        const auto partial=fraction/nanos;
        const auto maximum=std::numeric_limits<std::uint64_t>::max();
        if(partial>maximum-m_pending || seconds>(maximum-m_pending-partial)/m_step.TicksPerSecond())
            throw std::overflow_error("Frame accumulator exhausted its tick range");
        m_pending+=seconds*m_step.TicksPerSecond()+partial;m_fraction=fraction%nanos;
    }
    constexpr bool StepDue() const noexcept {return m_pending!=0;}
    // Consume only after a successful simulation step. A host may bound the
    // work per render; unconsumed debt survives the next frame.
    constexpr void ConsumeStep() {
        if(!m_pending) throw std::logic_error("No simulation step is due");
        --m_pending;
    }
    constexpr std::uint64_t PendingSteps() const noexcept {return m_pending;}
    constexpr Engine::Math::Fixed Interpolation() const noexcept {return m_pending ? Engine::Math::Fixed::One() : Engine::Math::Fixed::FromRatio(m_fraction,1'000'000'000);}
private:
    FixedStep m_step;
    std::uint64_t m_pending{},m_fraction{};
};
}
