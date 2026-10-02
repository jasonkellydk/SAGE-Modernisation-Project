export module engine.gameplay.rts.upgrades.resources.object_upgrade_grants;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// Objects given an upgrade of their own this tick by a system (a finished
// OBJECT upgrade research, a promotion: Object::giveUpgrade), for the upgrade
// pass to take in the same tick; it empties the list once it has.
export namespace engine::gameplay
{
struct ObjectUpgradeGrant
{
	ecs::Entity entity;
	std::uint32_t upgrade{0};
};

struct ObjectUpgradeGrants
{
	std::vector<ObjectUpgradeGrant> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::ObjectUpgradeGrants>
{
	static constexpr std::string_view StableName = "engine.gameplay.object_upgrade_grants";
};
}
