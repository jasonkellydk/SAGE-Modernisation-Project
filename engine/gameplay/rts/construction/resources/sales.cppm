export module engine.gameplay.rts.construction.resources.sales;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// How selling goes (the original's BuildAssistant): the scaffold stands
// `scaffoldTicks` before the structure starts coming down, then its
// construction falls by `perTick` a tick; at `donePercent` it is gone. And
// the structures whose sale finished this tick, for the game to pay for and
// remove; and those built this tick (by whom), for the game to finish; and
// the structures a builder at work found whole this tick (its repair done).
export namespace engine::gameplay
{
struct SaleSettings
{
	std::uint64_t scaffoldTicks{45};                                           // FRAMES_TO_ALLOW_SCAFFOLD: 1.5 s
	Engine::Math::Fixed perTick{Engine::Math::Fixed::FromRatio(100, 90)};      // 100% over TOTAL_FRAMES_TO_SELL_OBJECT: 3 s
	Engine::Math::Fixed donePercent{Engine::Math::Fixed::FromInt(-50)};
};

struct SalesDone
{
	std::vector<ecs::Entity> entities;
};

struct ConstructionDone
{
	ecs::Entity structure;
	ecs::Entity builder;
	bool rebuild{false}; // finished by a rebuild (onStructureConstructionComplete's isRebuild)
};

// DozerActionDoActionState, DOZER_TASK_REPAIR: the builder at its dock finds its structure whole (repair complete).
struct RepairDone
{
	ecs::Entity structure;
	ecs::Entity builder;
};

struct ConstructionsDone
{
	std::vector<ConstructionDone> list;
	std::vector<RepairDone> repairs;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::SaleSettings>
{
	static constexpr std::string_view StableName = "engine.gameplay.sale_settings";
};

template<>
struct ResourceTraits<engine::gameplay::ConstructionsDone>
{
	static constexpr std::string_view StableName = "engine.gameplay.constructions_done";
};

template<>
struct ResourceTraits<engine::gameplay::SalesDone>
{
	static constexpr std::string_view StableName = "engine.gameplay.sales_done";
};
}
