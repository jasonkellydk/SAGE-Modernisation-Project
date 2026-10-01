export module games.generalszh.shell.load_screen.mission_load_screen_view_model;
import std;

export import engine.gui.mvvm.observable;

// A mission's load screen (LoadScreen.cpp SinglePlayerLoadScreen, ChallengeLoadScreen): the mission's movie (Campaign.ini
// IntroMovie) plays in the screen's background window while its bar climbs to its first 23 percent, then the load
// carries the bar on from there ((percent + FRAME_FUDGE_ADD) / 1.3). A campaign's mission shows its side's background and
// bar colour once its movie plays (USA blue, GLA green, China red); its percent text stays hidden.
export namespace generalszh::shell
{
inline constexpr int FrameFudgeAdd = 30; // FRAME_FUDGE_ADD

// SinglePlayerLoadScreen::update / ChallengeLoadScreen::update: the load's percent past the movie's part, (percent + 30) /
// 1.3 truncated to Int; in whole numbers * 10 / 13, the same for every percent the load reports (0..101: checked against
// the original's division).
constexpr int MissionLoadPercent(int percent) noexcept
{
	return (percent + FrameFudgeAdd) * 10 / 13;
}

// The campaign's background and bar centre once its movie plays (empty: the layout's own).
struct MissionLoadLook
{
	std::string background;
	std::string bar;
	bool operator==(const MissionLoadLook &) const = default;
};

// SinglePlayerLoadScreen::init: by the campaign's name, ignoring case.
inline MissionLoadLook MissionLoadLookFor(std::string_view campaign)
{
	const auto same = [campaign](std::string_view name) {
		return campaign.size() == name.size() && std::ranges::equal(campaign, name, [](char a, char b) {
			const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
			return lower(a) == lower(b);
		});
	};
	if (same("USA"))
		return {"MissionLoad_USA", "LoadingBar_ProgressCenter2"};
	if (same("GLA"))
		return {"MissionLoad_GLA", "LoadingBar_ProgressCenter3"};
	if (same("China"))
		return {"MissionLoad_China", "LoadingBar_ProgressCenter1"};
	return {};
}

class MissionLoadScreenViewModel
{
public:
	// init: the bar at 0, the percent "0%" and hidden. `challenge`: ChallengeLoadScreen (no side look).
	void Init(bool challenge)
	{
		m_challenge = challenge;
		m_movieFrames = 0;
		m_shiftedPercent = -FrameFudgeAdd + 1;
		progress.Set(0);
		percentText.Set(u"0%");
		percentShown.Set(false);
		movieShown.Set(false);
		look.Set({});
	}

	// The mission's movie opened (`frameCount` frames): a campaign's side look, the movie in the background window.
	void StartMovie(std::string_view campaign, int frameCount)
	{
		m_movieFrames = frameCount;
		if (!m_challenge)
			look.Set(MissionLoadLookFor(campaign));
		movieShown.Set(true);
	}

	// One movie frame rendered (`frameIndex`: the stream's index after frameNext): every frameCount / 30 frames the
	// bar steps one of its first 30, shown as (step + 30) / 1.3.
	void MovieFrame(int frameIndex)
	{
		const int every = m_movieFrames / FrameFudgeAdd;
		if (every <= 0 || frameIndex % every != 0)
			return;
		m_shiftedPercent = std::min(m_shiftedPercent + 1, 0);
		const int percent = MissionLoadPercent(m_shiftedPercent);
		progress.Set(percent);
		percentText.Set(PercentText(percent));
	}

	// The movie over: the background picture shows through (setVideoBuffer(NULL)); the challenge screen keeps its last
	// frame (ChallengeLoadScreen::init leaves the video buffer set).
	void EndMovie()
	{
		if (!m_challenge)
			movieShown.Set(false);
	}

	// update: the load's percent past the movie's part.
	void Update(int percent)
	{
		const int shown = MissionLoadPercent(percent);
		progress.Set(shown);
		if (!m_challenge)
			percentText.Set(PercentText(shown));
	}

	engine::gui::mvvm::Observable<int> progress{0};
	engine::gui::mvvm::Observable<std::u16string> percentText;
	engine::gui::mvvm::Observable<bool> percentShown{false};
	engine::gui::mvvm::Observable<bool> movieShown{false};
	engine::gui::mvvm::Observable<MissionLoadLook> look;

private:
	static std::u16string PercentText(int percent)
	{
		const std::string text = std::to_string(percent) + "%";
		return {text.begin(), text.end()};
	}

	bool m_challenge{false};
	int m_movieFrames{0};
	int m_shiftedPercent{-FrameFudgeAdd + 1};
};
}
