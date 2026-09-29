export module engine.gameplay.rts.upgrades.resources.upgrade_reactions;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
import engine.ecs.system.system;

// The tick's upgrade triggers that went (UpgradeMux::giveSelfUpgrade), for
// the game's systems to carry out: which object, which of its triggers and
// that trigger's reaction. Rebuilt every tick.
export namespace engine::gameplay
{
struct UpgradeReaction
{
	ecs::Entity entity;
	std::uint32_t trigger{0};
	std::uint32_t reaction{0};
};

struct UpgradeReactions : ecs::ChunkOutputs<UpgradeReaction>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::UpgradeReactions>
{
	static constexpr std::string_view StableName = "engine.gameplay.upgrade_reactions";
};
}
