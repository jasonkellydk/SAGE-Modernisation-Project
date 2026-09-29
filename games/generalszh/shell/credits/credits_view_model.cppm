export module games.generalszh.shell.credits.credits_view_model;
import std;

export import games.generalszh.shell.model.shell_model;

// The credits (the original's CreditsManager and CreditsMenu, without
// windows): Credits.ini's lines in their styles, scrolled up the screen a
// few pixels a frame (30 a second), each line entering at the bottom edge
// once the previous one has cleared it by its height plus 2 pixels, leaving
// past the top, fading in the top and bottom thirds; a column line puts its
// two texts at a third and two thirds of the width. When the last line has
// gone, or the player presses Escape, the screen closes.
export namespace generalszh::shell
{
enum class CreditStyle : std::uint8_t
{
	Title,
	MinorTitle, // POSITION in the original
	Normal,
	Column,
	Blank,
};

struct CreditLine
{
	CreditStyle style{CreditStyle::Normal};
	std::u16string text;
	std::u16string second; // a column line's right text
	bool done{false};      // a column line with both its texts
};

struct CreditsSettings
{
	int scrollRate{1};        // pixels a step
	int scrollEveryFrames{1}; // frames between steps
	bool scrollDown{false};
	std::array<std::uint32_t, 3> colors{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu}; // title, minor title, normal (ARGB)
	std::vector<CreditLine> lines;
};

// Credits.ini ("Credits" block: ScrollRate, ScrollRateEveryFrames, ScrollDown, the colors,
// then Style / Text / Blank in order). `translate` looks up labels (texts with a ':').
inline CreditsSettings ReadCredits(std::string_view text, const std::function<std::u16string(std::string_view)> &translate)
{
	CreditsSettings credits;
	CreditStyle style = CreditStyle::Normal;
	const auto trim = [](std::string_view value) {
		while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
			value.remove_prefix(1);
		while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
			value.remove_suffix(1);
		return value;
	};
	const auto color = [](std::string_view value) {
		std::uint32_t channels[4] = {0, 0, 0, 255};
		const char *labels = "RGBA";
		for (int index = 0; index < 4; ++index)
			if (const auto at = value.find(std::string{labels[index], ':'}); at != std::string_view::npos)
				channels[index] = static_cast<std::uint32_t>(std::stoul(std::string(value.substr(at + 2))));
		return (channels[3] << 24) | (channels[0] << 16) | (channels[1] << 8) | channels[2];
	};
	// getUnicodeString: <BLANK> is empty, a label (with ':') is looked up, a quoted text is itself.
	const auto words = [&](std::string_view value) -> std::u16string {
		if (value == "<BLANK>")
			return {};
		if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
		{
			value = value.substr(1, value.size() - 2);
			return std::u16string(value.begin(), value.end());
		}
		if (value.find(':') != std::string_view::npos)
			return translate(value);
		return std::u16string(value.begin(), value.end());
	};
	bool inside = false;
	while (!text.empty())
	{
		const auto end = text.find('\n');
		std::string_view line = text.substr(0, end);
		text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
		if (const auto comment = line.find(';'); comment != std::string_view::npos)
			line = line.substr(0, comment);
		line = trim(line);
		if (line.empty())
			continue;
		if (!inside)
		{
			inside = line == "Credits";
			continue;
		}
		if (line == "End")
			break;
		if (line == "Blank")
		{
			credits.lines.push_back({CreditStyle::Blank});
			continue;
		}
		const auto equals = line.find('=');
		if (equals == std::string_view::npos)
			continue;
		const std::string_view key = trim(line.substr(0, equals)), value = trim(line.substr(equals + 1));
		if (key == "ScrollRate")
			credits.scrollRate = std::max(1, std::stoi(std::string(value)));
		else if (key == "ScrollRateEveryFrames")
			credits.scrollEveryFrames = std::max(1, std::stoi(std::string(value)));
		else if (key == "ScrollDown")
			credits.scrollDown = value == "Yes" || value == "YES" || value == "yes";
		else if (key == "TitleColor")
			credits.colors[0] = color(value);
		else if (key == "MinorTitleColor")
			credits.colors[1] = color(value);
		else if (key == "NormalColor")
			credits.colors[2] = color(value);
		else if (key == "Style")
			style = value == "TITLE" ? CreditStyle::Title : value == "MINORTITLE" ? CreditStyle::MinorTitle : value == "COLUMN" ? CreditStyle::Column : CreditStyle::Normal;
		else if (key == "Text")
		{
			// addText: a column's second text joins the column line before it.
			if (style == CreditStyle::Column && !credits.lines.empty() && credits.lines.back().style == CreditStyle::Column && !credits.lines.back().done)
			{
				credits.lines.back().second = words(value);
				credits.lines.back().done = true;
			}
			else
				credits.lines.push_back({style, words(value)});
		}
	}
	return credits;
}

// A line on screen: what it says, in which style, where (its top) and how opaque
// (the fraction `fade` / `of` of its colour's alpha).
struct ShownCredit
{
	const CreditLine *line{nullptr};
	int y{0};
	int height{0};
	int fade{1};
	int of{1};
};

class CreditsViewModel
{
public:
	// `heights`: each style's line height in pixels (title, minor title, normal; a blank is a normal line).
	CreditsViewModel(ShellModel &model, CreditsSettings credits, std::array<int, 3> heights, int displayHeight)
		: m_model(model), m_credits(std::move(credits)), m_heights(heights), m_displayHeight(displayHeight)
	{
	}

