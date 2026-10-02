export module games.generalszh.shell.intro.intro_sequence;
import std;

// The intro before the shell (the original's Intro, run by GameClient::update): the EA logo movie, a wait of 1 s and
// the sizzle movie, then done. PlayIntro allows the logo; PlaySizzle the sizzle movie (and its wait only with the
// intro). A stage begins once no movie plays and the wait is over; Esc skips the stage playing (a movie stopped, a wait
// cut short). Headless: the host plays the movies. (TheSuperHackers' credit screen and its wait, which the fork adds
// between the two, are left out by the user's direction.)
export namespace generalszh::shell
{
enum class IntroStage : std::uint8_t
{
	Start,
	EALogoMovie,
	SizzleMovieWait,
	SizzleMovie,
	Done,
};

struct IntroSequence
{
	IntroStage stage{IntroStage::Start};
	std::uint32_t allowed{0};
	std::uint64_t waitUntilMs{0};
};

// What a stage's start asks of the host: a movie to play ("EALogoMovie", "Sizzle"; the 640 versions are for machines
// failing the memory check, which is not made here).
struct IntroStep
{
	std::string movie;
	bool done{false}; // doPostIntro: the shell goes on
};

inline IntroSequence MakeIntro(bool playIntro, bool playSizzle)
{
	IntroSequence intro;
	const auto allow = [&](IntroStage stage) { intro.allowed |= 1u << static_cast<unsigned>(stage); };
	if (playIntro)
		allow(IntroStage::EALogoMovie);
	if (playSizzle)
	{
		if (playIntro)
			allow(IntroStage::SizzleMovieWait);
		allow(IntroStage::SizzleMovie);
	}
	return intro;
}

inline bool IntroDone(const IntroSequence &intro) noexcept { return intro.stage == IntroStage::Done; }

namespace intro_detail
{
// Intro::enterNextState: on to the next allowed stage (Done is always there).
inline void EnterNext(IntroSequence &intro)
{
	auto stage = static_cast<unsigned>(intro.stage);
	while (stage < static_cast<unsigned>(IntroStage::Done))
	{
		++stage;
		if ((intro.allowed & (1u << stage)) != 0)
			break;
	}
	intro.stage = static_cast<IntroStage>(stage);
}
}

// Intro::update at `nowMs` (timeGetTime): with no movie playing and the wait over, the next stage starts.
inline IntroStep UpdateIntro(IntroSequence &intro, std::uint64_t nowMs, bool moviePlaying)
{
	IntroStep step;
	if (IntroDone(intro) || moviePlaying || intro.waitUntilMs >= nowMs)
		return step;
	intro_detail::EnterNext(intro);
	switch (intro.stage)
	{
	case IntroStage::EALogoMovie: step.movie = "EALogoMovie"; break;
	case IntroStage::SizzleMovieWait: intro.waitUntilMs = nowMs + 1000; break;
	case IntroStage::SizzleMovie: step.movie = "Sizzle"; break;
	case IntroStage::Done: step.done = true; break;
	default: break;
	}
	return step;
}

// Intro::skipCurrentIntroStage (Esc): the wait over; a movie stopped (the host's: `stopMovie`), on to the next stage.
// False once done.
inline bool SkipIntroStage(IntroSequence &intro, bool &stopMovie)
{
	stopMovie = false;
	const bool skipping = !IntroDone(intro);
	intro.waitUntilMs = 0;
	if (intro.stage == IntroStage::EALogoMovie || intro.stage == IntroStage::SizzleMovie)
	{
		stopMovie = true;
		intro_detail::EnterNext(intro);
	}
	return skipping;
}
}
