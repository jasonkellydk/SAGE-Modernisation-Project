export module games.generalszh.presentation.objects.resources.floating_texts;
import std;

import engine.ecs.system.system;

// Text floating up over the world (the original's InGameUI floating text:
// "+$300" where a supply truck delivered): what it says, where it started, its
// colour, how many ticks it has shown. After `timeoutTicks` it fades, its
// alpha falling by (ticks past the timeout) x `vanishPerTick` every tick; the
// view draws it `riseRate` pixels a tick higher, in the player's colour with a
// black drop shadow, and it is gone at no alpha. The model a view draws from.
export namespace generalszh::presentation
{
struct FloatingText
{
	std::u16string text;
	std::array<float, 3> at{};
	std::array<float, 3> color{1.0f, 1.0f, 1.0f};
	int alpha{230};
	std::uint32_t ticks{0};
};

struct FloatingTextSettings
{
	std::uint32_t timeoutTicks{10}; // FloatingTextTimeOut (333 ms)
	float riseRate{1.0f};           // FloatingTextMoveUpSpeed (30 a second), pixels a tick
	float vanishPerTick{0.1f};      // FloatingTextVanishRate (3 a second)
	std::u16string addCash{u"$%d"}; // GUI:AddCash
	std::u16string loseCash{u"-$%d"}; // GUI:LoseCash
};

struct FloatingTexts
{
	std::vector<FloatingText> shown; // newest first, as the original's list
};

// UnicodeString::format of a "%d" pattern.
inline std::u16string FormatAmount(const std::u16string &pattern, std::int64_t amount)
{
	const std::size_t at = pattern.find(u"%d");
	if (at == std::u16string::npos)
		return pattern;
	std::u16string out = pattern.substr(0, at);
	for (const char digit : std::to_string(amount))
		out.push_back(static_cast<char16_t>(digit));
	out += pattern.substr(at + 2);
	return out;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::FloatingTexts>
{
	static constexpr std::string_view StableName = "generalszh.presentation.floating_texts";
};
template<>
struct ResourceTraits<generalszh::presentation::FloatingTextSettings>
{
	static constexpr std::string_view StableName = "generalszh.presentation.floating_text_settings";
};
}