	// A frame (the original's CreditsManager::update, called once a frame).
	void Step()
	{
		if (m_finished)
			return;
		++m_frames;
		if (m_frames % m_credits.scrollEveryFrames != 0)
			return;
		const int start = m_credits.scrollDown ? 0 : m_displayHeight;
		const int end = m_credits.scrollDown ? m_displayHeight : 0;
		const int offsetStart = m_credits.scrollDown ? -1 : 0;
		const int offsetEnd = m_credits.scrollDown ? 0 : 1;
		const int direction = m_credits.scrollDown ? 1 : -1;
		int y = 0, yTest = 0, lastHeight = 0;
		for (auto it = m_shown.begin(); it != m_shown.end();)
		{
			y = it->y = it->y + m_credits.scrollRate * direction;
			lastHeight = it->height;
			yTest = y + (lastHeight + SpaceOffset) * offsetEnd;
			if ((m_credits.scrollDown && yTest > end) || (!m_credits.scrollDown && yTest < end))
				it = m_shown.erase(it);
			else
				++it;
		}
		// Time for the next line when the last has cleared the edge (or none is showing).
		if (!((m_credits.scrollDown && yTest >= start) || (!m_credits.scrollDown && yTest <= start)))
			return;
		if (m_shown.empty() && m_next == m_credits.lines.size())
		{
			m_finished = true;
			m_model.Pop(); // CreditsMenuUpdate: finished, the menu closes
			return;
		}
		if (m_next == m_credits.lines.size())
			return;
		const CreditLine &line = m_credits.lines[m_next++];
		const int height = line.style == CreditStyle::Title ? m_heights[0] : line.style == CreditStyle::MinorTitle ? m_heights[1] : m_heights[2];
		m_shown.push_back({&line, start + height * offsetStart, height, 1, 1});
	}

	// The lines on screen, faded in the top and bottom thirds (CreditsManager::draw).
	std::vector<ShownCredit> Shown() const
	{
		std::vector<ShownCredit> shown = m_shown;
		const int chunk = m_displayHeight / 3;
		for (ShownCredit &credit : shown)
		{
			const int y = credit.y;
			credit.of = std::max(chunk, 1);
			if (y < chunk || y > chunk * 2)
				credit.fade = y < 0 || y > m_displayHeight ? 0 : y < chunk ? y : chunk - (y - 2 * chunk);
			else
				credit.fade = credit.of;
		}
		return shown;
	}

	const CreditsSettings &Credits() const noexcept { return m_credits; }
	bool Finished() const noexcept { return m_finished; }

	// Escape (released): the menu closes.
	void Leave() { m_model.Pop(); }

private:
	static constexpr int SpaceOffset = 2; // CREDIT_SPACE_OFFSET
	ShellModel &m_model;
	CreditsSettings m_credits;
	std::array<int, 3> m_heights;
	int m_displayHeight;
	std::vector<ShownCredit> m_shown;
	std::size_t m_next{0};
	std::uint64_t m_frames{0};
	bool m_finished{false};
};
}
