export module games.renegade.gameplay.weapons.systems.weapon_state_system;
import std;
export import games.renegade.gameplay.weapons.components.weapon_state;
export import games.renegade.content.weapons.weapon_catalog;
export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
import Engine.Core.Math.FixedRandom;
export namespace renegade {
struct WeaponDefinitions {content::WeaponCatalog catalog;std::uint64_t seed{};};
struct WeaponShot {ecs::Entity weapon,owner;std::uint32_t ammunition{},sequence{};std::uint64_t tick{};};
using WeaponShots=ecs::ChunkOutputs<WeaponShot>;
}
export namespace ecs {
template<> struct ResourceTraits<renegade::WeaponDefinitions> {static constexpr std::string_view StableName="renegade.weapon_definitions";};
template<> struct ResourceTraits<renegade::WeaponShots> {static constexpr std::string_view StableName="renegade.weapon_shots";};
}
export namespace renegade {
struct WeaponStateSystem {
    using Query=ecs::Query<ecs::Read<engine::gameplay::InventoryItem>,ecs::Read<WeaponControl>,ecs::Write<WeaponState>,ecs::Write<engine::gameplay::Magazine>>;
    using Resources=ecs::Resources<ecs::Read<WeaponDefinitions>,ecs::Write<WeaponShots>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {context.Write<WeaponShots>().Reset(query.PreparedChunkCount());}
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using namespace Engine::Math;using namespace engine::gameplay;
        const auto items=chunk.Get<InventoryItem>();const auto controls=chunk.Get<WeaponControl>();const auto states=chunk.Get<WeaponState>();const auto magazines=chunk.Get<Magazine>();
        const auto entities=chunk.Entities();const auto& definitions=context.Read<WeaponDefinitions>();auto& output=context.Write<WeaponShots>().Slot(context);
        const auto tick=context.Tick();const auto rate=context.Time().Step().TicksPerSecond();
        const auto elapsed=Fixed::FromRatio(tick,rate)-Fixed::FromRatio(tick ? tick-1 : 0,rate);
        for(std::size_t row=0;row<items.size();++row) {
            auto& state=states[row];auto& magazine=magazines[row];const auto control=controls[row];state.fired=state.reload_started=0;
            const auto* weapon=definitions.catalog.Find(items[row].definition);if(!weapon || !weapon->primary) continue;
            const auto& primary=definitions.catalog.ammunition.at(weapon->primary);
            const auto set=[&](WeaponPhase phase) {
                state.phase=phase;
                switch(phase) {
                case WeaponPhase::Ready:state.remaining=Fixed::FromInt(25);break;
                // Set_State uses the original constant SWITCH_TIME=1,
                // despite retaining SwitchTime in the content schema.
                case WeaponPhase::StartSwitch:case WeaponPhase::EndSwitch:state.remaining=Fixed::FromRatio(1,2);break;
                case WeaponPhase::Reload:state.remaining=weapon->reload_time;state.reload_started=1;break;
                case WeaponPhase::Charge:state.remaining=primary.charge;break;
                case WeaponPhase::FirePrimary:state.remaining=Fixed::One()/primary.rate;break;
                case WeaponPhase::FireSecondary:state.remaining=Fixed::One()/definitions.catalog.ammunition.at(weapon->secondary).rate;break;
                default:state.remaining={};break;
                }
            };
            if(control.active && !state.active) set(WeaponPhase::StartSwitch);
            state.active=control.active;if(!control.active || !items[row].available) continue;
            const bool trigger=control.primary || control.secondary && weapon->secondary;
            if(primary.rate<=Fixed{} || weapon->reload_time<Fixed{} || primary.charge<Fixed{}) continue;
            if(primary.burst_max<=0) state.burst_count=-1;
            else {
                state.burst_timer-=elapsed;
                if(!trigger || state.burst_timer<=Fixed{}) {
                    auto random=Stream(definitions.seed,{entities[row].index,entities[row].generation,tick});
                    state.burst_timer=primary.burst_delay*UniformFixed(random,Fixed::FromRatio(1,2),Fixed::One());state.burst_count=primary.burst_max;
                }
            }
            if(control.reload && magazine.reserve!=0 && state.phase<=WeaponPhase::Ready) set(WeaponPhase::Reload);
            auto budget=elapsed;
            while(budget>Fixed{}) {
                if(state.phase<=WeaponPhase::Ready) {
                    if(trigger && state.burst_count!=0 && control.permitted && (weapon->style!=6 || control.upright) && !control.safety) {
                        if(IsLoaded(magazine)) set(WeaponPhase::Charge);
                        else if(weapon->empty_sound) {state.empty_timer-=elapsed;if(state.empty_timer<=Fixed{}) {state.empty_timer=Fixed::FromRatio(3,10);++state.empty_clicks;}}
                    }
                    if(!IsLoaded(magazine) && magazine.reserve!=0) set(WeaponPhase::Reload);
                }
                const auto use=std::min(state.remaining,budget);state.remaining-=use;budget-=use;if(state.remaining>Fixed{}) break;
                switch(state.phase) {
                case WeaponPhase::Idle:budget={};break;
                case WeaponPhase::Ready:set(WeaponPhase::Idle);break;
                case WeaponPhase::Charge: {
                    const bool first=control.primary || !control.secondary || !weapon->secondary;
                    const auto& ammo=first ? primary : definitions.catalog.ammunition.at(weapon->secondary);
                    if(ammo.rate<=Fixed{}) {set(WeaponPhase::Ready);budget={};break;}
                    set(first ? WeaponPhase::FirePrimary : WeaponPhase::FireSecondary);
                    // C4/beacon deployment is a separate source behavior; it
                    // must not masquerade as an ordinary bullet launch.
                    if(trigger && control.muzzle_clear && weapon->style!=0 && weapon->style!=6) {
                        Consume(magazine,ammo.bullet_cost);--state.burst_count;++state.shots;state.fired=1;
                        output.push_back({entities[row],items[row].container,ammo.id,state.shots,tick});
                    }
                    break;
                }
                case WeaponPhase::FirePrimary:case WeaponPhase::FireSecondary:set(WeaponPhase::Ready);break;
                case WeaponPhase::Reload:engine::gameplay::Reload(magazine);set(WeaponPhase::Ready);break;
                case WeaponPhase::StartSwitch:set(WeaponPhase::EndSwitch);break;
                case WeaponPhase::EndSwitch:set(WeaponPhase::Idle);break;
                }
            }
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<renegade::WeaponStateSystem> {
    static constexpr std::string_view StableName="renegade.weapon_state";static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::Simulation;
    using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
}
