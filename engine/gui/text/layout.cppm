export module engine.gui.text.layout;
import std;

export namespace engine::gui::text {
struct TextMetrics final
{
	const void *context = nullptr;
	int (*spacing)(const void *context, std::uint16_t character) noexcept = nullptr;
	int (*height)(const void *context) noexcept = nullptr;
	int (*extra_overlap)(const void *context) noexcept = nullptr;
};

struct TextLayoutOptions final
{
	int wrapping_width = 0;
	bool centered = false;
	bool parse_hotkey = false;
	std::uint16_t hotkey = 0;
	bool hard_wrap = false;
	bool right_aligned = false;
	bool snap_to_pixels = false;
};

struct TextPlacement final
{
	std::uint16_t character = 0;
	float x = 0.0f;
	float y = 0.0f;
	std::uint32_t line = 0;
	bool hotkey = false;
};

struct TextLine final
{
	std::uint32_t first = 0;
	std::uint32_t count = 0;
	float width = 0.0f;
};

class TextLayout final
{
public:
	explicit TextLayout(std::size_t placement_capacity = 8192, std::size_t line_capacity = 1024)
		: m_placement_capacity(placement_capacity), m_line_capacity(line_capacity)
	{
		m_placements.reserve(placement_capacity);
		m_lines.reserve(line_capacity);
	}

	bool Build(TextMetrics metrics, const std::uint16_t *text, TextLayoutOptions options) noexcept
	{
		m_placements.clear();
		m_lines.clear();
		m_width = 0.0f;
		m_height = 0;
		if (text == nullptr || metrics.context == nullptr || metrics.spacing == nullptr
			|| metrics.height == nullptr || metrics.extra_overlap == nullptr)
			return false;

		const int line_height = std::max(1, metrics.height(metrics.context));
		float cursor_x = 0.0f;
		std::uint32_t line = 0;
		std::uint32_t line_start = 0;
		bool hotkey_seen = false;

		for (std::size_t index = 0; text[index] != 0;) {
			std::uint16_t character = text[index++];
			if (character == static_cast<std::uint16_t>('\n')) {
				if (!Finish_Line(cursor_x, line, line_start))
					return false;
				continue;
			}

			bool hotkey = false;
			if (options.parse_hotkey && character == static_cast<std::uint16_t>('&')
				&& text[index] != 0 && text[index] > static_cast<std::uint16_t>(' ')
				&& text[index] != static_cast<std::uint16_t>('\n')) {
				character = text[index++];
				hotkey = !hotkey_seen && (options.hotkey == 0 || character == options.hotkey);
				if (hotkey)
					hotkey_seen = true;
			}

			const float spacing = static_cast<float>(metrics.spacing(metrics.context, character));
			if (character == static_cast<std::uint16_t>(' ')) {
				if (options.wrapping_width > 0) {
					float word_width = spacing;
					for (std::size_t word_index = index; text[word_index] != 0
						&& text[word_index] > static_cast<std::uint16_t>(' '); ++word_index) {
						if (options.parse_hotkey && text[word_index] == static_cast<std::uint16_t>('&')
							&& text[word_index + 1] != 0
							&& text[word_index + 1] > static_cast<std::uint16_t>(' ')
							&& text[word_index + 1] != static_cast<std::uint16_t>('\n'))
							++word_index;
						if (text[word_index] != 0)
							word_width += static_cast<float>(metrics.spacing(metrics.context, text[word_index]));
					}
					if (cursor_x > 0.0f && cursor_x + word_width >= static_cast<float>(options.wrapping_width)) {
						if (!Finish_Line(cursor_x, line, line_start))
							return false;
						continue;
					}
				}
				cursor_x += spacing;
				continue;
			}

			if (options.hard_wrap && options.wrapping_width > 0 && cursor_x > 0.0f
				&& cursor_x + spacing >= static_cast<float>(options.wrapping_width)) {
				if (!Finish_Line(cursor_x, line, line_start))
					return false;
			}

			if (m_placements.size() >= m_placement_capacity)
				return false;
			m_placements.push_back({character, cursor_x, static_cast<float>(line * line_height), line, hotkey});
			cursor_x += spacing;
			if (hotkey)
				cursor_x += static_cast<float>(metrics.extra_overlap(metrics.context));
		}

		if (!Finish_Line(cursor_x, line, line_start))
			return false;

		for (const TextLine &current_line : m_lines)
			m_width = std::max(m_width, current_line.width);
		if (options.centered || options.right_aligned) {
			for (TextPlacement &placement : m_placements)
				placement.x += (m_width - m_lines[placement.line].width) * (options.right_aligned ? 1.0f : 0.5f);
		}
		m_height = static_cast<std::uint32_t>(m_lines.size()) * static_cast<std::uint32_t>(line_height);
		return true;
	}

	std::span<const TextPlacement> Placements() const noexcept { return m_placements; }
	std::span<const TextLine> Lines() const noexcept { return m_lines; }
	float Width() const noexcept { return m_width; }
	std::uint32_t Height() const noexcept { return m_height; }

private:
	bool Finish_Line(float &cursor_x, std::uint32_t &line, std::uint32_t &line_start) noexcept
	{
		if (m_lines.size() >= m_line_capacity || m_placements.size() > std::numeric_limits<std::uint32_t>::max())
			return false;
		m_lines.push_back({line_start, static_cast<std::uint32_t>(m_placements.size()) - line_start, cursor_x});
		line_start = static_cast<std::uint32_t>(m_placements.size());
		cursor_x = 0.0f;
		++line;
		return true;
	}

	std::size_t m_placement_capacity = 0;
	std::size_t m_line_capacity = 0;
	std::vector<TextPlacement> m_placements;
	std::vector<TextLine> m_lines;
	float m_width = 0.0f;
	std::uint32_t m_height = 0;
};

}
