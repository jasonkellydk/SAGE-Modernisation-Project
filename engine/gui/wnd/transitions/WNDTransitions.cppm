export module Engine.UI.WND.Transitions;
import std;

import Engine.UI.WND;
import Engine.UI.WND.Document;
import Graphics.Renderer2D;

// Window transitions (the original's GameWindowTransitions and its styles):
// groups of windows that flash, fade, grow or type themselves in, each after
// its frame delay, and play back out when reversed. One group plays at a time
// and one may wait behind it. Time is counted in the original's 30-a-second
// frames, stepped from real time and every frame visited in order. A style
// hides its window while it plays (TransitionHidden, apart from what screens
// show) and draws its overlay on top of every layout.
export namespace Engine::UI::WND
{
enum class TransitionStyle : std::uint8_t
{
	Flash,
	ButtonFlash,
	WinFade,
	WinScaleUp,
	MainMenuScaleUp,
	TypeText,
	ScreenFade,
	CountUp,
	FullFade,
	TextOnFrame,
	MainMenuMediumScaleUp,
	MainMenuSmallScaleDown,
	ControlBarArrow,
	ScoreScaleUp,
	ReverseSound,
};

struct TransitionWindowDefinition
{
	std::string window;
	TransitionStyle style{TransitionStyle::Flash};
	int frameDelay{0};
};

struct TransitionGroupDefinition
{
	std::string name;
	bool fireOnce{false};
	std::vector<TransitionWindowDefinition> windows;
};

// A window as the transitions reach it: its layout (its screen scale) and itself.
struct TransitionTarget
{
	WNDWindow *window{nullptr};
	float scaleX{1.0f}, scaleY{1.0f};
};

struct TransitionHost
{
	std::function<TransitionTarget(std::string_view window)> find;
	std::function<void(std::string_view sound)> play;
	std::function<ImageRef(std::string_view image)> image; // "Gradient"
	Graphics::Rect2D screen{0.0f, 0.0f, 800.0f, 600.0f};     // for SCREENFADE
};

namespace detail
{
inline Graphics::Color2D White(int alpha) { return {1.0f, 1.0f, 1.0f, static_cast<float>(std::clamp(alpha, 0, 255)) / 255.0f}; }
inline Graphics::Color2D Black(int alpha) { return {0.0f, 0.0f, 0.0f, static_cast<float>(std::clamp(alpha, 0, 255)) / 255.0f}; }

// One window's transition in a group (a Transition subclass of the original).
class Transition
{
public:
	Transition(const TransitionWindowDefinition &definition, TransitionHost &host) : m_style(definition.style), m_host(&host) { Length(); }

	TransitionStyle Style() const noexcept { return m_style; }
	int FrameLength() const noexcept { return m_length; }
	bool Finished() const noexcept { return m_finished; }

	// init: the window captured and (for most styles) hidden at once, the reverse start's frame 0.
	void Init(const std::string &name)
	{
		m_name = name;
		m_target = m_host->find ? m_host->find(name) : TransitionTarget{};
		m_win = m_target.window;
		m_forward = true;
		m_finished = false;
		m_drawState = -1;
		Capture();
		switch (m_style)
		{
		case TransitionStyle::ReverseSound:
			m_finished = true; // never holds a group back
			return;
		case TransitionStyle::ScreenFade:
		case TransitionStyle::ControlBarArrow:
			m_finished = m_style == TransitionStyle::ControlBarArrow; // in-game only
			return;
		case TransitionStyle::TypeText:
			m_fullText = m_win != nullptr ? m_win->text : std::u16string{};
			m_length = static_cast<int>(std::min<std::size_t>(m_fullText.size(), 30));
			break;
		case TransitionStyle::CountUp:
			InitCountUp();
			if (m_finished)
				return;
			break;
		case TransitionStyle::TextOnFrame:
			if (m_win == nullptr || Hidden(m_win))
			{
				m_finished = true, m_length = 0;
				return;
			}
			break;
		case TransitionStyle::MainMenuScaleUp:
			m_grow = m_host->find ? m_host->find("MainMenu.wnd:WinGrowMarker") : TransitionTarget{};
			if (m_grow.window == nullptr || m_win == nullptr)
				return;
			// The grow marker takes the window's disabled image.
			m_grow.window->draw_states[0].cells[0] = m_win->draw_states[1].cells[0];
			break;
		case TransitionStyle::MainMenuMediumScaleUp:
		case TransitionStyle::MainMenuSmallScaleDown:
			m_grow = m_host->find ? m_host->find(name + (m_style == TransitionStyle::MainMenuMediumScaleUp ? "Medium" : "Small")) : TransitionTarget{};
			if (m_style == TransitionStyle::MainMenuSmallScaleDown && m_grow.window != nullptr && m_win != nullptr)
				m_grow.window->draw_states[0].cells[0] = m_win->draw_states[0].cells[0];
			break;
		default:
			break;
		}
		Update(0, false); // the reverse start: hidden, finished
		m_finished = false;
		m_forward = true;
	}

