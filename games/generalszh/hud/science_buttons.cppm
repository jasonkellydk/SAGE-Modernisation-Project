export module games.generalszh.hud.science_buttons;
import std;

export import games.generalszh.session.session_view;
import engine.gameplay.rts.sciences.resources.player_sciences;
import games.generalszh.gameplay.orders.resources.command_bar_overrides;

// The general's powers on command buttons (ControlBar::populateCommand's NEED_SPECIAL_POWER_SCIENCE rule, shared by the
// command bar and the powers shortcut bar): a power needing a science shows only once the player has that science, in
// the art of the best science of its Science list the player has (CommandButton::copyImagesFrom the purchase button).
export namespace generalszh::hud
{
// The player's PlayerTemplate (none: not known).
inline const content::PlayerTemplateInfo *Faction(session::SessionView &view, std::uint32_t player)
{
	const std::string name = view.PlayerTemplateName(player);
	for (const content::PlayerTemplateInfo &info : view.Content().playerTemplates.templates)
		if (info.name == name)
			return &info;
	return nullptr;
}

// The purchase-science button (rank 1, 3 or 8 of the General's Powers screen) whose first science is `science`, for
// its art (CommandButton::copyImagesFrom); none: the button keeps its own.
inline const content::CommandButtonContent *PurchaseButtonFor(session::SessionView &view, const content::PlayerTemplateInfo &faction,
	std::string_view science)
{
	static constexpr std::array<std::size_t, 3> buttons{4, 15, 4}; // MAX_PURCHASE_SCIENCE_RANK_1 / 3 / 8
	const content::CommandCatalog &commands = view.Content().commands;
	std::array<std::optional<content::CommandSetContent>, 3> sets;
	for (std::size_t rank = 0; rank < 3; ++rank)
	{
		if (faction.purchaseScienceCommandSets[rank].empty())
			return nullptr;
		sets[rank] = generalszh::gameplay::EffectiveCommandSet(commands, view.World().FindResource<generalszh::gameplay::CommandBarOverrides>(),
			faction.purchaseScienceCommandSets[rank]);
		if (!sets[rank])
			return nullptr;
	}
	for (std::size_t rank = 0; rank < 3; ++rank)
		for (std::size_t slot = 0; slot < buttons[rank]; ++slot)
		{
			const content::CommandButtonContent *each = commands.Button(sets[rank]->buttons[slot]);
			if (each != nullptr && each->command == content::ButtonCommand::PurchaseScience && !each->sciences.empty() && each->sciences.front() == science)
				return each;
		}
	return nullptr;
}

// populateCommand: `button`'s art for `player` (none: hidden). A NEED_SPECIAL_POWER_SCIENCE button (not a purchase,
// player upgrade or object upgrade) whose power needs a science the player lacks is hidden, since it can never be had
// another way; having it, its Science list is walked in order while the player has each, and the purchase button of
// the last one had lends its art. Any other button keeps its own.
inline std::optional<std::string> ScienceButtonImage(session::SessionView &view, std::uint32_t player, const content::CommandButtonContent &button)
{
	namespace gp = engine::gameplay;
	if ((button.options & content::button_option::NeedScience) == 0 || button.command == content::ButtonCommand::PurchaseScience ||
		button.command == content::ButtonCommand::PlayerUpgrade || button.command == content::ButtonCommand::ObjectUpgrade)
		return button.buttonImage;
	const content::GameContent &content = view.Content();
	const auto index = content.powers.Template(button.specialPower);
	if (!index || content.powers.templates[*index].requiredScience.empty())
		return button.buttonImage;
	const auto *sciences = view.World().FindResource<gp::PlayerSciences>();
	const auto has = [&](std::string_view name) {
		const auto bit = content.Science(name);
		return bit && sciences != nullptr && sciences->Has(player, *bit);
	};
	if (!has(content.powers.templates[*index].requiredScience))
		return std::nullopt;
	std::optional<std::size_t> best;
	for (std::size_t at = 0; at < button.sciences.size() && has(button.sciences[at]); ++at)
		best = at;
	if (best)
		if (const content::PlayerTemplateInfo *faction = Faction(view, player))
			if (const content::CommandButtonContent *art = PurchaseButtonFor(view, *faction, button.sciences[*best]))
				return art->buttonImage;
	return button.buttonImage;
}
}
