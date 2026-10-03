export module games.renegade.gameplay.weapons.components.weapon_state;
import std;
export import engine.gameplay.common.inventory.components.magazine;
export import engine.gameplay.common.inventory.components.inventory;
export import Engine.Core.Math.FixedVector;
export namespace renegade {
enum class WeaponPhase:std::uint32_t {Idle,Ready,Charge,FirePrimary,FireSecondary,Reload,StartSwitch,EndSwitch};
struct WeaponState {
    WeaponPhase phase{};std::uint32_t reserved{};Engine::Math::Fixed remaining{},burst_timer{},empty_timer{};
    std::int32_t burst_count{};std::uint32_t shots{},fired{},active{},reload_started{},empty_clicks{};
};
struct WeaponControl {std::uint32_t active{},primary{},secondary{},reload{},permitted{1},muzzle_clear{1},upright{1},safety{};};
struct WeaponInput {
    Engine::Math::FixedVector3 origin,target;
    std::uint32_t primary{},secondary{},reload{},permitted{1};
};
// Combat/weapons.cpp Add_Rounds. Inventory policy is game-local; transferring
// and consuming a loaded magazine use shared mechanisms.
inline void AddWeaponRounds(engine::gameplay::Magazine& magazine,std::int32_t amount) {
    std::int64_t rounds=amount;
    if(!magazine.loaded) {
        if(rounds<0) magazine.loaded=magazine.capacity;
        else if(magazine.reserve>=0) {const auto count=std::min(rounds,std::int64_t(magazine.capacity));rounds-=count;magazine.loaded=static_cast<std::int32_t>(count);}
    }
    if(rounds<0) magazine.reserve=-1;
    else if(magazine.reserve>=0) magazine.reserve=static_cast<std::int32_t>(std::min(std::int64_t(magazine.maximum_reserve),std::int64_t(magazine.reserve)+rounds));
}
}
export namespace ecs {
template<> struct ComponentTraits<renegade::WeaponState> {static constexpr std::string_view StableName="renegade.weapon_state";static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;};
template<> struct ComponentTraits<renegade::WeaponControl> {static constexpr std::string_view StableName="renegade.weapon_control";static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;};
template<> struct ComponentTraits<renegade::WeaponInput> {static constexpr std::string_view StableName="renegade.weapon_input";static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;};
}