	void Reverse()
	{
		m_forward = false;
		m_finished = false;
		if (m_style == TransitionStyle::TypeText)
			m_partial = m_fullText;
		if (m_style == TransitionStyle::MainMenuMediumScaleUp)
		{
			Hide(m_win, true);
			Hide(m_grow.window, true);
		}
	}

	// update(frame): the window's frame of its own (0 .. length); the end state for `End`.
	static constexpr int End = 1 << 20;
	void Update(int frame) { Update(frame, m_forward); }

	// skip: the end state in the direction it plays (update(END)).
	void Skip()
	{
		if ((m_style == TransitionStyle::CountUp || m_style == TransitionStyle::ReverseSound) && m_finished)
			return;
		Update(End, m_forward);
	}

	bool Draw(DrawList &list, TextRenderer *) const;

private:
	static bool Hidden(const WNDWindow *window) noexcept { return window != nullptr && Has_Flag(window->flags, WindowFlag::TransitionHidden); }
	static void Hide(WNDWindow *window, bool hidden) noexcept
	{
		if (window == nullptr)
			return;
		if (hidden)
			window->flags |= static_cast<std::uint32_t>(WindowFlag::TransitionHidden);
		else
			window->flags &= ~static_cast<std::uint32_t>(WindowFlag::TransitionHidden);
	}
	void Play(std::string_view sound) const
	{
		if (m_host->play)
			m_host->play(sound);
	}

	void Length()
	{
		switch (m_style)
		{
		case TransitionStyle::Flash: m_length = 8; break;
		case TransitionStyle::ButtonFlash: m_length = 17; break;
		case TransitionStyle::WinFade: m_length = 10; break;
		case TransitionStyle::WinScaleUp:
		case TransitionStyle::ScoreScaleUp: m_length = 6; break;
		case TransitionStyle::MainMenuScaleUp: m_length = 5; break;
		case TransitionStyle::MainMenuMediumScaleUp: m_length = 3; break;
		case TransitionStyle::MainMenuSmallScaleDown: m_length = 6; break;
		case TransitionStyle::TypeText: m_length = 30; break;
		case TransitionStyle::ScreenFade: m_length = 30; break;
		case TransitionStyle::CountUp: m_length = 0; break;
		case TransitionStyle::FullFade: m_length = 10; break;
		case TransitionStyle::TextOnFrame: m_length = 1; break;
		case TransitionStyle::ControlBarArrow: m_length = 22; break;
		case TransitionStyle::ReverseSound: m_length = 2; break;
		}
	}

	Graphics::Rect2D Screen(const TransitionTarget &target) const
	{
		if (target.window == nullptr)
			return {};
		const Rect &r = target.window->screen_region;
		return {r.left * target.scaleX, r.top * target.scaleY, r.right * target.scaleX, r.bottom * target.scaleY};
	}

