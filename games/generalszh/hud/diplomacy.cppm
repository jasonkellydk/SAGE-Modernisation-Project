export module games.generalszh.hud.diplomacy;
import std;

export import engine.gui.mvvm.observable;

// The in-game diplomacy window (the original's Diplomacy.cpp over Diplomacy.wnd), toggled by the DIPLOMACY meta-event and
// the control bar's communicator button: in a multiplayer game (InGameParent) a row per occupied game slot, its player's
// name, side and team in its colour, its status (alive, dead, an observer, gone) and, for another human still in the
// game, its mute / unmute button; outside one (SoloParent) the briefing history (each military subtitle's and popup
// message's label, once). It slides down from the top as it shows (WIN_ANIMATION_SLIDE_TOP) and back up as it hides.
// Headless: the host gives it the slots and the labels' texts and places the window.
export namespace generalszh::hud
{
inline constexpr std::size_t DiplomacyRows = 8; // MAX_SLOTS

// A game slot as PopulateInGameDiplomacyPopup reads it.
struct DiplomacySlot
{
	bool occupied{false};
	bool human{false};
	bool ai{false};
	bool local{false};     // TheGameInfo->getLocalSlotNum
	bool connected{true};  // TheNetwork->isPlayerConnected (a skirmish: the human and the computers are in the game)
	bool alive{true};      // !hasSinglePlayerBeenDefeated(its player)
	bool observer{false};  // its player isPlayerObserver
	std::u16string name;   // GameSlot::getName
	std::u16string side;   // getApparentPlayerTemplateDisplayName
	int team{-1};          // getTeamNumber
	std::uint32_t color{0}; // the apparent colour, 0xAARRGGBB
};

struct DiplomacyRow
{
	bool shown{false};
	int slot{-1};               // slotNumInRow
	std::u16string name, side, team, status;
	std::uint32_t color{0};     // name, side and team
	std::uint32_t statusColor{0};
	bool muteShown{false}, unmuteShown{false};
	bool operator==(const DiplomacyRow &) const = default;
};

inline constexpr std::uint32_t DiplomacyAlive = 0xFF00FF00u;          // GameMakeColor(0, 255, 0, 255)
inline constexpr std::uint32_t DiplomacyDead = 0xFFFF0000u;           // (255, 0, 0)
inline constexpr std::uint32_t DiplomacyObserver = 0xFFFFFFFFu;       // (255, 255, 255)
inline constexpr std::uint32_t DiplomacyGone = 0xFFC40000u;           // (196, 0, 0)
inline constexpr std::uint32_t DiplomacyObserverGone = 0xFFC4C4C4u;   // (196, 196, 196)

// PopulateInGameDiplomacyPopup: the occupied slots in order, one row each. In the game: a human's (but the AI's, which
// always are) if connected, the human in a skirmish (`network` false). Mute / unmute for another human still in the game
// (unmute while muted, mute else); the status alive (green "GUI:PlayerAlive"), else an observer's "GUI:PlayerObserver"
// (white) or "GUI:PlayerDead" (red); out of the game "GUI:PlayerObserverGone" (light grey) or "GUI:PlayerGone" (dark red).
// The team "Team:<team + 1>", "Team:AI" for a computer on no team. Rows past them hidden.
inline std::array<DiplomacyRow, DiplomacyRows> PopulateDiplomacy(std::span<const DiplomacySlot> slots, std::span<const bool> muted, bool network,
	const std::function<std::u16string(std::string_view)> &text)
{
	const auto fetch = [&](std::string_view label) { return text ? text(label) : std::u16string(label.begin(), label.end()); };
	std::array<DiplomacyRow, DiplomacyRows> rows{};
	std::size_t row = 0;
	for (std::size_t slot = 0; slot < slots.size() && slot < DiplomacyRows; ++slot)
	{
		const DiplomacySlot &each = slots[slot];
		if (!each.occupied)
			continue;
		bool inGame = false;
		if (network && each.connected)
			inGame = true;
		else if (!network && each.human)
			inGame = true;
		if (each.ai)
			inGame = true;
		const bool isMuted = slot < muted.size() && muted[slot];
		DiplomacyRow &shown = rows[row++];
		shown.shown = true;
		shown.slot = static_cast<int>(slot);
		if (each.human && !each.local && inGame)
		{
			shown.muteShown = !isMuted;
			shown.unmuteShown = isMuted;
		}
		shown.color = each.color;
		shown.name = each.name;
		shown.side = each.side;
		shown.team = fetch(each.ai && each.team == -1 ? std::string("Team:AI") : "Team:" + std::to_string(each.team + 1));
		if (inGame)
		{
			if (each.alive)
			{
				shown.statusColor = DiplomacyAlive;
				shown.status = fetch("GUI:PlayerAlive");
			}
			else if (each.observer)
			{
				shown.statusColor = DiplomacyObserver;
				shown.status = fetch("GUI:PlayerObserver");
			}
			else
			{
				shown.statusColor = DiplomacyDead;
				shown.status = fetch("GUI:PlayerDead");
			}
		}
		else if (each.observer)
		{
			shown.statusColor = DiplomacyObserverGone;
			shown.status = fetch("GUI:PlayerObserverGone");
		}
		else
		{
			shown.statusColor = DiplomacyGone;
			shown.status = fetch("GUI:PlayerGone");
		}
	}
	return rows;
}

// ProcessAnimateWindowSlideFromTop for one window: its rest, start and current y, velocity, and whether it is reversed.
struct TopSlide
{
	bool finished{true};
	bool reversed{false};
	int startY{0}, endY{0}, currentY{0};
	float velocity{0.0f};
};

inline constexpr float TopSlideMaxVelocity = 40.0f;
inline constexpr int TopSlideSlowDownThreshold = 80;
inline constexpr float TopSlideSlowDownRatio = 0.67f;
inline constexpr float TopSlideSpeedUpRatio = 2.0f - TopSlideSlowDownRatio;

// initAnimateWindow: from the display's width above its rest, at the top speed.
inline TopSlide StartTopSlide(int restY, int displayWidth)
{
	TopSlide slide;
	slide.finished = false;
	slide.endY = restY;
	slide.startY = slide.currentY = restY - displayWidth;
	slide.velocity = TopSlideMaxVelocity;
	return slide;
}

// updateAnimateWindow: y moves by the truncated velocity; past the rest it stops there, finished; within the threshold
// of it the velocity shrinks by the ratio, never below a pixel a frame.
inline bool StepTopSlide(TopSlide &slide)
{
	if (slide.finished)
		return true;
	slide.currentY += static_cast<int>(slide.velocity);
	if (slide.currentY > slide.endY)
	{
		slide.currentY = slide.endY;
		slide.finished = true;
		return true;
	}
	if (slide.endY - slide.currentY <= TopSlideSlowDownThreshold)
		slide.velocity *= TopSlideSlowDownRatio;
	if (slide.velocity < 1.0f)
		slide.velocity = 1.0f;
	return false;
}

// AnimateWindowManager::reverseAnimateWindow -> initReverseAnimateWindow: the velocity turned round, not finished.
inline void ReverseTopSlide(TopSlide &slide)
{
	slide.velocity = -slide.velocity;
	slide.reversed = true;
	slide.finished = false;
}

// reverseAnimateWindow: y moves by the truncated velocity; above the start it stops there, finished; near the rest the
// velocity grows by the speed-up ratio, else it is the top speed back up; never faster than the top speed.
inline bool StepTopSlideBack(TopSlide &slide)
{
	if (slide.finished)
		return true;
	slide.currentY += static_cast<int>(slide.velocity);
	if (slide.currentY < slide.startY)
	{
		slide.currentY = slide.startY;
		slide.finished = true;
		return true;
	}
	if (slide.endY - slide.currentY <= TopSlideSlowDownThreshold)
		slide.velocity *= TopSlideSpeedUpRatio;
	else
		slide.velocity = -TopSlideMaxVelocity;
	if (slide.velocity < -TopSlideMaxVelocity)
		slide.velocity = -TopSlideMaxVelocity;
	return false;
}

// Where the diplomacy window is asked for.
struct DiplomacyContext
{
	bool inputEnabled{true};     // TheInGameUI->getInputEnabled (and no intro movie, no map loading)
	bool quitMenuVisible{false};
	bool multiplayer{false};     // TheRecorder->isMultiplayer: the in-game panel, else the briefing history
	bool animate{true};          // GlobalData m_animateWindows
};

class DiplomacyViewModel
{
public:
	// `text`: a label's text; `slots`: the game's slots now (none outside a skirmish or network game: no TheGameInfo).
	DiplomacyViewModel(std::function<std::u16string(std::string_view)> text, std::function<std::optional<std::vector<DiplomacySlot>>()> slots)
		: m_text(std::move(text)), m_slots(std::move(slots))
	{
		hide.SetAction([this] { Hide(false); });
		for (std::size_t row = 0; row < DiplomacyRows; ++row)
		{
			mute[row].SetAction([this, row] { SetMuted(row, true); });
			unmute[row].SetAction([this, row] { SetMuted(row, false); });
		}
	}

