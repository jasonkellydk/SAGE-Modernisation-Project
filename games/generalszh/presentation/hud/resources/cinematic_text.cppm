export module games.generalszh.presentation.hud.resources.cinematic_text;
import std;

import engine.ecs.system.system;

// The cinematic text (DISPLAY_CINEMATIC_TEXT: the original's Display m_cinematicText, m_cinematicFont and
// m_cinematicTextFrames): the words drawn over the letterbox, in their font, for so many 30-a-second frames of real
// time (the original counted its drawn frames, 30 a second; a negative count never runs out).
export namespace generalszh::presentation
{
struct CinematicFont
{
	std::string name;
	std::int32_t size{0};
	bool bold{false};
};

struct CinematicText
{
	std::u16string text;
	CinematicFont font;
	std::int64_t frames{0}; // as set: LOGICFRAMES_PER_SECOND x seconds
	double shownSeconds{0}; // how long it has shown since
	std::uint32_t serial{0}; // changes with each new text (the host rebuilds its font)
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::CinematicText>
{
	static constexpr std::string_view StableName = "generalszh.presentation.cinematic_text";
};
}
