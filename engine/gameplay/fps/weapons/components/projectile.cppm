export module engine.gameplay.fps.weapons.components.projectile;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAffineTransform3;

export namespace engine::gameplay {
enum class ProjectileAim : std::uint32_t {TowardTarget,MuzzleForward,ConstrainedTarget};
struct ProjectileLauncher {
    ecs::Entity muzzle;Engine::Math::Fixed speed{},range{},gravity{};
    Engine::Math::Fixed target_cone_cosine{};
    std::uint32_t model{},categories{},previous_shots{};ProjectileAim aim{ProjectileAim::TowardTarget};
};
struct LinearFlight {
    Engine::Math::FixedVector3 velocity;
    Engine::Math::Fixed gravity{},remaining{};
    std::uint32_t categories{},expired{};
    Engine::Math::FixedAffineTransform3 birth_pose;
    std::uint64_t birth_tick{};
};
}
export namespace ecs {
template<> struct ComponentTraits<engine::gameplay::ProjectileLauncher> {
    static constexpr std::string_view StableName="engine.gameplay.projectile_launcher";static constexpr std::uint32_t Version=2;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
template<> struct ComponentTraits<engine::gameplay::LinearFlight> {
    static constexpr std::string_view StableName="engine.gameplay.linear_flight";static constexpr std::uint32_t Version=2;static constexpr PersistencePolicy Persistence=PersistencePolicy::Serializable;
};
}