	DiplomacyViewModel(const DiplomacyViewModel &) = delete;
	DiplomacyViewModel &operator=(const DiplomacyViewModel &) = delete;

	// The window's laid-out top and the display's width, in display pixels.
	void SetLayout(int restTop, int displayWidth)
	{
		m_restTop = restTop;
		m_displayWidth = displayWidth;
		if (m_slide.finished && !m_slide.reversed)
			top.Set(restTop);
	}
	void SetNetwork(bool network) noexcept { m_network = network; }
	// InGameUI::getMessageColor(0 / 1): the briefing history's alternating line colours (0xAARRGGBB).
	void SetMessageColors(std::uint32_t first, std::uint32_t second) noexcept { m_messageColors = {first, second}; }

	// ToggleDiplomacy: shows it when hidden (or never made), else hides it.
	void Toggle(bool immediate, const DiplomacyContext &context)
	{
		if (!m_made || !shown.Get())
			Show(immediate, context);
		else
			Hide(immediate);
	}

	// ShowDiplomacy: not without input, nor under the quit menu; shown, the in-game panel in a multiplayer game, the
	// briefing history otherwise; sliding down unless immediate (or windows are not animated); the rows filled.
	void Show(bool immediate, const DiplomacyContext &context)
	{
		if (!context.inputEnabled || context.quitMenuVisible)
			return;
		m_made = true;
		m_animate = context.animate;
		shown.Set(true);
		inGameShown.Set(context.multiplayer);
		soloShown.Set(!context.multiplayer);
		// theAnimateWindowManager->reset(): the window back at its rest, nothing registered.
		m_slide = TopSlide{};
		m_registered = false;
		top.Set(m_restTop);
		if (!immediate && context.animate)
		{
			m_slide = StartTopSlide(m_restTop, m_displayWidth);
			m_registered = true;
			top.Set(m_slide.currentY);
		}
		Populate();
	}