	void Capture()
	{
		m_rect = Screen(m_target);
		if (m_style == TransitionStyle::ScreenFade)
			m_rect = m_host->screen;
	}

	void InitCountUp()
	{
		if (m_win == nullptr || Has_Flag(m_win->flags, WindowFlag::Hidden) || Hidden(m_win))
		{
			m_finished = true, m_length = 0;
			return;
		}
		m_fullText = m_win->text;
		long long value = 0;
		for (const char16_t c : m_fullText)
		{
			if (c < u'0' || c > u'9')
				break;
			value = value * 10 + (c - u'0');
		}
		m_countTarget = value;
		if (value < 30)
			m_countStep = 1, m_length = static_cast<int>(value);
		else if (value / 100 < 30)
			m_countStep = 100, m_length = static_cast<int>(value / 100);
		else
			m_countStep = 1000, m_length = static_cast<int>(std::min<long long>(value / 1000, 30));
		m_countValue = 0;
		m_win->text = u"0";
	}

	void Update(int frame, bool forward);

	TransitionStyle m_style;
	TransitionHost *m_host;
	std::string m_name;
	TransitionTarget m_target{}, m_grow{};
	WNDWindow *m_win{nullptr};
	Graphics::Rect2D m_rect{};
	int m_length{0};
	int m_drawState{-1};
	bool m_forward{true};
	bool m_finished{false};
	std::u16string m_fullText, m_partial;
	long long m_countTarget{0}, m_countValue{0}, m_countStep{1};
	int m_frame{0};
};

inline void Transition::Update(int frame, bool forward)
{
	m_frame = frame;
	const bool end = frame >= End;
	const auto finish = [this] { m_finished = true; };
	switch (m_style)
	{
	case TransitionStyle::Flash:
	{
		if (end)
		{
			if (forward)
				Hide(m_win, false), m_drawState = -1, finish();
			return;
		}
		if (frame <= 0 && !forward)
		{
			Hide(m_win, true), m_drawState = -1, finish();
			return;
		}
		if (frame >= 8)
		{
			if (forward)
				Hide(m_win, false), m_drawState = -1, finish();
			return;
		}
		if (frame == 1 && forward)
			Play("GUIBoarderFadeIn");
		Hide(m_win, frame <= 3);
		m_drawState = frame;
		return;
	}
	case TransitionStyle::ButtonFlash:
	{
		if (end)
		{
			if (forward)
				Hide(m_win, false), m_drawState = -1, finish();
			return;
		}
		if (frame <= 0 && !forward)
		{
			Hide(m_win, true), m_drawState = -1, finish();
			return;
		}
		if (frame >= 17)
		{
			if (forward)
				Hide(m_win, false), m_drawState = -1, finish();
			return;
		}
		if (forward)
		{
			if (frame == 1)
				Play("GUIButtonsFadeIn");
			Hide(m_win, frame <= 10);
			m_drawState = frame;
		}
		else
		{
			// Played back: the gradient over the shown button (16, 15), then hidden with its images (14 .. 8),
			// then the flashes fading (7 .. 1 are states 1 .. 7).
			Hide(m_win, frame <= 14);
			m_drawState = frame >= 11 ? 100 + (27 - frame) : frame >= 8 ? 10 : 200 + (8 - frame);
		}
		return;
	}
	case TransitionStyle::WinFade:
	case TransitionStyle::WinScaleUp:
	case TransitionStyle::ScoreScaleUp:
	{
		const int length = m_style == TransitionStyle::WinFade ? 10 : 6;
		if (end || frame >= length)
		{
			if (forward || end)
			{
				if (forward)
					Hide(m_win, false), m_drawState = -1, finish();
			}
			return;
		}
		if (frame <= 0)
		{
			if (!forward)
				Hide(m_win, true), m_drawState = -1, finish();
			return;
		}
		if (frame == 1 && forward && m_style != TransitionStyle::WinFade)
			Play(m_style == TransitionStyle::WinScaleUp ? "GUILogoMouseOver" : "GUIScoreScreenPictures");
		Hide(m_win, true);
		m_drawState = frame;
		return;
	}
	case TransitionStyle::MainMenuScaleUp:
	{
		if (m_grow.window == nullptr)
		{
			finish();
			return;
		}
		if (end || frame >= 5)
		{
			if (forward)
				Hide(m_win, true), Hide(m_grow.window, false), m_drawState = -1, finish();
			return;
		}
		if (frame <= 0)
		{
			if (!forward)
				Hide(m_grow.window, true), m_drawState = -1, finish();
			else
				Hide(m_grow.window, true);
			return;
		}
		if (frame == 1)
			Play("GUILogoSelect");
		Hide(m_win, true), Hide(m_grow.window, true);
		m_drawState = frame;
		return;
	}
	case TransitionStyle::MainMenuMediumScaleUp:
	case TransitionStyle::MainMenuSmallScaleDown:
	{
		const int length = m_style == TransitionStyle::MainMenuMediumScaleUp ? 3 : 6;
		if (end || frame >= length)
		{
			if (forward)
				Hide(m_win, true), Hide(m_grow.window, false), m_drawState = -1, finish();
			return;
		}
		if (frame <= 0)
		{
			Hide(m_win, false), Hide(m_grow.window, true), m_drawState = -1;
			if (!forward)
				finish();
			return;
		}
		if (frame == 1 && forward && m_style == TransitionStyle::MainMenuMediumScaleUp)
			Play("GUILogoMouseOver");
		Hide(m_win, true), Hide(m_grow.window, true);
		m_drawState = frame;
		return;
	}
	case TransitionStyle::TypeText:
	{
		if (end)
		{
			if (forward)
				Hide(m_win, false), m_drawState = -1, finish();
			return;
		}
		if (frame <= 0)
		{
			m_partial.clear();
			Hide(m_win, true);
			m_drawState = -1;
			if (!forward)
				finish();
			return;
		}
		if (frame >= m_length)
		{
			Hide(m_win, false);
			m_drawState = -1;
			if (frame >= 30 && forward)
				finish();
			return;
		}
		Hide(m_win, true);
		Play("GUITypeText");
		if (forward)
			m_partial = m_fullText.substr(0, static_cast<std::size_t>(frame));
		else if (!m_partial.empty())
			m_partial.pop_back();
		m_drawState = frame;
		return;
	}
	case TransitionStyle::CountUp:
	{
		if (m_win == nullptr)
		{
			finish();
			return;
		}
		if (end || frame >= m_length)
		{
			m_win->text = m_fullText;
			Hide(m_win, false);
			finish();
			return;
		}
		if (frame <= 0)
		{
			m_win->text = u"0";
			Hide(m_win, true);
			return;
		}
		Hide(m_win, false);
		Play("GUIScoreScreenTick");
		m_countValue = std::min(m_countValue + m_countStep, m_countTarget);
		const std::string shown = std::to_string(m_countValue);
		m_win->text = std::u16string(shown.begin(), shown.end());
		return;
	}
	case TransitionStyle::TextOnFrame:
	{
		if (end || frame >= 1)
		{
			if (forward)
				Hide(m_win, false), finish();
			return;
		}
		Hide(m_win, true);
		if (!forward)
			finish();
		return;
	}
	case TransitionStyle::ReverseSound:
	{
		if (end)
			return;
		if (frame == 1)
			Play("GUITransitionFade");
		if (frame <= 0 && !forward)
			finish();
		return;
	}
	case TransitionStyle::FullFade:
	{
		if (end)
		{
			if (forward)
				Hide(m_win, false), m_drawState = -1, finish();
			return;
		}
		m_drawState = frame;
		if (frame == 5)
			Hide(m_win, !forward);
		if (frame <= 0)
		{
			Hide(m_win, true);
			m_drawState = -1;
			if (!forward)
				finish();
		}
		if (frame >= 10 && forward)
		{
			Hide(m_win, false);
			m_drawState = -1;
			finish();
		}
		return;
	}
	case TransitionStyle::ScreenFade:
	{
		m_drawState = end ? (forward ? 30 : 0) : frame;
		if (m_drawState <= 0 || m_drawState >= 30)
			finish();
		return;
	}
	case TransitionStyle::ControlBarArrow:
		finish();
		return;
	}
}

inline bool Transition::Draw(DrawList &list, TextRenderer *text) const
{
	if (m_drawState < 0)
		return true;
	const Graphics::Rect2D r = m_rect;
	const float w = r.right - r.left, h = r.bottom - r.top;
	const auto rect = [&](Graphics::Rect2D area, Graphics::Color2D color) { return list.Add_Rect(area, color); };
	const auto outline = [&](Graphics::Rect2D area, Graphics::Color2D color) { return list.Add_Outline(area, 1.0f, color); };
	const auto image = [&](const ImageRef &picture, Graphics::Rect2D area, Graphics::Color2D tint = {}) {
		return !picture.texture.Is_Valid() || list.Add_Image(picture, area, tint);
	};
	const ImageRef enabledImage = m_win != nullptr ? m_win->draw_states[0].cells[0].image : ImageRef{};
	switch (m_style)
	{
	case TransitionStyle::Flash:
	{
		// (pos.x+1, pos.y+1, size.x-2, size.y): an outline and a fill, fading.
		static constexpr int Outline[] = {0, 100, 150, 200, 250, 250, 250, 250};
		static constexpr int Fill[] = {0, 33, 66, 99, 75, 50, 25, 10};
		if (m_drawState < 1 || m_drawState > 7)
			return true;
		const Graphics::Rect2D area{r.left + 1.0f, r.top + 1.0f, r.left + 1.0f + (w - 2.0f), r.top + 1.0f + h};
		return outline(area, White(Outline[m_drawState])) && rect(area, White(Fill[m_drawState]));
	}
	case TransitionStyle::ButtonFlash:
	{
		// The button's enabled left, middle (tiled) and right images, as PushButtonImageDrawThree.
		const auto images = [&]() {
			if (m_win == nullptr)
				return true;
			const WNDDrawState &state = m_win->draw_states[0];
			const WNDDrawCell &left = state.cells[0], &middle = state.cells[5], &right = state.cells[6];
			if (!left.image.texture.Is_Valid())
				return true;
			const float lw = static_cast<float>(left.image_width) * m_target.scaleX, rw = static_cast<float>(right.image_width) * m_target.scaleX;
			return image(left.image, {r.left, r.top, r.left + lw, r.bottom}) && image(middle.image, {r.left + lw, r.top, r.right - rw, r.bottom})
				&& image(right.image, {r.right - rw, r.top, r.right, r.bottom});
		};
		const auto gradient = [&](int alpha) { return image(m_host->image ? m_host->image("Gradient") : ImageRef{}, r, White(alpha)); };
		static constexpr int Gradient[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 200, 150, 100, 50, 17};
		const int state = m_drawState;
		if (state >= 100 && state < 200)
		{
			// Played back from forward frame g: shown with the gradient alone (g 11, 12), else hidden with its images too.
			const int g = state - 100;
			return (g <= 12 || images()) && gradient(Gradient[g]);
		}
		if (state >= 200)
		{
			// Played back states 1 .. 7: 1-3 the flashes, 4 the images, outline and fill, 5-7 the images fading.
			const int f = state - 200;
			static constexpr int Outline[] = {0, 100, 150, 200, 250, 0, 0, 0};
			static constexpr int Fill[] = {0, 75, 150, 200, 150, 100, 50, 15};
			if (f >= 4 && !images())
				return false;
			if (Outline[f] > 0 && !outline(r, White(Outline[f])))
				return false;
			return rect(r, White(Fill[f]));
		}
		if (state >= 1 && state <= 3)
		{
			static constexpr int Outline[] = {0, 100, 150, 200};
			static constexpr int Fill[] = {0, 75, 150, 200};
			return outline(r, White(Outline[state])) && rect(r, White(Fill[state]));
		}
		if (state >= 4 && state <= 7)
		{
			static constexpr int Fill[] = {0, 0, 0, 0, 150, 100, 50, 15};
			return images() && outline(r, White(250)) && rect(r, White(Fill[state]));
		}
		if (state >= 8 && state <= 10)
			return images();
		if (state >= 11 && state <= 16)
			return (state > 11 || images()) && gradient(Gradient[state]);
		return true;
	}
	case TransitionStyle::WinFade:
		return image(enabledImage, r, White(25 * m_drawState));
	case TransitionStyle::WinScaleUp:
	case TransitionStyle::ScoreScaleUp:
	{
		// Grows from the centre: inc = size / 6 (whole pixels), inc * frame wide and high.
		const float incX = std::floor(w / 6.0f), incY = std::floor(h / 6.0f);
		const float cx = r.left + std::floor(w / 2.0f), cy = r.top + std::floor(h / 2.0f);
		const float sx = incX * static_cast<float>(m_drawState), sy = incY * static_cast<float>(m_drawState);
		return image(enabledImage, {cx - sx / 2.0f, cy - sy / 2.0f, cx - sx / 2.0f + sx, cy - sy / 2.0f + sy});
	}
	case TransitionStyle::MainMenuScaleUp:
	{
		if (m_grow.window == nullptr)
			return true;
		const Graphics::Rect2D g = Screen(m_grow);
		const float f = static_cast<float>(m_drawState);
		const float incX = std::trunc((g.left - r.left) / 5.0f), incY = std::trunc((g.top - r.top) / 5.0f);
		const float incW = std::trunc(((g.right - g.left) - w) / 5.0f), incH = std::trunc(((g.bottom - g.top) - h) / 5.0f);
		const float x = r.left + incX * f, y = r.top + incY * f;
		return image(m_grow.window->draw_states[0].cells[0].image, {x, y, x + w + incW * f, y + h + incH * f});
	}
	case TransitionStyle::MainMenuMediumScaleUp:
	case TransitionStyle::MainMenuSmallScaleDown:
	{
		if (m_grow.window == nullptr)
			return true;
		const Graphics::Rect2D g = Screen(m_grow);
		const float parts = m_style == TransitionStyle::MainMenuMediumScaleUp ? 3.0f : 6.0f;
		const float incW = std::trunc(((g.right - g.left) - w) / parts), incH = std::trunc(((g.bottom - g.top) - h) / parts);
		const float f = static_cast<float>(m_drawState);
		return image(enabledImage, {r.left - incW * f / 2.0f, r.top - incH * f / 2.0f, r.right + incW * f / 2.0f, r.bottom + incH * f / 2.0f});
	}
	case TransitionStyle::TypeText:
	{
		if (m_win == nullptr || m_win->font == nullptr || m_partial.empty() || text == nullptr)
			return true;
		TextLayoutOptions options;
		options.wrapping_width = std::max(1, static_cast<int>(w - 10.0f));
		options.centered = m_win->wrap_centered;
		std::uint32_t fullWidth = 0, fullHeight = 0;
		const std::uint16_t *full = list.Keep_Text(m_fullText);
		const std::uint16_t *partial = list.Keep_Text(m_partial);
		if (full == nullptr || partial == nullptr || !text->Measure(*m_win->font, full, options, fullWidth, fullHeight))
			return true;
		const float x = m_win->centered_text ? r.left + (w - static_cast<float>(fullWidth)) / 2.0f : r.left + 7.0f;
		const float y = r.top + (h - static_cast<float>(fullHeight)) / 2.0f;
		TextStyle style = m_win->text_styles[0];
		return list.Add_Text(m_win->font, nullptr, partial, x, y, options, style, true, r);
	}
	case TransitionStyle::FullFade:
	{
		// A black box fades in, the window appears under it at frame 5, the box fades away.
		const int f = m_drawState;
		const int alpha = static_cast<int>(0.2f * 255.0f * static_cast<float>(f <= 5 ? f : 10 - f));
		return rect(r, Black(alpha)) && outline(r, {60.0f / 255.0f, 60.0f / 255.0f, 180.0f / 255.0f, static_cast<float>(std::clamp(alpha, 0, 255)) / 255.0f});
	}
	case TransitionStyle::ScreenFade:
		return rect(m_host->screen, Black(std::min(255, static_cast<int>(255.0f * static_cast<float>(m_drawState) / 29.0f))));
	default:
		return true;
	}
}

// A group playing (TransitionGroup): its transitions, its frame, its direction.
class Group
{
public:
	Group(const TransitionGroupDefinition &definition, TransitionHost &host) : m_definition(&definition), m_host(&host)
	{
		for (const TransitionWindowDefinition &window : definition.windows)
			m_windows.push_back({window, Transition(window, host)});
	}
	const std::string &Name() const noexcept { return m_definition->name; }
	bool FireOnce() const noexcept { return m_definition->fireOnce; }
	bool Reversed() const noexcept { return m_direction < 0; }

