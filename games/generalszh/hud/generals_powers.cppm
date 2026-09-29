export module games.generalszh.hud.generals_powers;
import std;

export import engine.gui.mvvm.observable;
export import games.generalszh.session.session_view;
export import games.generalszh.commands.game_commands;
import engine.gameplay.rts.sciences.resources.player_ranks;
import engine.gameplay.rts.sciences.resources.player_sciences;
import engine.gameplay.rts.sciences.definitions.rank_rules;
import games.generalszh.gameplay.orders.resources.command_bar_overrides;

// The General's Powers screen (the original's ControlBar::populatePurchaseScience / updateContextPurchaseScience /
// show/hide/togglePurchaseScience, GUI_COMMAND_PURCHASE_SCIENCE and the general's button star, getStarImage):
// read each frame from the session's view, headless. Views bind to the view model; the host feeds it the frame's state
// and the frame's real time.
export namespace generalszh::hud
{
// ButtonRank1Number0..3, ButtonRank3Number0..14, ButtonRank8Number0..3 (MAX_PURCHASE_SCIENCE_RANK_1 / 3 / 8).
inline constexpr std::array<std::size_t, 3> ScienceButtons{4, 15, 4};

struct ScienceSlot
{
	bool shown{false};
	bool enabled{false};
	bool owned{false};   // WIN_STATUS_ALWAYS_COLOR: a science the player has draws in colour
	std::string image;   // the button's ButtonImage
	std::string purchase; // what a click asks to buy (empty: nothing)
};

struct GeneralsPowersState
{
	bool hasPlayer{false};
	std::int32_t purchasePoints{0};
	std::int32_t level{1};
	int progress{0}; // percent into the rank
	std::array<std::array<ScienceSlot, 15>, 3> ranks{};
};

namespace generals_powers_detail
{
namespace gp = engine::gameplay;

struct Player
{
	std::uint32_t index;
	const gp::PlayerRank &rank;
	const gp::PlayerSciences &known;
	const gp::RankRules &rules;
	const content::GameContent &content;

