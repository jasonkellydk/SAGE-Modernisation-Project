export module games.generalszh.scripting.match_vocabulary;
import std;

export import engine.scripting.runtime.script_runtime;
import games.generalszh.scripting.script_parameters;
import games.generalszh.scripting.core_vocabulary;

// The script conditions about the local player's match (the original's ScriptConditions over TheVictoryConditions,
// the local player and the audio): whether the local player's alliance won or lost, whether the local player alone
// lost, a skirmish player's side, and whether a music track has played through. They answer for the machine they run
// on, so they belong to the local player's own scripts (MultiplayerScripts.scb), never to the lockstep simulation's.
export namespace generalszh::scripting
{
class LocalMatchHost
{
public:
	virtual ~LocalMatchHost() = default;

	virtual bool AlliedVictory() const = 0; // VictoryConditions::isLocalAlliedVictory
	virtual bool AlliedDefeat() const = 0;  // isLocalAlliedDefeat
	virtual bool LocalDefeat() const = 0;   // isLocalDefeat
	// A player's side (Player::getSide), by name or "<Local Player>"; empty if there is no such player.
	virtual std::string SideOf(const std::string &player) const = 0;
	// AudioManager::hasMusicTrackCompleted: the track has played through at least `times` times.
	virtual bool MusicCompleted(const std::string &track, std::int64_t times) const = 0;
	// A player's (by name or "<Local Player>") objects of a type or an object type list (evaluatePlayerUnitCondition's
	// count: `ignoreDead` leaves the dying out); none for no such player.
	virtual std::int64_t CountObjects(const std::string &player, const std::string &types, bool ignoreDead) const = 0;
	// PLAYER_LOST_OBJECT_TYPE's memory: the count last seen for (player, types), kept on this machine.
	virtual std::int64_t RecordedCount(const std::string &player, const std::string &types) const = 0;
	virtual void RecordCount(const std::string &player, const std::string &types, std::int64_t count) = 0;
};

inline void AddMatchVocabulary(engine::scripting::Vocabulary &vocabulary, LocalMatchHost *host)
{
	using engine::scripting::ScriptCallContext;
	using parameters::Integer;
	using parameters::Text;
	vocabulary.AddCondition("MULTIPLAYER_ALLIED_VICTORY", [host](ScriptCallContext &) { return host->AlliedVictory(); });
	vocabulary.AddCondition("MULTIPLAYER_ALLIED_DEFEAT", [host](ScriptCallContext &) { return host->AlliedDefeat(); });
	// evaluateMultiplayerPlayerDefeat: the local player lost while their alliance goes on.
	vocabulary.AddCondition("MULTIPLAYER_PLAYER_DEFEAT", [host](ScriptCallContext &) { return host->LocalDefeat() && !host->AlliedDefeat(); });
	// SKIRMISH_PLAYER_FACTION(player, side): evaluateSkirmishPlayerIsFaction.
	vocabulary.AddCondition("SKIRMISH_PLAYER_FACTION", [host](ScriptCallContext &c) {
		const std::string side = host->SideOf(Text(c, 0));
		return !side.empty() && side == Text(c, 1);
	});
	vocabulary.AddCondition("MUSIC_TRACK_HAS_COMPLETED", [host](ScriptCallContext &c) { return host->MusicCompleted(Text(c, 0), Integer(c, 1)); });
	// The music scripts' questions about the local player's army (the lockstep's player vocabulary answers them the same
	// way): PLAYER_HAS_OBJECT_COMPARISON(player, comparison, count, type), counting the dead too; PLAYER_LOST_OBJECT_TYPE
	// (player, type): fewer living than last seen, the count seen kept.
	vocabulary.AddCondition("PLAYER_HAS_OBJECT_COMPARISON", [host](ScriptCallContext &c) {
		return detail::Compare(host->CountObjects(Text(c, 0), Text(c, 3), false), Integer(c, 1), Integer(c, 2));
	});
	vocabulary.AddCondition("PLAYER_LOST_OBJECT_TYPE", [host](ScriptCallContext &c) {
		const std::int64_t now = host->CountObjects(Text(c, 0), Text(c, 1), true);
		const std::int64_t seen = host->RecordedCount(Text(c, 0), Text(c, 1));
		if (now != seen)
			host->RecordCount(Text(c, 0), Text(c, 1), now);
		return now < seen;
	});
}
}