	void Init()
	{
		m_frame = 0.0;
		m_direction = 1;
		for (auto &[definition, transition] : m_windows)
		{
			transition = Transition(definition, *m_host);
			transition.Init(definition.window);
		}
	}
	void Reverse()
	{
		m_direction = -1;
		m_frame = static_cast<double>(TotalLength());
		for (auto &[definition, transition] : m_windows)
			transition.Reverse();
	}
	void Skip()
	{
		for (auto &[definition, transition] : m_windows)
			transition.Skip();
	}
	bool Finished() const
	{
		for (const auto &[definition, transition] : m_windows)
			if (!transition.Finished())
				return false;
		return true;
	}
	int TotalLength() const
	{
		int total = 0;
		for (const auto &[definition, transition] : m_windows)
			total = std::max(total, definition.frameDelay + transition.FrameLength());
		return total;
	}
	// TransitionGroup::update: every whole frame between the old and the new, in order.
	void Update(double frames)
	{
		const int from = static_cast<int>(std::floor(m_frame));
		m_frame += static_cast<double>(m_direction) * frames;
		const int to = static_cast<int>(std::floor(m_frame));
		for (int frame = from + m_direction; m_direction > 0 ? frame <= to : frame >= to; frame += m_direction)
		{
			for (auto &[definition, transition] : m_windows)
			{
				const int local = frame - definition.frameDelay;
				if (local >= 0 && local <= transition.FrameLength())
					transition.Update(local);
			}
			if (Finished())
				break;
		}
	}
	bool Draw(DrawList &list, TextRenderer *text) const
	{
		for (const auto &[definition, transition] : m_windows)
			if (!transition.Draw(list, text))
				return false;
		return true;
	}

private:
	const TransitionGroupDefinition *m_definition;
	TransitionHost *m_host;
	std::vector<std::pair<TransitionWindowDefinition, Transition>> m_windows;
	double m_frame{0.0};
	int m_direction{1};
};
}

// The handler (GameWindowTransitionsHandler).
class WNDTransitions
{
public:
	WNDTransitions(std::vector<TransitionGroupDefinition> groups, TransitionHost host) : m_definitions(std::move(groups)), m_host(std::move(host))
	{
		m_groups.reserve(m_definitions.size());
		for (const TransitionGroupDefinition &definition : m_definitions)
			m_groups.emplace_back(definition, m_host);
	}
	WNDTransitions(const WNDTransitions &) = delete;
	WNDTransitions &operator=(const WNDTransitions &) = delete;