	// HideDiplomacy: at once (or unanimated) hidden; else, once the slide in is over, it slides back up and hides at its end.
	void Hide(bool immediate)
	{
		if (!m_made)
			return;
		if (immediate || !m_animate)
		{
			shown.Set(false);
			return;
		}
		// The manager reverses only what it has (a window shown at once registered none: it stays), and only when done.
		if (m_slide.finished && m_registered && !m_slide.reversed)
			ReverseTopSlide(m_slide);
	}

	// PopulateInGameDiplomacyPopup again (a player defeated) while it shows.
	void Refresh()
	{
		if (m_made && shown.Get())
			Populate();
	}

	// The window's layout update (updateFunc), once per frame: here every 1/30 s of real time.
	void Update(double seconds)
	{
		if (m_slide.finished)
		{
			m_pending = 0.0;
			return;
		}
		m_pending += (std::max)(seconds, 0.0);
		while (m_pending >= FrameSeconds && !m_slide.finished)
		{
			m_pending -= FrameSeconds;
			if (m_slide.reversed)
			{
				if (StepTopSlideBack(m_slide))
					shown.Set(false); // finished going back up: hidden
			}
			else
				StepTopSlide(m_slide);
			top.Set(m_slide.currentY);
		}
	}

	// UpdateDiplomacyBriefingText: `clear` empties the history first; a label not had yet joins it (its text in the
	// messages' alternating colours).
	void Briefing(std::string_view label, bool clear) { Briefing(std::u16string(label.begin(), label.end()), Text(label), clear); }

	// The same, by a key standing for the label (the host knows only the text a subtitle or popup showed) and its text.
	void Briefing(const std::u16string &key, const std::u16string &text, bool clear)
	{
		if (clear)
		{
			m_briefing.clear();
			briefingLines.Set({});
			briefingColors.Set({});
			briefingRowColors.Set({});
		}
		if (key.empty() || std::ranges::find(m_briefing, key) != m_briefing.end())
			return;
		m_briefing.push_back(key);
		auto lines = briefingLines.Get();
		auto colors = briefingColors.Get();
		auto rowColors = briefingRowColors.Get();
		const std::uint32_t argb = m_messageColors[lines.size() % 2];
		colors.push_back(argb);
		rowColors.push_back((argb << 8) | (argb >> 24)); // the list's rows take 0xRRGGBBAA
		lines.push_back(text);
		briefingColors.Set(std::move(colors));
		briefingRowColors.Set(std::move(rowColors));
		briefingLines.Set(std::move(lines));
	}

