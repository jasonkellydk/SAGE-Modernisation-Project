export module games.generalszh.presentation.hud.systems.in_game_message_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.hud.algorithms.message_list;
export import games.generalszh.presentation.audio.resources.audio_resources;
export import engine.gameplay.rts.match.resources.match_outcome;
export import engine.gameplay.common.identity.components.owner;

// Once a tick: each player the match saw fall (VictoryConditions::update past the first frame) gets the message
// GUI:PlayerHasBeenDefeated with their name, and the GUIMessageReceived sound ("People are boneheads. Also play a
// sound"); then the messages fade (InGameUI::update).
export namespace generalszh::presentation
{
struct InGameMessageSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::MatchOutcome>, ecs::Write<InGameMessages>, ecs::Write<AudioCommands>>;

	void Execute(ecs::SystemContext &context) const
	{
		InGameMessages &messages = context.Write<InGameMessages>();
		const std::uint64_t tick = context.Tick();
		for (const std::uint32_t player : context.Read<engine::gameplay::MatchOutcome>().fallen)
		{
			const std::u16string name = player < messages.playerNames.size() ? messages.playerNames[player] : std::u16string{};
			AddMessage(messages, FormatWithName(messages.defeatedText, name), tick);
			context.Write<AudioCommands>().pending.push_back({AudioCommand::Kind::Interface, "GUIMessageReceived"});
		}
		StepMessages(messages, tick);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::InGameMessageSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.in_game_messages";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