	// setGroup: `immediate` ends what plays at once; otherwise what plays goes back (unless it plays
	// once or already goes back) and this one waits behind it, its windows hidden now.
	void SetGroup(std::string_view name, bool immediate = false)
	{
		if (name.empty() && immediate)
		{
			m_current = nullptr;
			return;
		}
		detail::Group *group = Find(name);
		if (immediate && m_current != nullptr)
		{
			m_current->Skip();
			m_current = group;
			if (m_current != nullptr)
				m_current->Init();
			return;
		}
		if (m_current != nullptr)
		{
			if (!m_current->FireOnce() && !m_current->Reversed())
				m_current->Reverse();
			m_pending = group;
			if (m_pending != nullptr)
				m_pending->Init();
			return;
		}
		m_current = group;
		if (m_current != nullptr)
			m_current->Init();
	}

	// reverse: what plays goes back; a waiting one is dropped; another plays out from fully shown.
	void Reverse(std::string_view name)
	{
		detail::Group *group = Find(name);
		if (group == nullptr)
			return;
		if (group == m_current)
		{
			m_current->Reverse();
			return;
		}
		if (group == m_pending)
		{
			m_pending = nullptr;
			return;
		}
		if (m_current != nullptr)
			m_current->Skip();
		if (m_pending != nullptr)
			m_pending->Skip();
		m_current = group;
		group->Init();
		group->Skip();
		group->Reverse();
		m_pending = nullptr;
	}

