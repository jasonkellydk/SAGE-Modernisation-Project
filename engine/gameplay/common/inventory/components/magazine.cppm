export module engine.gameplay.common.inventory.components.magazine;
import std;
export import engine.ecs.core.component_registry;
export namespace engine::gameplay {
// Quantities are separate SoA columns from inventory identity and selection.
// Negative quantities designate unlimited supply; ownership and grant rules
// are composed by the game rather than encoded in this storage mechanism.
struct Magazine {std::int32_t loaded{},reserve{},capacity{},maximum_reserve{};};
inline bool IsLoaded(const Magazine& magazine) noexcept {return magazine.loaded!=0;}
inline void Consume(Magazine& magazine,std::uint32_t rounds) noexcept {
    if(magazine.loaded>=0) magazine.loaded=static_cast<std::int32_t>(std::max(std::int64_t{},std::int64_t(magazine.loaded)-rounds));
}
inline void Reload(Magazine& magazine) noexcept {
    if(magazine.capacity<0) {magazine.loaded=-1;return;}
    const auto missing=std::max(0,magazine.capacity-magazine.loaded);
    const auto transfer=magazine.reserve<0 ? missing : std::min(missing,magazine.reserve);
    magazine.loaded+=transfer;if(magazine.reserve>=0) magazine.reserve-=transfer;
}
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::Magazine> {
    static constexpr std::string_view StableName="engine.gameplay.magazine";
    static constexpr std::uint32_t Version=1;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
