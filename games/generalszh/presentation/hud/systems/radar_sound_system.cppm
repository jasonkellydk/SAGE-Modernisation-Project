export module games.generalszh.presentation.hud.systems.radar_sound_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.hud.resources.radar_sounds;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import engine.gameplay.rts.radar.resources.player_radar;
export import engine.gameplay.rts.match.resources.match_outcome;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.common.identity.components.definition_ref;

// Player::addRadar / removeRadar / enableRadar / disableRadar (Player.cpp): the tick a player's radar comes on
// (hadRadar false, hasRadar true) it hears RadarNotifyOnlineSound, the tick it goes (an object giving it gone or
// disabled, its power short) RadarNotifyOfflineSound; each for that player only (setPlayerIndex), without a position.
// okToPlayRadarEdgeSound: not once the player is defeated (hasSinglePlayerBeenDefeated) or dead (killPlayer), and not
// on the logic's frame 0 or outside its update (what the map made: the presentation's first look only takes note).
export namespace generalszh::presentation
{
struct RadarSoundSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::PlayerRadar>, ecs::Read<engine::gameplay::MatchOutcome>,
		ecs::Read<engine::gameplay::TeamRoster>, ecs::Read<RadarSounds>, ecs::Write<RadarHeard>, ecs::Write<SoundRequests>>;

	void Execute(ecs::SystemContext &context) const
	{
		const engine::gameplay::PlayerRadar &radar = context.Read<engine::gameplay::PlayerRadar>();
		RadarHeard &heard = context.Write<RadarHeard>();
		if (!heard.seen || context.Tick() == 0)
		{
			heard.had = radar.has;
			heard.seen = true;
			return;
		}
		const engine::gameplay::MatchOutcome &outcome = context.Read<engine::gameplay::MatchOutcome>();
		const engine::gameplay::TeamRoster &roster = context.Read<engine::gameplay::TeamRoster>();
		const RadarSounds &sounds = context.Read<RadarSounds>();
		auto &requests = context.Write<SoundRequests>().pending;
		const std::size_t players = (std::max)(heard.had.size(), radar.has.size());
		for (std::uint32_t player = 0; player < players; ++player)
		{
			const bool had = player < heard.had.size() && heard.had[player] != 0;
			const bool has = radar.Has(player);
			if (had == has)
				continue;
			if (outcome.Eliminated(player) || (player < roster.PlayerCount() && roster.PlayerAt(player).dead))
				continue;
			const std::string &sound = has ? sounds.online : sounds.offline;
			if (sound.empty())
				continue;
			SoundRequest request;
			request.sound = sound;
			request.owner = player;
			request.positioned = false;
			requests.push_back(std::move(request));
		}
		heard.had = radar.has;
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::RadarSoundSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.radar_sounds";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