	// remove: ended at once (the waiting one only if asked); what waits then plays.
	void Remove(std::string_view name, bool skipPending = false)
	{
		detail::Group *group = Find(name);
		if (group == nullptr)
			return;
		if (group == m_pending)
		{
			if (skipPending)
				m_pending->Skip();
			m_pending = nullptr;
		}
		if (group == m_current)
		{
			m_current->Skip();
			m_current = m_pending;
			m_pending = nullptr;
		}
	}

	bool IsFinished() const { return m_current == nullptr || m_current->Finished(); }
	// Something draws or moves (the layouts need rebuilding as it plays).
	bool Active() const { return m_current != nullptr || m_pending != nullptr || m_draw != nullptr || m_secondary != nullptr; }

	// GameWindowTransitionsHandler::update, `frames` of the original's 30 a second.
	void Update(double frames)
	{
		m_secondary = m_draw != m_current ? m_draw : nullptr;
		m_draw = m_current;
		if (m_current != nullptr && !m_current->Finished())
			m_current->Update(frames);
		if (m_current != nullptr && m_current->Finished() && m_current->FireOnce())
			m_current = nullptr;
		if (m_current != nullptr && m_pending != nullptr && m_current->Finished())
			m_current = std::exchange(m_pending, nullptr);
		if (m_current == nullptr && m_pending != nullptr)
			m_current = std::exchange(m_pending, nullptr);
		if (m_current != nullptr && m_current->Finished() && m_current->Reversed())
			m_current = nullptr;
	}

	// The overlays, over every layout.
	bool Draw(DrawList &list) const
	{
		TextRenderer *text = &Get_Text_Renderer();
		if (m_draw != nullptr && !m_draw->Draw(list, text))
			return false;
		return m_secondary == nullptr || m_secondary->Draw(list, text);
	}

private:
	detail::Group *Find(std::string_view name)
	{
		const auto folded = [](std::string_view text) {
			std::string out(text);
			for (char &c : out)
				if (c >= 'A' && c <= 'Z')
					c = static_cast<char>(c - 'A' + 'a');
			return out;
		};
		const std::string key = folded(name);
		for (detail::Group &group : m_groups)
			if (folded(group.Name()) == key)
				return &group;
		return nullptr;
	}

	std::vector<TransitionGroupDefinition> m_definitions;
	TransitionHost m_host;
	std::vector<detail::Group> m_groups;
	detail::Group *m_current{nullptr};
	detail::Group *m_pending{nullptr};
	detail::Group *m_draw{nullptr};
	detail::Group *m_secondary{nullptr};
};
}
