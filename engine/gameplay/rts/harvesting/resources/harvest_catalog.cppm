export module engine.gameplay.rts.harvesting.resources.harvest_catalog;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// The harvest's rules, from the game's data:
//   what a delivered box is worth (the original's GlobalData
//   ValuePerSupplyBox);
//   per definition (DefinitionRef::index), how much a harvester with nowhere
//   to go prefers it as home (lower first; NoHome: never), as the original's
//   regrouping: a cash generator, else a command centre, else any structure;
//   how much further a computer player's harvesters look for a store.
// HarvestEvents: this tick's deliveries (the credits, where) and stores
// emptied under a harvester with no other store near, for the game's
// floating text and speech.
export namespace engine::gameplay
{
struct HarvestCatalog
{
	static constexpr std::uint8_t NoHome = 0xFF;

	std::int64_t valuePerBox{100};
	std::vector<std::uint8_t> homeRank; // by definition
	std::uint32_t computerScanMultiplier{2};
	std::vector<std::uint8_t> computer; // by player: a computer player (PLAYER_COMPUTER)

	std::uint8_t HomeRank(std::uint32_t definition) const noexcept { return definition < homeRank.size() ? homeRank[definition] : NoHome; }
	bool Computer(std::uint32_t player) const noexcept { return player < computer.size() && computer[player] != 0; }
};

struct HarvestEvent
{
	enum class Kind : std::uint8_t
	{
		Delivered, // `amount` credits to the depot's player, at `position` (the harvester)
		Depleted,  // the store it took the last box from, with no other store near
	};
	ecs::Entity harvester;
	Kind kind{Kind::Delivered};
	std::int64_t amount{0};
	std::uint32_t player{0};
	Engine::Math::FixedVector3 position;
};

struct HarvestEvents : ecs::ChunkOutputs<HarvestEvent>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::HarvestCatalog>
{
	static constexpr std::string_view StableName = "engine.gameplay.harvest_catalog";
};
template<>
struct ResourceTraits<engine::gameplay::HarvestEvents>
{
	static constexpr std::string_view StableName = "engine.gameplay.harvest_events";
};
}
