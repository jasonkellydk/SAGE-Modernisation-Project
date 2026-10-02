export module games.generalszh.presentation.hud.resources.military_caption;
import std;

import engine.ecs.system.system;

// The military subtitle (the original's InGameUI m_militarySubtitle, SHOW_MILITARY_CAPTION): its text typed a letter
// at a time into up to MaxLines lines, a block blinking after the last letter, then fading once its time is up. Ticks
// are the presentation's (one a logic frame). And how captions look and type: InGameUI.ini's MilitaryCaptionColor,
// Language.ini's MilitaryCaptionSpeed (ticks between letters) and MilitaryCaptionDelayMS (before the first letter and
// each new line, as whole ticks: LOGICFRAMES_PER_SECOND * ms / 1000).
export namespace generalszh::presentation
{
struct MilitaryCaption
{
	static constexpr std::size_t MaxLines = 4; // MAX_SUBTITLE_LINES

	bool shown{false};
	std::u16string text;
	std::uint64_t lifetime{0};        // the tick its time is up
	std::uint64_t blockBeginTick{0};  // when the block last blinked
	std::uint64_t incrementOnTick{0}; // the next letter comes after this tick
	std::size_t index{0};             // the next letter of `text`
	std::vector<std::u16string> lines;
	bool blockDrawn{true};
	std::array<std::uint8_t, 4> color{};

	std::array<std::uint8_t, 4> baseColor{200, 200, 30, 255};
	std::uint64_t speedTicks{0};
	std::uint64_t delayTicks{22};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::MilitaryCaption>
{
	static constexpr std::string_view StableName = "generalszh.presentation.military_caption";
};
}
