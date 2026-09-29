export module games.generalszh.shell.challenge.challenge_view_model;
import std;

export import games.generalszh.shell.model.shell_model;

// The Generals' Challenge menu (the original ChallengeMenu.cpp, without windows):
// a medallion per general on the map; pointing at one types its bio in
// (two letters a frame, teletype style) under its portrait, choosing one
// plays its preview and offers Play, which starts its challenge at the
// difficulty the main menu picked. The announcer asks to choose a general
// once the menu has faded in.
export namespace generalszh::shell
{
struct ChallengeGeneral
{
	bool enabled{false};                    // StartsEnabled: shown and clickable
	std::string normal, hilite, selected;   // the medallion's images (its faction's)
	std::string portrait;                   // BioPortraitSmall
	std::u16string name, rank, branch, strategy; // the bio's four lines, localized
	std::string previewSound;
	std::string campaign, playerTemplate;
};

struct ChallengeServices
{
	std::function<void(std::string_view)> sound; // an interface sound
	std::function<void(std::string_view)> voice; // a voice in place of the last (empty: it stops)
	std::function<void(const ChallengeGeneral &, Difficulty)> start;
};

class ChallengeViewModel
{
public:
	static constexpr std::size_t Count = 12;   // NUM_GENERALS
	static constexpr int TeletypeSkip = 2;     // TELETYPE_SKIP: letters a frame
	static constexpr int IntroDelay = 10;      // frames after the fade before the announcer

	ChallengeViewModel(ShellModel &model, std::vector<ChallengeGeneral> generals, ChallengeServices services)
		: m_model(model), m_generals(std::move(generals)), m_services(std::move(services))
	{
		m_generals.resize(Count);
		for (std::size_t index = 0; index < Count; ++index)
		{
			const int at = static_cast<int>(index);
			select[index].SetAction([this, at] { Select(at); });
			enter[index].SetAction([this, at] { Enter(at); });
			leave[index].SetAction([this, at] { Leave(at); });
		}
		play.SetAction([this] { Play(); });
		back.SetAction([this] { m_model.Pop(); });
	}

	ChallengeViewModel(const ChallengeViewModel &) = delete;
	ChallengeViewModel &operator=(const ChallengeViewModel &) = delete;

	const std::vector<ChallengeGeneral> &Generals() const noexcept { return m_generals; }

	// Each general's medallion down (chosen); the bio panel and Play; the bio's portrait and lines as typed so far.
	std::array<engine::gui::mvvm::Observable<bool>, Count> checked;
	engine::gui::mvvm::Observable<bool> bioShown{false}, playShown{false};
	engine::gui::mvvm::Observable<std::string> portrait;
	std::array<engine::gui::mvvm::Observable<std::u16string>, 4> lines;

	std::array<engine::gui::mvvm::Command, Count> select, enter, leave;
	engine::gui::mvvm::Command play, back;

	// ChallengeMenuInit: nothing chosen, no bio, no Play.
	void Open()
	{
		for (auto &down : checked)
			down.Set(false);
		bioShown.Set(false);
		playShown.Set(false);
		m_last = -1;
		m_introPlayed = false;
	}

	// ChallengeMenuShutdown: the voices stop.
	void Close()
	{
		m_last = -1;
		m_introFrames = 0;
		if (m_services.voice)
			m_services.voice("");
	}

	// One ChallengeMenuUpdate (a 30 Hz frame): the announcer once the fade is done, the bio typing on.
	void Update(bool transitionsFinished)
	{
		if (!m_introPlayed && transitionsFinished && ++m_introFrames == IntroDelay)
		{
			if (m_services.sound)
				m_services.sound("Taunts_GCAnnouncer01"); // "Choose your general."
			m_introPlayed = true;
		}
		for (int step = 0; step < TeletypeSkip && m_position < m_total; ++step, ++m_position)
		{
			int at = m_position;
			std::size_t line = 0;
			while (line < 3 && at >= static_cast<int>(m_full[line].size()))
				at -= static_cast<int>(m_full[line++].size());
			std::u16string text = lines[line].Get();
			text.push_back(m_full[line][static_cast<std::size_t>(at)]);
			lines[line].Set(std::move(text));
		}
	}

	int Chosen() const noexcept { return m_last; }

private:
	bool Valid(int index) const noexcept { return index >= 0 && static_cast<std::size_t>(index) < Count; }

	// setGeneralBio: the panel shows, the portrait changes and the lines start typing again.
	void SetBio(int index)
	{
		if (!Valid(index))
			return;
		const ChallengeGeneral &general = m_generals[static_cast<std::size_t>(index)];
		bioShown.Set(true);
		portrait.Set(general.portrait);
		m_full = {general.name, general.rank, general.branch, general.strategy};
		m_position = 0;
		m_total = 0;
		for (std::size_t line = 0; line < 4; ++line)
		{
			m_total += static_cast<int>(m_full[line].size());
			lines[line].Set(std::u16string{});
		}
	}

	// GBM_MOUSE_ENTERING: preview the bio (not the chosen general's again).
	void Enter(int index)
	{
		if (index == m_last)
			return;
		SetBio(index);
		if (m_services.sound)
			m_services.sound("GUILogoMouseOver");
	}

	// GBM_MOUSE_LEAVING: back to the chosen general's bio, if there is one.
	void Leave(int index)
	{
		if (index != m_last)
			SetBio(m_last);
	}

	// GBM_SELECTED on a medallion: exactly one stays down; its preview plays in place of the last voice.
	void Select(int index)
	{
		if (Valid(m_last))
			checked[static_cast<std::size_t>(m_last)].Set(false);
		checked[static_cast<std::size_t>(index)].Set(true);
		if (m_services.voice)
			m_services.voice(m_generals[static_cast<std::size_t>(index)].previewSound);
		m_last = index;
		playShown.Set(true);
	}

	// ButtonPlay: the chosen general's challenge; the medallion comes back up for when the player returns.
	void Play()
	{
		if (!Valid(m_last))
			return;
		const int chosen = std::exchange(m_last, -1);
		checked[static_cast<std::size_t>(chosen)].Set(false);
		if (m_services.start)
			m_services.start(m_generals[static_cast<std::size_t>(chosen)], m_model.campaignDifficulty.Get());
	}

	ShellModel &m_model;
	std::vector<ChallengeGeneral> m_generals;
	ChallengeServices m_services;
	int m_last{-1}; // lastButtonIndex
	std::array<std::u16string, 4> m_full;
	int m_position{0}, m_total{0};
	int m_introFrames{0};
	bool m_introPlayed{false};
};
}