	std::optional<std::uint32_t> Bit(std::string_view science) const { return content.Science(science); }
	bool Has(std::uint32_t science) const { return known.Has(index, science); }
	// ScienceStore::playerHasPrereqsForScience / playerHasRootPrereqsForScience (an unknown science: false).
	bool HasAll(std::optional<std::uint32_t> science, bool roots) const
	{
		if (!science || *science >= rules.sciences.size())
			return false;
		const gp::ScienceRule &rule = rules.sciences[*science];
		for (const std::uint32_t needed : roots ? rule.roots : rule.prerequisites)
			if (!Has(needed))
				return false;
		return true;
	}
	// getSciencePurchaseCost <= getSciencePurchasePoints (an unknown science costs 0).
	bool Affordable(std::optional<std::uint32_t> science) const
	{
		const std::int32_t cost = science && *science < rules.sciences.size() ? rules.sciences[*science].purchaseCost : 0;
		return cost <= rank.purchasePoints;
	}
	bool Buyable(std::optional<std::uint32_t> science) const { return science && !Has(*science) && HasAll(science, false) && Affordable(science); }
};

// One button of a rank's command set: shown unless script-only, disabled unless its first science may be bought, in
// colour when had; a script's disabled science stays shown but off, a hidden one hides, as does one whose roots the
// player lacks. `checkEmpty`: the rank 1 row checks nothing for a button naming no science.
inline ScienceSlot Slot(const Player &player, const content::CommandButtonContent *button, bool checkEmpty)
{
	ScienceSlot slot;
	if (button == nullptr || (button->options & content::button_option::ScriptOnly) != 0)
		return slot;
	slot.shown = true;
	slot.image = button->buttonImage;
	// GUI_COMMAND_PURCHASE_SCIENCE: the first it may buy, else (the loop run out) the last.
	for (const std::string &name : button->sciences)
	{
		slot.purchase = name;
		if (player.Buyable(player.Bit(name)))
			break;
	}
	if (checkEmpty && button->sciences.empty())
		return slot;
	const std::optional<std::uint32_t> science = button->sciences.empty() ? std::nullopt : player.Bit(button->sciences.front());
	if (science && player.known.Disabled(player.index, *science))
		return slot;
	if (science && player.known.Hidden(player.index, *science))
	{
		slot.shown = false;
		return slot;
	}
	slot.enabled = player.Buyable(science);
	slot.owned = science && player.Has(*science);
	if (!player.HasAll(science, true))
		slot.shown = false;
	return slot;
}
}

// The screen for `player` (none: an observer): nothing unless its template names all three rank command sets and
// they exist.
inline GeneralsPowersState ReadGeneralsPowers(session::SessionView &view, std::optional<std::uint32_t> player)
{
	using namespace generals_powers_detail;
	GeneralsPowersState state;
	if (!player)
		return state;
	auto &world = view.World();
	const auto *ranks = world.FindResource<gp::PlayerRanks>();
	const auto *known = world.FindResource<gp::PlayerSciences>();
	const auto *rules = world.FindResource<gp::RankRules>();
	const gp::PlayerRank *rank = ranks != nullptr ? ranks->Find(*player) : nullptr;
	if (known == nullptr || rules == nullptr || rank == nullptr)
		return state;
	state.hasPlayer = true;
	state.purchasePoints = rank->purchasePoints;
	state.level = rank->level;
	// ((skill points - level down) * 100) / (level up - level down), in Int.
	const std::int64_t span = static_cast<std::int64_t>(rank->levelUp) - rank->levelDown;
	state.progress = span != 0 ? static_cast<int>(static_cast<std::int32_t>((static_cast<std::int64_t>(rank->skillPoints) - rank->levelDown) * 100) / span) : 0;
	const content::GameContent &content = view.Content();
	const content::PlayerTemplateInfo *faction = nullptr;
	const std::string name = view.PlayerTemplateName(*player);
	for (const content::PlayerTemplateInfo &info : content.playerTemplates.templates)
		if (info.name == name)
			faction = &info;
	if (faction == nullptr)
		return state;
	std::array<std::optional<content::CommandSetContent>, 3> sets{};
	for (std::size_t row = 0; row < 3; ++row)
	{
		if (faction->purchaseScienceCommandSets[row].empty())
			return state;
		sets[row] = generalszh::gameplay::EffectiveCommandSet(content.commands, world.FindResource<generalszh::gameplay::CommandBarOverrides>(),
			faction->purchaseScienceCommandSets[row]);
	}
	if (std::ranges::any_of(sets, [](const auto &set) { return !set.has_value(); }))
		return state;
	const Player who{*player, *rank, *known, *rules, content};
	for (std::size_t row = 0; row < 3; ++row)
		for (std::size_t slot = 0; slot < ScienceButtons[row]; ++slot)
			state.ranks[row][slot] = Slot(who, content.commands.Button(sets[row]->buttons[slot]), row == 0);
	return state;
}

class GeneralsPowersViewModel
{
public:
	// `submit` sends an order onto the command bus; `formatTitle` writes the rank's title (SCIENCE:Rank<n>); the
	// general's button draws `starOff` normally and `starOn` in the lit half of its blink (the scheme's
	// GeneralButtonEnable / GeneralButtonHightlited).
	GeneralsPowersViewModel(std::function<void(commands::GameCommand)> submit, std::function<std::u16string(std::int32_t)> formatTitle,
		std::string starOff = {}, std::string starOn = {})
		: m_submit(std::move(submit)), m_formatTitle(std::move(formatTitle)), m_starOff(std::move(starOff)), m_starOn(std::move(starOn))
	{
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t slot = 0; slot < ScienceButtons[row]; ++slot)
				clicked[row][slot].SetAction([this, row, slot] { Buy(row, slot); });
		general.SetAction([this] { Toggle(); });
		exit.SetAction([this] { Hide(); });
		generalImage.Set(m_starOff);
	}

	GeneralsPowersViewModel(const GeneralsPowersViewModel &) = delete;
	GeneralsPowersViewModel &operator=(const GeneralsPowersViewModel &) = delete;