	// GameSlot::isMuted, as the player set it here.
	bool Muted(int slot) const noexcept { return slot >= 0 && slot < static_cast<int>(DiplomacyRows) && m_muted[static_cast<std::size_t>(slot)]; }
	bool Shown() const noexcept { return m_made && shown.Get(); }
	const TopSlide &Slide() const noexcept { return m_slide; }

	engine::gui::mvvm::Observable<bool> shown{false};
	engine::gui::mvvm::Observable<int> top{0};             // the window's top, display pixels
	engine::gui::mvvm::Observable<bool> inGameShown{false}; // InGameParent
	engine::gui::mvvm::Observable<bool> soloShown{false};   // SoloParent
	// Each row's windows (StaticTextPlayer / Side / Team / Status<n>, ButtonMute / ButtonUnMute<n>).
	std::array<engine::gui::mvvm::Observable<bool>, DiplomacyRows> rowShown, muteShown, unmuteShown;
	std::array<engine::gui::mvvm::Observable<std::u16string>, DiplomacyRows> rowName, rowSide, rowTeam, rowStatus;
	std::array<engine::gui::mvvm::Observable<std::uint32_t>, DiplomacyRows> rowColor, rowStatusColor; // 0xAARRGGBB
	std::array<engine::gui::mvvm::Command, DiplomacyRows> mute, unmute;
	// The row as last filled.
	const DiplomacyRow &Row(std::size_t row) const { return m_rows[row]; }
	engine::gui::mvvm::Command hide; // ButtonHide
	engine::gui::mvvm::Observable<std::vector<std::u16string>> briefingLines; // ListboxSolo
	engine::gui::mvvm::Observable<std::vector<std::uint32_t>> briefingColors;    // 0xAARRGGBB
	engine::gui::mvvm::Observable<std::vector<std::uint32_t>> briefingRowColors; // the same as 0xRRGGBBAA
	engine::gui::mvvm::Observable<int> noRow{-1};                                // ListboxSolo picks nothing
	engine::gui::mvvm::Observable<bool> never{false}; // the buddy panel and its radio buttons (GameSpy: not here)

private:
	static constexpr double FrameSeconds = 1.0 / 30.0;

	void Populate()
	{
		const std::optional<std::vector<DiplomacySlot>> slots = m_slots ? m_slots() : std::nullopt;
		if (!slots)
			return; // no TheGameInfo
		const auto populated = PopulateDiplomacy(*slots, m_muted, m_network, m_text);
		for (std::size_t row = 0; row < DiplomacyRows; ++row)
		{
			const DiplomacyRow &now = populated[row];
			m_slotInRow[row] = now.slot;
			m_rows[row] = now;
			rowShown[row].Set(now.shown);
			muteShown[row].Set(now.muteShown);
			unmuteShown[row].Set(now.unmuteShown);
			rowName[row].Set(now.name);
			rowSide[row].Set(now.side);
			rowTeam[row].Set(now.team);
			rowStatus[row].Set(now.status);
			rowColor[row].Set(now.color);
			rowStatusColor[row].Set(now.statusColor);
		}
	}

	// ButtonMute<row> / ButtonUnMute<row>: the row's slot muted or not (GameSlot::mute), the rows filled again.
	void SetMuted(std::size_t row, bool muted)
	{
		const int slot = m_slotInRow[row];
		if (slot < 0)
			return;
		m_muted[static_cast<std::size_t>(slot)] = muted;
		Populate();
	}

	std::u16string Text(std::string_view label) const { return m_text ? m_text(label) : std::u16string(label.begin(), label.end()); }

	std::function<std::u16string(std::string_view)> m_text;
	std::function<std::optional<std::vector<DiplomacySlot>>()> m_slots;
	std::array<bool, DiplomacyRows> m_muted{};
	std::array<int, DiplomacyRows> m_slotInRow{-1, -1, -1, -1, -1, -1, -1, -1};
	std::array<DiplomacyRow, DiplomacyRows> m_rows{};
	std::vector<std::u16string> m_briefing;
	std::array<std::uint32_t, 2> m_messageColors{0xFFFFFFFFu, 0xFFFFFFFFu};
	TopSlide m_slide;
	int m_restTop{0}, m_displayWidth{800};
	bool m_network{true};
	bool m_made{false};
	bool m_animate{true};
	bool m_registered{false}; // a slide registered with the window's animation manager
	double m_pending{0.0};
};
}
