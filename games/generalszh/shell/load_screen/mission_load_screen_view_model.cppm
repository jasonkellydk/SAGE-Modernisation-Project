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

// ChallengeLoadScreen's two generals: the player's (ChallengeGenerals::getPlayerGeneralByCampaignName, the campaign's
// persona) on the left, the opponent's (getGeneralByGeneralName, the mission's Campaign.ini GeneralName) on the right.
struct ChallengeLoadGeneral
{
	std::u16string name, rank, strategy; // BioNameString, BioRankString, BioStrategyString, localized
	std::string nameSound;
	std::string portraitMovieLeft, portraitMovieRight; // PortraitMovieLeftName / PortraitMovieRightName
	std::string tauntSound;                            // the opponent's: getRandomTauntSound, picked by the host
};

struct ChallengeLoadSetup
{
	ChallengeLoadGeneral player, opponent;
	std::function<void(std::string_view)> sound; // TheAudio->addAudioEvent
};

// ChallengeLoadScreen::activatePieces' movie frames (LoadScreen.cpp's FRAME_* enum).
namespace challenge_load_frame
{
inline constexpr int TitlesStart = 20;           // FRAME_TITLES_START
inline constexpr int TeletypeStart = 24;         // FRAME_TELETYPE_START
inline constexpr int PortraitsStart = 35;        // FRAME_PORTRAITS_START
inline constexpr int OuterCircleAlphaShow = 63;  // FRAME_OUTER_CIRCLE_ALPHA_SHOW
inline constexpr int InnerCircleAlphaShow = 74;  // FRAME_INNER_CIRCLE_ALPHA_SHOW
inline constexpr int InnerBackdropAlphaShow = 80; // FRAME_INNER_BACKDROP_ALPHA_SHOW
inline constexpr int VsAnimStart = 98;           // FRAME_VS_ANIM_START
inline constexpr int RightVoice = 140;           // FRAME_RIGHT_VOICE
inline constexpr int TeletypeEvery = 2;          // TELETYPE_UPDATE_FREQ
inline constexpr std::string_view VsMovie = "VSSmall";
inline constexpr std::string_view VsSound = "Taunts_GCAnnouncer12";
}

// One side's bio on the challenge load screen: the titles, then the entries typed out one letter at a time.
struct ChallengeLoadBio
{
	engine::gui::mvvm::Observable<std::u16string> bigName, name, rank, strategy;
};

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
		m_pieces.reset();
		m_tauntPlayed = false;
		titlesShown.Set(false);
		entriesShown.Set(false);
		portraitMoviesShown.Set(false);
		outerCircleShown.Set(false);
		innerCircleShown.Set(false);
		versusBackdropShown.Set(false);
		versusShown.Set(false);
		for (ChallengeLoadBio *bio : {&left, &right})
			for (auto *text : {&bio->bigName, &bio->name, &bio->rank, &bio->strategy})
				text->Set(u"");
		portraitMovieLeft.Set("");
		portraitMovieRight.Set("");
		versusMovie.Set("");
	}

	// ChallengeLoadScreen::init: the two generals its movie frames bring on (activatePieces).
	void InitChallenge(ChallengeLoadSetup setup) { m_pieces = std::move(setup); }

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
		if (every > 0 && frameIndex % every == 0)
		{
			m_shiftedPercent = std::min(m_shiftedPercent + 1, 0);
			const int percent = MissionLoadPercent(m_shiftedPercent);
			progress.Set(percent);
			percentText.Set(PercentText(percent));
		}
		if (m_challenge && m_pieces)
			ActivatePieces(frameIndex);
	}

	// The movie over: the background picture shows through (setVideoBuffer(NULL)); the challenge screen keeps its last
	// frame (ChallengeLoadScreen::init leaves the video buffer set).
	void EndMovie()
	{
		if (!m_challenge)
			movieShown.Set(false);
		// ChallengeLoadScreen::init after its loop: the opponent's random taunt.
		if (m_challenge && m_pieces && !std::exchange(m_tauntPlayed, true))
			Play(m_pieces->opponent.tauntSound);
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
	// The challenge screen's pieces (activatePieces): the bio titles (BioName/Birthplace/StrategyLeft and Right), the
	// entries (BigNameEntry, BioNameEntry, BioBirthplaceEntry: the rank, BioStrategyEntry), the portrait movies, the
	// reticle's circles, the versus backdrop and overlay; the movies each window plays (empty: none yet).
	engine::gui::mvvm::Observable<bool> titlesShown{false}, entriesShown{false}, portraitMoviesShown{false};
	engine::gui::mvvm::Observable<bool> outerCircleShown{false}, innerCircleShown{false}, versusBackdropShown{false}, versusShown{false};
	ChallengeLoadBio left, right;
	engine::gui::mvvm::Observable<std::string> portraitMovieLeft, portraitMovieRight, versusMovie;

private:
	void Play(std::string_view sound) const
	{
		if (m_pieces && m_pieces->sound && !sound.empty())
			m_pieces->sound(sound);
	}

	// updateTeletypeText: one more letter of `full`, none past its end.
	static void Teletype(engine::gui::mvvm::Observable<std::u16string> &window, const std::u16string &full)
	{
		const std::u16string shown = window.Get();
		if (shown.size() < full.size())
			window.Set(shown + full[shown.size()]);
	}

	// ChallengeLoadScreen::activatePieces for the movie frame `frame`.
	void ActivatePieces(int frame)
	{
		namespace at = challenge_load_frame;
		const ChallengeLoadSetup &pieces = *m_pieces;
		switch (frame)
		{
		case at::TitlesStart: titlesShown.Set(true); break;
		case at::TeletypeStart:
			entriesShown.Set(true);
			for (ChallengeLoadBio *bio : {&left, &right})
				for (auto *text : {&bio->bigName, &bio->name, &bio->rank, &bio->strategy})
					text->Set(u"");
			break;
		case at::PortraitsStart:
			portraitMovieLeft.Set(pieces.player.portraitMovieLeft);
			portraitMovieRight.Set(pieces.opponent.portraitMovieRight);
			portraitMoviesShown.Set(true);
			Play(pieces.player.nameSound);
			break;
		case at::OuterCircleAlphaShow: outerCircleShown.Set(true); break;
		case at::InnerCircleAlphaShow: innerCircleShown.Set(true); break;
		case at::InnerBackdropAlphaShow: versusBackdropShown.Set(true); break;
		case at::VsAnimStart:
			versusBackdropShown.Set(true);
			versusShown.Set(true);
			versusMovie.Set(std::string(at::VsMovie));
			Play(at::VsSound);
			break;
		case at::RightVoice: Play(pieces.opponent.nameSound); break;
		default: break;
		}
		if (frame > at::TeletypeStart && frame % at::TeletypeEvery == 0)
		{
			Teletype(left.name, pieces.player.name);
			Teletype(left.bigName, pieces.player.name);
			Teletype(left.rank, pieces.player.rank);
			Teletype(left.strategy, pieces.player.strategy);
			Teletype(right.name, pieces.opponent.name);
			Teletype(right.bigName, pieces.opponent.name);
			Teletype(right.rank, pieces.opponent.rank);
			Teletype(right.strategy, pieces.opponent.strategy);
		}
	}

	std::optional<ChallengeLoadSetup> m_pieces;
	bool m_tauntPlayed{false};
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
