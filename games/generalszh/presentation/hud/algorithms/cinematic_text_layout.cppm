export module games.generalszh.presentation.hud.algorithms.cinematic_text_layout;
import std;

export import games.generalszh.presentation.hud.resources.cinematic_text;

// DISPLAY_CINEMATIC_TEXT: ScriptActions::doDisplayCinematicText (the text, its font parsed from WorldBuilder's
// "Name - Size:n [Bold]", LOGICFRAMES_PER_SECOND x seconds frames) and its drawing in W3DDisplay::draw, over the
// letterbox: white on nothing, word-wrapped centred at the screen's width less 20, centred across the screen (20 from
// the left when wider than it), its top nine tenths of the way down; one frame spent each drawn frame.
export namespace generalszh::presentation
{
// The font of "Name - Size:n [Bold]". The original's parse appends the rest of the string from each letter
// (AsciiString::concat(const char *)), naming a font that does not exist, which Windows then replaced with a default;
// that is a bug-like quirk, and the port takes the name WorldBuilder wrote: everything before " - ". The size is the
// digits after the first ':' up to a space (atoi), bold when it ends in "[Bold]".
inline CinematicFont ParseCinematicFont(std::string_view text)
{
	CinematicFont font;
	const std::size_t dash = text.find(" - ");
	font.name = std::string(text.substr(0, dash == std::string_view::npos ? text.find(' ') : dash));
	if (const std::size_t colon = text.find(':'); colon != std::string_view::npos)
	{
		std::string_view digits = text.substr(colon + 1);
		digits = digits.substr(0, digits.find(' '));
		std::int32_t size = 0;
		const char *begin = digits.data();
		while (begin != digits.data() + digits.size() && (*begin == ' ' || *begin == '\t'))
			++begin;
		std::from_chars(begin, digits.data() + digits.size(), size);
		font.size = size;
	}
	font.bold = text.ends_with("[Bold]");
	return font;
}

// doDisplayCinematicText(label, font, seconds): the fetched text, its font, LOGICFRAMES_PER_SECOND x seconds frames.
inline void SetCinematicText(CinematicText &cinematic, std::u16string text, std::string_view font, std::int64_t seconds)
{
	cinematic.text = std::move(text);
	cinematic.font = ParseCinematicFont(font);
	cinematic.frames = 30 * seconds;
	cinematic.shownSeconds = 0.0;
	++cinematic.serial;
}

inline void AdvanceCinematicText(CinematicText &cinematic, double realSeconds) { cinematic.shownSeconds += realSeconds; }

// W3DDisplay::draw: drawn while there is text and its frame count is not spent (a negative count never is); at 30 drawn
// frames a second, the frame due now is shownSeconds x 30.
inline bool CinematicTextShown(const CinematicText &cinematic) noexcept
{
	if (cinematic.text.empty() || cinematic.frames == 0)
		return false;
	return cinematic.frames < 0 || cinematic.shownSeconds * 30.0 < static_cast<double>(cinematic.frames);
}

struct CinematicTextPlace
{
	std::int32_t x{0};
	std::int32_t y{0};
	std::int32_t wrapWidth{0};
};

// Where W3DDisplay::draw puts it on a `width` x `height` screen for text `textWidth` wide (its wrapped width).
inline CinematicTextPlace PlaceCinematicText(std::int32_t width, std::int32_t height, std::int32_t textWidth) noexcept
{
	CinematicTextPlace place;
	place.wrapWidth = width - 20;
	place.y = static_cast<std::int32_t>(height * .9);
	place.x = textWidth > width ? 20 : (width - textWidth) / 2;
	return place;
}
}
