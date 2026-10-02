export module games.generalszh.presentation.hud.resources.money_feedback;
import std;

import engine.ecs.system.system;

// What a player hears of its money (MiscAudio MoneyDepositSound and MoneyWithdrawSound, played by Money::deposit and
// Money::withdraw for the money's player). Presentation configuration: not saved.
export namespace generalszh::presentation
{
struct MoneyFeedback
{
	std::string depositSound;
	std::string withdrawSound;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::MoneyFeedback>
{
	static constexpr std::string_view StableName = "generalszh.presentation.money_feedback";
};
}
