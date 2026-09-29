export module games.generalszh.hud.match_record;
import std;

export import games.generalszh.session.session_view;
export import games.generalszh.shell.score.battle_honors;
import games.generalszh.gameplay.score.resources.score_keepers;
import engine.gameplay.rts.match.resources.match_outcome;

// What the score screen reads of a finished game for the local player's SkirmishStats.ini (ScoreScreen.cpp
// populatePlayerInfo and updateSkirmishBattleHonors): won or lost with its alliance, still playing or not, its side,
// the game's length, and from its ScoreKeeper whether it built each kind of superweapon (getTotalObjectsBuilt of the
// original's named templates: every entry equivalent to one, reskins either way, counts, even one built then unbuilt)
// and how many vehicles (not aircraft) and aircraft it built (getTotalUnitsBuilt). The seats are the host's to add.
export namespace generalszh::hud
{
namespace match_record_detail
{
// ThingTemplate::isEquivalentTo: the same, or one reskinned from the other, or both from the same.
inline bool Equivalent(const content::ObjectDefinition &a, const content::ObjectDefinition *b, std::string_view name)
{
	if (a.name == name)
		return true;
	if (b == nullptr)
		return false;
	return a.reskinnedFrom == b->name || b->reskinnedFrom == a.name || (!a.reskinnedFrom.empty() && a.reskinnedFrom == b->reskinnedFrom);
}
}

inline shell::SkirmishGameRecord ReadGameRecord(session::SessionView &view, std::uint32_t player)
{
	using namespace match_record_detail;
	shell::SkirmishGameRecord record;
	auto &world = view.World();
	if (const auto *outcome = world.FindResource<engine::gameplay::MatchOutcome>())
	{
		record.won = outcome->HasWon(player);
		record.lost = outcome->HasLost(player);
		record.active = !outcome->Eliminated(player);
	}
	record.side = view.PlayerSide(player);
	record.ticks = view.CurrentTick();
	const auto *keepers = world.FindResource<generalszh::gameplay::ScoreKeepers>();
	const generalszh::gameplay::ScoreKeeper *keeper = keepers != nullptr ? keepers->Find(player) : nullptr;
	if (keeper == nullptr)
		return record;
	const auto &objects = view.Content().objects;
	const auto builtAny = [&](std::initializer_list<std::string_view> names) {
		for (const std::string_view name : names)
		{
			const content::ObjectDefinition *wanted = objects.Find(name);
			for (const auto &[definition, count] : keeper->objectsBuilt)
				if (definition < view.DefinitionCount() && Equivalent(view.Definition(definition), wanted, name))
					return true;
		}
		return false;
	};
	record.builtScud = builtAny({"GLAScudStorm", "Boss_GLAScudStorm", "Chem_GLAScudStorm", "Slth_GLAScudStorm", "Demo_GLAScudStorm"});
	record.builtParticleCannon = builtAny({"AmericaParticleCannonUplink", "AirF_AmericaParticleCannonUplink", "Lazr_AmericaParticleCannonUplink",
		"SupW_AmericaParticleCannonUplink", "Boss_ParticleCannonUplink"});
	record.builtNuke = builtAny({"ChinaNuclearMissileLauncher", "Boss_NuclearMissileLauncher", "Infa_ChinaNuclearMissileLauncher",
		"Nuke_ChinaNuclearMissileLauncher", "Tank_ChinaNuclearMissileLauncher"});
	for (const auto &[definition, count] : keeper->objectsBuilt)
	{
		if (definition >= view.DefinitionCount())
			continue;
		const content::ObjectDefinition &kind = view.Definition(definition);
		if (kind.Is("AIRCRAFT"))
			record.aircraftBuilt += count;
		else if (kind.Is("VEHICLE"))
			record.vehiclesBuilt += count;
	}
	return record;
}
}
