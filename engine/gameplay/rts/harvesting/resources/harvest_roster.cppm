export module engine.gameplay.rts.harvesting.resources.harvest_roster;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// This tick's stores, depots and homes as harvesters see them (the
// original's per-player ResourceGatheringManager lists and
// Player::findClosestByKindOf), gathered before any harvester looks: where
// each is, whose, how big, what it holds and how it takes dockers.
export namespace engine::gameplay
{
struct HarvestSite
{
	ecs::Entity entity;
	Engine::Math::FixedVector3 position;
	Engine::Math::Fixed radius; // bounding circle
	std::uint32_t player{0};
	std::uint32_t boxes{0};      // a store's
	std::uint32_t startingBoxes{0};
	std::uint8_t homeRank{0xFF}; // HarvestCatalog::NoHome: not a home
	std::uint8_t approaches{0};  // a dock's approach points (not dynamic)
	bool store{false};
	bool depot{false};
	bool dynamic{false};
	bool open{true};
	bool deleteWhenEmpty{false};
	bool listed{false}; // a store harvesters looking for one find
};

struct HarvestRoster : ecs::ChunkOutputs<HarvestSite>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::HarvestRoster>
{
	static constexpr std::string_view StableName = "engine.gameplay.harvest_roster";
};
}
