export module games.generalszh.hud.build_tooltip;
import std;

export import games.generalszh.session.session_view;

// The control bar's build tooltip (Core/GameEngine/Source/GameClient/GUI/GUICallbacks/ControlBarPopupDescription.cpp):
// ControlBar::showBuildTooltipLayout's wait and ControlBar::update's hiding as a small state, and
// populateBuildTooltipLayout's name, cost and description for a command button (or the power, money and general's
// experience windows) from the game's facts (gameplay::BuildTooltipFacts). Headless: the host lays the popup out.
export namespace generalszh::hud
{
using TooltipLabels = std::function<std::u16string(std::string_view)>;

// UnicodeString::format with %d / %i / %u (numbers) and %s / %ls / %hs (texts), in order; %% a percent sign.
using FormatArgument = std::variant<std::int64_t, std::u16string>;

inline std::u16string FormatText(std::u16string_view pattern, std::initializer_list<FormatArgument> arguments)
{
	std::u16string out;
	auto next = arguments.begin();
	for (std::size_t at = 0; at < pattern.size(); ++at)
	{
		if (pattern[at] != u'%' || at + 1 >= pattern.size())
		{
			out.push_back(pattern[at]);
			continue;
		}
		std::size_t spec = at + 1;
		if (pattern[spec] == u'%')
		{
			out.push_back(u'%');
			at = spec;
			continue;
		}
		if (pattern[spec] == u'l' || pattern[spec] == u'h')
			++spec;
		if (spec >= pattern.size())
		{
			out.push_back(pattern[at]);
			continue;
		}
		const char16_t kind = pattern[spec];
		if ((kind == u'd' || kind == u'i' || kind == u'u' || kind == u's') && next != arguments.end())
		{
			if (const auto *number = std::get_if<std::int64_t>(&*next))
				for (const char digit : std::to_string(*number))
					out.push_back(static_cast<char16_t>(digit));
			else
				out += std::get<std::u16string>(*next);
			++next;
			at = spec;
			continue;
		}
		out.push_back(pattern[at]);
	}
	return out;
}

// ControlBar::showBuildTooltipLayout and ControlBar::update's runUpdate (ControlBarPopupDescriptionUpdateFunc, with no
// window animation: useAnimation is never set): the window last asked for (prevWindow), when its wait began
// (beginWaitTime), whether it has been shown since (isInitialized), whether the popup shows, and whether this frame's
// mouse asked to keep it (m_showBuildToolTipLayout).
struct BuildTooltipState
{
	std::string window;
	std::uint32_t beginWait{0};
	bool initialized{false};
	bool shown{false};
	bool keep{false};
};

enum class BuildTooltipStep : std::uint8_t
{
	Nothing,
	Hide,     // m_buildToolTipLayout->hide(TRUE): another window asked while it showed
	Populate, // populateBuildTooltipLayout, then shown
};

// showBuildTooltipLayout(cmdButton), called each frame the window is the mouse's tooltip window: the same window again
// keeps the popup and, once (beginWaitTime + its TOOLTIPDELAY, in unsigned milliseconds) has passed and it has not been
// shown since, fills and shows it; another while it shows hides it (prevWindow cleared); else the wait begins.
// `disabled`: tooltips off (InGameUI::areTooltipsDisabled) or the game ending; `blocked`: a button's popup is not
// shown in a replay, under the quit menu or the disconnect screen (after the wait, once).
inline BuildTooltipStep ShowBuildTooltip(BuildTooltipState &state, std::string_view window, int delay, std::uint32_t now, bool disabled, bool blocked)
{
	if (disabled)
		return BuildTooltipStep::Nothing;
	bool passed = false;
	if (state.window == window)
	{
		state.keep = true;
		if (!state.initialized && state.beginWait + static_cast<std::uint32_t>(delay) < now)
			passed = true;
		if (!passed)
			return BuildTooltipStep::Nothing;
	}
	else if (state.shown)
	{
		state.shown = false;
		state.window.clear();
		return BuildTooltipStep::Hide;
	}
	if (!passed)
	{
		state.window = std::string(window);
		state.beginWait = now;
		state.initialized = false;
		return BuildTooltipStep::Nothing;
	}
	state.initialized = true;
	if (blocked)
		return BuildTooltipStep::Nothing;
	state.keep = true;
	state.shown = true;
	return BuildTooltipStep::Populate;
}

// ControlBar::update: while the popup shows, ControlBarPopupDescriptionUpdateFunc hides it (deleteBuildTooltipLayout:
// prevWindow cleared) unless this frame's mouse kept it; then the keep is cleared. True when it was hidden now.
inline bool UpdateBuildTooltip(BuildTooltipState &state, bool gameEnding = false)
{
	if (!state.shown)
		return false;
	bool hidden = false;
	if (gameEnding || !state.keep)
	{
		state.shown = false;
		state.window.clear();
		hidden = true;
	}
	state.keep = false;
	return hidden;
}

struct BuildTooltipText
{
	std::u16string name;
	std::u16string cost;
	std::u16string description;
	std::int64_t costToBuild{0}; // the cost line shows only above 0
};

namespace build_tooltip_detail
{
// ProductionPrerequisite::getRequiresList for one prerequisite: each unfulfilled unit group's last unit's name, a group
// of alternatives "<previous> or <unit>" (only the one before: CONTROLBAR:OrRequirement), names after the first on new
// lines; a science line not had: CONTROLBAR:GeneralsPromotion (the original's newline for it goes to an unused string).
inline std::u16string RequiresList(const gameplay::PrerequisiteState &prerequisite, const content::GameContent &content, const TooltipLabels &labels)
{
	std::u16string list;
	bool first = true;
	if (!prerequisite.science)
	{
		std::vector<int> owned = prerequisite.owned;
		std::vector<bool> orWith(owned.size(), false);
		for (std::size_t index = 1; index < owned.size(); ++index)
		{
			orWith[index] = true; // every unit after the first is "or" with the one before
			owned[index] += owned[index - 1];
			owned[index - 1] = -1;
		}
		const auto displayName = [&](const std::string &unit) {
			const content::ObjectDefinition *object = content.objects.Find(unit);
			return object != nullptr && !object->displayName.empty() ? labels(object->displayName) : std::u16string{};
		};
		for (std::size_t index = 0; index < owned.size(); ++index)
		{
			if (owned[index] != 0)
				continue;
			if (orWith[index])
				list += displayName(prerequisite.units[index - 1]) + u" " + labels("CONTROLBAR:OrRequirement") + u" ";
			std::u16string name = displayName(prerequisite.units[index]);
			if (first)
				first = false;
			else
				name += u"\n";
			list += name;
		}
	}
	else if (!prerequisite.hasScience)
		list += labels("CONTROLBAR:GeneralsPromotion");
	return list;
}

// The thing's prerequisites' lists joined with ", " (only between non-empty ones), as CONTROLBAR:Requirements says them,
// on a new line after any description.
inline void AppendRequirements(std::u16string &description, const gameplay::BuildTooltipFacts &facts, const content::GameContent &content,
	const TooltipLabels &labels)
{
	std::u16string needed;
	bool first = true;
	for (const gameplay::PrerequisiteState &prerequisite : facts.prerequisites)
	{
		const std::u16string list = RequiresList(prerequisite, content, labels);
		if (!list.empty())
		{
			if (first)
				first = false;
			else
				needed += u", ";
		}
		needed += list;
	}
	if (needed.empty())
		return;
	needed = FormatText(labels("CONTROLBAR:Requirements"), {needed});
	if (!description.empty())
		description += u"\n";
	description += needed;
}
}

// ControlBar::populateBuildTooltipLayout(commandButton) for the local player and the first selected object.
inline BuildTooltipText PopulateBuildTooltip(const content::CommandButtonContent &button, const gameplay::BuildTooltipFacts &facts,
	const content::GameContent &content, const TooltipLabels &labels)
{
	using namespace build_tooltip_detail;
	using content::ButtonCommand;
	BuildTooltipText text;
	const content::ObjectDefinition *thing = button.object.empty() ? nullptr : content.objects.Find(button.object);
	const bool hasUpgrade = !button.upgrade.empty() && content.upgrades.Find(button.upgrade).has_value();
	const bool purchase = button.command == ButtonCommand::PurchaseScience;
	// The science it shows: a purchase button's first one the player lacks; a button that fires a science its last had
	// (fireScienceButton: no science text then).
	std::optional<std::size_t> science;
	bool fireScience = false;
	if (button.command != ButtonCommand::PlayerUpgrade && button.command != ButtonCommand::ObjectUpgrade)
	{
		const std::size_t count = button.sciences.size();
		const auto had = [&](std::size_t index) { return index < facts.hasButtonScience.size() && facts.hasButtonScience[index]; };
		if (count > 1)
		{
			for (std::size_t index = 0; index < count; ++index)
			{
				science = index;
				if (!purchase)
				{
					if (!had(index) && index > 0)
						science = index - 1;
					fireScience = true;
					break;
				}
				if (!had(index))
					break;
			}
		}
		else if (count == 1)
		{
			science = 0;
			fireScience = !purchase;
		}
	}
	if (!button.descriptLabel.empty())
	{
		text.description = labels(button.descriptLabel);
		if (facts.selected)
		{
			if (button.command == ButtonCommand::ToggleOvercharge)
			{
				if (facts.overcharge)
					text.description += u"\n" + labels(*facts.overcharge ? "TOOLTIP:TooltipNukeReactorOverChargeIsOn" : "TOOLTIP:TooltipNukeReactorOverChargeIsOff");
			}
			else if (thing != nullptr)
			{
				switch (facts.refusal)
				{
				case gameplay::BuildRefusal::NoMoney: text.description += u"\n\n" + labels("TOOLTIP:TooltipNotEnoughMoneyToBuild"); break;
				case gameplay::BuildRefusal::QueueFull: text.description += u"\n\n" + labels("TOOLTIP:TooltipCannotPurchaseBecauseQueueFull"); break;
				case gameplay::BuildRefusal::ParkingFull: text.description += u"\n\n" + labels("TOOLTIP:TooltipCannotBuildUnitBecauseParkingFull"); break;
				case gameplay::BuildRefusal::MaxedOut:
					text.description += u"\n\n" +
						labels(thing->Is("STRUCTURE") ? "TOOLTIP:TooltipCannotBuildBuildingBecauseMaximumNumber" : "TOOLTIP:TooltipCannotBuildUnitBecauseMaximumNumber");
					break;
				case gameplay::BuildRefusal::None: break;
				}
			}
			else if (hasUpgrade && !facts.upgradeInProduction &&
				(button.command == ButtonCommand::PlayerUpgrade || button.command == ButtonCommand::ObjectUpgrade))
			{
				if (facts.queueAtMax)
					text.description += u"\n\n" + labels("TOOLTIP:TooltipCannotPurchaseBecauseQueueFull");
				else if (!facts.canAffordUpgrade)
					text.description += u"\n\n" + labels("TOOLTIP:TooltipNotEnoughMoneyToBuild");
			}
		}
	}
	text.name = labels(button.textLabel);
	if (thing != nullptr && !purchase)
	{
		text.costToBuild = facts.thingCost;
		if (text.costToBuild > 0)
			text.cost = FormatText(labels("TOOLTIP:Cost"), {text.costToBuild});
		AppendRequirements(text.description, facts, content, labels);
	}
	else if (hasUpgrade)
	{
		const bool playerButton = button.command == ButtonCommand::PlayerUpgrade, objectButton = button.command == ButtonCommand::ObjectUpgrade;
		bool already = facts.upgradeComplete;
		bool conflicting = false;
		if (!already && facts.selected)
		{
			already = facts.objectHasUpgrade;
			if (objectButton)
				conflicting = !facts.objectAffected;
		}
		if (conflicting && !already)
			text.description = labels(!button.conflictingLabel.empty() ? std::string_view(button.conflictingLabel) : std::string_view("TOOLTIP:HasConflictingUpgradeDefault"));
		else if (already && (playerButton || objectButton))
			text.description = labels(!button.purchasedLabel.empty() ? std::string_view(button.purchasedLabel) : std::string_view("TOOLTIP:AlreadyUpgradedDefault"));
		else if (!already)
		{
			const bool missingScience = std::any_of(facts.hasButtonScience.begin(), facts.hasButtonScience.end(), [](bool had) { return !had; });
			text.costToBuild = facts.upgradeCost;
			if (text.costToBuild > 0)
				text.cost = FormatText(labels("TOOLTIP:Cost"), {text.costToBuild});
			if (missingScience)
			{
				if (!text.description.empty())
					text.description += u"\n";
				text.description += FormatText(labels("CONTROLBAR:Requirements"), {labels("CONTROLBAR:GeneralsPromotion")});
			}
		}
	}
	else if (science && !fireScience)
	{
		// ScienceStore::getNameAndDescription and getSciencePurchaseCost (TOOLTIP:ScienceCost).
		const auto bit = content.Science(button.sciences[*science]);
		const content::ScienceInfo *info = bit && *bit < content.scienceInfo.size() ? &content.scienceInfo[*bit] : nullptr;
		if (info != nullptr)
		{
			text.name = info->displayName.empty() ? std::u16string{} : labels(info->displayName);
			text.description = info->description.empty() ? std::u16string{} : labels(info->description);
			text.costToBuild = info->purchaseCost;
		}
		else
			text.costToBuild = 0;
		if (text.costToBuild > 0)
			text.cost = FormatText(labels("TOOLTIP:ScienceCost"), {text.costToBuild});
		if (thing != nullptr)
			AppendRequirements(text.description, facts, content, labels);
	}
	return text;
}

// populateBuildTooltipLayout(nullptr, window) for the bar's windows without a command: the money (CONTROLBAR:Money,
// CONTROLBAR:MoneyDescription), the power (CONTROLBAR:Power, CONTROLBAR:PowerDescription with the viewed player's
// production and consumption, 0 and 0 without one) and the general's experience; any other: none.
inline std::optional<BuildTooltipText> PopulateWindowTooltip(std::string_view window, std::optional<std::pair<std::int64_t, std::int64_t>> energy,
	const TooltipLabels &labels)
{
	BuildTooltipText text;
	if (window == "ControlBar.wnd:MoneyDisplay")
	{
		text.name = labels("CONTROLBAR:Money");
		text.description = labels("CONTROLBAR:MoneyDescription");
	}
	else if (window == "ControlBar.wnd:PowerWindow")
	{
		text.name = labels("CONTROLBAR:Power");
		const auto [made, used] = energy.value_or(std::pair<std::int64_t, std::int64_t>{0, 0});
		text.description = FormatText(labels("CONTROLBAR:PowerDescription"), {made, used});
	}
	else if (window == "ControlBar.wnd:GeneralsExp")
	{
		text.name = labels("CONTROLBAR:GeneralsExp");
		text.description = labels("CONTROLBAR:GeneralsExpDescription");
	}
	else
		return std::nullopt;
	return text;
}
}