	// The screen (GenExpParent) shows.
	engine::gui::mvvm::Observable<bool> shown{false};
	engine::gui::mvvm::Observable<std::u16string> points;
	engine::gui::mvvm::Observable<std::u16string> title;
	engine::gui::mvvm::Observable<int> progress{0};
	std::array<std::array<engine::gui::mvvm::Observable<bool>, 15>, 3> slotShown;
	std::array<std::array<engine::gui::mvvm::Observable<bool>, 15>, 3> slotEnabled;
	std::array<std::array<engine::gui::mvvm::Observable<bool>, 15>, 3> slotOwned;
	std::array<std::array<engine::gui::mvvm::Observable<std::string>, 15>, 3> slotImage;
	std::array<std::array<engine::gui::mvvm::Command, 15>, 3> clicked;
	// The general's button (ControlBar.wnd:ButtonGeneral): enabled for a player, its art blinking while points wait.
	engine::gui::mvvm::Observable<bool> generalEnabled{false};
	engine::gui::mvvm::Observable<std::string> generalImage;
	engine::gui::mvvm::Command general;
	engine::gui::mvvm::Command exit; // ButtonExit (and Esc)

	// The frame's state and the real microseconds since the last (the star's blink runs on wall-clock time).
	void Apply(GeneralsPowersState state, std::uint64_t microseconds)
	{
		// onPlayerRankChanged / onPlayerSciencePurchasePointsChanged: the star flashes again.
		if (m_seen && (state.level != m_state.level || state.purchasePoints != m_state.purchasePoints))
			m_flash = true;
		m_seen = state.hasPlayer;
		m_state = std::move(state);
		generalEnabled.Set(m_state.hasPlayer);
		if (!m_state.hasPlayer)
			shown.Set(false);
		Populate();
		Star(microseconds);
	}

	void Show()
	{
		if (!m_state.hasPlayer)
			return;
		Populate();
		m_flash = false;
		shown.Set(true);
	}
	void Hide() { shown.Set(false); }
	void Toggle() { shown.Get() ? Hide() : Show(); }

private:
	void Populate()
	{
		points.Set(Number(m_state.purchasePoints));
		title.Set(m_formatTitle ? m_formatTitle(m_state.level) : std::u16string{});
		progress.Set(m_state.progress);
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t slot = 0; slot < ScienceButtons[row]; ++slot)
			{
				const ScienceSlot &each = m_state.ranks[row][slot];
				slotShown[row][slot].Set(each.shown);
				slotEnabled[row][slot].Set(each.enabled);
				slotOwned[row][slot].Set(each.owned);
				slotImage[row][slot].Set(each.image);
				clicked[row][slot].enabled.Set(each.shown && each.enabled);
			}
	}

	// ControlBar::getStarImage: no flash once the points fall below those last flashed for, or run out; else lit for the
	// second half of each second.
	void Star(std::uint64_t microseconds)
	{
		if (m_lastFlashed > m_state.purchasePoints || m_state.purchasePoints <= 0)
			m_flash = false;
		else
			m_lastFlashed = m_state.purchasePoints;
		if (!m_flash || !m_state.hasPlayer)
		{
			generalImage.Set(m_starOff);
			return;
		}
		m_blink = (m_blink + microseconds) % 1'000'000u;
		generalImage.Set(m_blink >= 500'000u ? m_starOn : m_starOff);
	}

	// GUI_COMMAND_PURCHASE_SCIENCE: MSG_PURCHASE_SCIENCE for the science the button would buy.
	void Buy(std::size_t row, std::size_t slot)
	{
		const std::string &science = m_state.ranks[row][slot].purchase;
		if (!science.empty())
			m_submit(commands::PurchaseScience{science});
	}

	static std::u16string Number(std::int64_t value)
	{
		const std::string digits = std::to_string(value);
		return std::u16string(digits.begin(), digits.end());
	}

	std::function<void(commands::GameCommand)> m_submit;
	std::function<std::u16string(std::int32_t)> m_formatTitle;
	std::string m_starOff, m_starOn;
	GeneralsPowersState m_state;
	bool m_seen{false};
	bool m_flash{true};             // m_genStarFlash starts on
	std::int32_t m_lastFlashed{-1}; // m_lastFlashedAtPointValue
	std::uint64_t m_blink{0}; // microseconds into the one-second cycle
};
}
