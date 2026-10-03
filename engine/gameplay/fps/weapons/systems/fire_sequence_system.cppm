export module engine.gameplay.fps.weapons.systems.fire_sequence_system;
import std;
export import engine.gameplay.fps.weapons.components.fire_sequence;
export import engine.ecs.system.system;
export import Engine.Core.Math.FixedVector;
export namespace engine::gameplay {
// Time debt carries through charge, cooldown and magazine reload instead of
// rounding each shot to whole ticks. Inventory, hit resolution, burst policy
// and presentation are composed separately by the game.
struct FireSequenceSystem {
    using Query=ecs::Query<ecs::Read<FireTrigger>,ecs::Write<FireSequence>>;
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using Engine::Math::Fixed;const auto triggers=chunk.Get<FireTrigger>();const auto states=chunk.Get<FireSequence>();
        for(std::size_t row=0;row<states.size();++row) {
            auto& state=states[row];state.fired=0;if(state.period<=Fixed{}) continue;
            const auto tick=context.Tick();const auto rate=context.Time().Step().TicksPerSecond();auto budget=Fixed::FromRatio(tick,rate)-Fixed::FromRatio(tick ? tick-1 : 0,rate);
            while(budget>Fixed{}) {
                if(state.phase==FirePhase::Ready) {
                    if(!triggers[row].pressed) break;
                    if(state.capacity && !state.rounds) {state.phase=FirePhase::Reload;state.remaining=state.reload;}
                    else {state.phase=FirePhase::Charge;state.remaining=state.charge;}
                }
                const auto consumed=std::min(budget,state.remaining);state.remaining-=consumed;budget-=consumed;if(state.remaining>Fixed{}) break;
                switch(state.phase) {
                case FirePhase::Charge:
                    if(triggers[row].pressed) {++state.shots;state.fired=1;if(state.capacity) --state.rounds;}
                    state.phase=FirePhase::Cooldown;state.remaining=state.period;break;
                case FirePhase::Cooldown:state.phase=FirePhase::Ready;break;
                case FirePhase::Reload:state.rounds=state.capacity;state.phase=FirePhase::Ready;break;
                default:break;
                }
            }
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<engine::gameplay::FireSequenceSystem> {
    static constexpr std::string_view StableName="engine.gameplay.fire_sequence";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
