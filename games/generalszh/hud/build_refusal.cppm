export module games.generalszh.hud.build_refusal;
import std;

export import games.generalszh.session.session_view;

// ControlBar::processCommandUI's check as a build-type button is pressed (UNIT_BUILD, DOZER_CONSTRUCT,
// SPECIAL_POWER_CONSTRUCT and SPECIAL_POWER_CONSTRUCT_FROM_SHORTCUT: BuildAssistant::canMakeUnit for the builder and the
// button's Object): no money, a full queue, full parking or the player's most of its kind refuse the press with a
// message (InGameUI::message), no money also with EVA's InsufficientFunds; any other answer lets it go on.
export namespace generalszh::hud
{
// The message's label for a refusal; empty: the press goes on.
inline std::string_view RefusalMessage(gameplay::BuildRefusal refusal) noexcept
{
	switch (refusal)
	{
	case gameplay::BuildRefusal::NoMoney: return "GUI:NotEnoughMoneyToBuild";
	case gameplay::BuildRefusal::QueueFull: return "GUI:ProductionQueueFull";
	case gameplay::BuildRefusal::ParkingFull: return "GUI:ParkingPlacesFull";
	case gameplay::BuildRefusal::MaxedOut: return "GUI:UnitMaxedOut";
	case gameplay::BuildRefusal::None: break;
	}
	return {};
}

// What a press asks and what a refusal tells (the view models' hooks; the host wires them to the session's view, the
// in-game messages and EVA).
struct BuildRefusalHooks
{
	std::function<gameplay::BuildRefusal(ecs::Entity, const content::CommandButtonContent &)> canMake; // canMakeUnit(builder, Object)
	std::function<void(std::string_view)> message;                                                    // InGameUI::message(label)
	std::function<void(std::string_view)> eva;                                                        // Eva::setShouldPlay(event)

	// Whether the press is refused (told as the original tells it).
	bool Refuses(ecs::Entity builder, const content::CommandButtonContent &button) const
	{
		if (!canMake)
			return false;
		const gameplay::BuildRefusal refusal = canMake(builder, button);
		const std::string_view label = RefusalMessage(refusal);
		if (label.empty())
			return false;
		if (refusal == gameplay::BuildRefusal::NoMoney && eva)
			eva("InsufficientFunds");
		if (message)
			message(label);
		return true;
	}
};
}
