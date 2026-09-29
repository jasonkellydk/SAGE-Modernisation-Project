export module games.generalszh.gameplay.world.resources.solo_play;
import std;

export import engine.ecs.core.component_registry;
export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// A single-player game's difficulty rules (DifficultyBonus: the objects that have it): whether this is one
// (GameLogic::isInSinglePlayerGame: GAME_SINGLE_PLAYER, a campaign or challenge mission), the game's difficulty
// (ScriptEngine::getGlobalDifficulty: a human player's), and whether objects get the difficulty bonus
// (ScriptEngine::m_objectsShouldReceiveDifficultyBonus, OBJECT_ALLOW_BONUSES).
// Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct SoloPlay
{
	bool singlePlayer{false};
	std::uint8_t difficulty{1}; // 0 easy, 1 normal, 2 hard
	bool bonuses{true};
	// A Generals' Challenge mission (Campaign IsChallengeCampaign), and the local human's player in a single-player
	// game (ThePlayerList->getLocalPlayer: what "<Local Player>", and in a challenge "ThePlayer", name).
	bool challenge{false};
	std::uint32_t localPlayer{0};

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.Flag(singlePlayer);
		writer.U32(difficulty);
		writer.Flag(bonuses);
		writer.Flag(challenge);
		writer.U32(localPlayer);
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto single = reader.Flag();
		const auto level = reader.U32();
		const auto allowed = reader.Flag();
		const auto challenged = reader.Flag();
		const auto local = reader.U32();
		if (!single || !level || !allowed || !challenged || !local || *level > 2)
			return false;
		singlePlayer = *single;
		difficulty = static_cast<std::uint8_t>(*level);
		bonuses = *allowed;
		challenge = *challenged;
		localPlayer = *local;
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::SoloPlay>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.solo_play";
};

}
