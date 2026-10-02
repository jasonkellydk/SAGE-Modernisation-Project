export module games.generalszh.presentation.hud.systems.money_sound_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.hud.resources.money_feedback;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import engine.gameplay.rts.economy.resources.player_money;
export import engine.gameplay.common.identity.components.definition_ref;

// Money::deposit / Money::withdraw (EA's: the MiscAudio event with the money's player index, added unless the call
// said not to): each of the last tick's sounding transactions, in order, plays its sound for its player, heard without
// a position (its "ui player" type: only that player hears it, as AudioManager::shouldPlayLocally has it).
export namespace generalszh::presentation
{
struct MoneySoundSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::PlayerMoney>, ecs::Read<MoneyFeedback>, ecs::Write<SoundRequests>>;

	void Execute(ecs::SystemContext &context) const
	{
		const MoneyFeedback &feedback = context.Read<MoneyFeedback>();
		auto &sounds = context.Write<SoundRequests>().pending;
		for (const engine::gameplay::MoneyTransaction &transaction : context.Read<engine::gameplay::PlayerMoney>().Transactions())
		{
			const std::string &sound = transaction.kind == engine::gameplay::MoneyTransaction::Kind::Deposit ? feedback.depositSound : feedback.withdrawSound;
			if (sound.empty())
				continue;
			SoundRequest request;
			request.sound = sound;
			request.owner = transaction.player;
			request.positioned = false;
			sounds.push_back(std::move(request));
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::MoneySoundSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.money_sounds";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
