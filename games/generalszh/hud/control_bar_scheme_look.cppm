export module games.generalszh.hud.control_bar_scheme_look;
import std;

export import games.generalszh.content.control_bar.control_bar_scheme;

// The control bar's per-side look (the original's ControlBarSchemeManager / ControlBarScheme): which scheme a side gets,
// where its init places the bar's windows and which art it gives their buttons, the scheme's images drawn behind and in
// front of the bar (W3DCommandBarBackgroundDraw / ForegroundDraw), and its AnimatingParts sliding on each update.
// Headless: the host applies the windows to its layout and draws the art.
export namespace generalszh::hud
{
// A window ControlBarScheme::init sets up: placed (winSetPosition relative to its parent's screen position, winSetSize,
// in display pixels) and, for a button, its art (GadgetButtonSetEnabledImage, SetHiliteImage, SetHiliteSelectedImage,
// SetDisabledImage; an image the scheme does not name sets none).
struct SchemeWindowLook
{
	std::string window;
	bool placed{true};
	int x{0}, y{0};          // relative to its parent's screen position
	int screenX{0}, screenY{0}; // where that puts it on the screen
	int width{0}, height{0};
	bool buttonImages{false}; // the four push button images
	bool cellImages{false};   // winSetEnabledImage(0) / winSetDisabledImage(0) only (WinUAttack, ExpBarForeground)
	std::string enabled, hilite, hiliteSelected, disabled;
};

namespace scheme_look_detail
{
struct Placement
{
	const char *window;
	const char *place;     // <place>UL / <place>LR; none: not placed
	const char *enabled;
	const char *hilite;
	const char *pushed;
	const char *disabled;
	bool button;
};

// ControlBarScheme::init's windows, in its order.
inline constexpr std::array<Placement, 10> Placements{{
	{"ControlBar.wnd:PopupCommunicator", "Chat", "BuddyButtonEnable", "BuddyButtonHightlited", "BuddyButtonPushed", "BuddyButtonDisabled", true},
	{"ControlBar.wnd:ButtonIdleWorker", "Worker", "IdleWorkerButtonEnable", "IdleWorkerButtonHightlited", "IdleWorkerButtonPushed",
		"IdleWorkerButtonDisabled", true},
	{"ControlBar.wnd:ExpBarForeground", nullptr, "ExpBarForegroundImage", nullptr, nullptr, nullptr, false},
	{"ControlBar.wnd:ButtonOptions", "Options", "OptionsButtonEnable", "OptionsButtonHightlited", "OptionsButtonPushed", "OptionsButtonDisabled", true},
	{"ControlBar.wnd:ButtonPlaceBeacon", "Beacon", "BeaconButtonEnable", "BeaconButtonHightlited", "BeaconButtonPushed", "BeaconButtonDisabled", true},
	{"ControlBar.wnd:MoneyDisplay", "Money", nullptr, nullptr, nullptr, nullptr, false},
	{"ControlBar.wnd:PowerWindow", "PowerBar", nullptr, nullptr, nullptr, nullptr, false},
	{"ControlBar.wnd:ButtonGeneral", "General", "GeneralButtonEnable", "GeneralButtonHightlited", "GeneralButtonPushed", "GeneralButtonDisabled", true},
	// ButtonLarge: placed only (its images are the bar stage's: setUpDownImages).
	{"ControlBar.wnd:ButtonLarge", "MinMax", nullptr, nullptr, nullptr, nullptr, false},
	// WinUAttack: its enabled image UAttackButtonEnable, its disabled image UAttackButtonHightlited.
	{"ControlBar.wnd:WinUAttack", "UAttack", "UAttackButtonEnable", nullptr, nullptr, "UAttackButtonHightlited", false},
}};

inline std::string ImageOf(const content::ControlBarSchemeContent &scheme, const char *key)
{
	return key == nullptr ? std::string{} : std::string(scheme.Image(key));
}
}

// ControlBarScheme::init on a display `displayWidth` x `displayHeight`: resMultiplier = display / ScreenCreationRes (as
// Real); each window moved to UL * resMultiplier - its parent's screen position and sized (LR - UL) * resMultiplier +
// COMMAND_BAR_SIZE_OFFSET (0), each truncated to whole pixels as winSetPosition / winSetSize take them. `parentScreen`:
// the window's parent's screen position (none: no parent). A window the layout lacks is left out (`hasWindow`).
inline std::vector<SchemeWindowLook> SchemeWindows(const content::ControlBarSchemeContent &scheme, int displayWidth, int displayHeight,
	const std::function<bool(std::string_view)> &hasWindow, const std::function<std::optional<std::pair<int, int>>(std::string_view)> &parentScreen)
{
	using scheme_look_detail::ImageOf;
	const float multiplierX = static_cast<float>(displayWidth) / static_cast<float>(scheme.width);
	const float multiplierY = static_cast<float>(displayHeight) / static_cast<float>(scheme.height);
	std::vector<SchemeWindowLook> windows;
	for (const scheme_look_detail::Placement &placement : scheme_look_detail::Placements)
	{
		if (hasWindow && !hasWindow(placement.window))
			continue;
		SchemeWindowLook look;
		look.window = placement.window;
		look.buttonImages = placement.button;
		look.cellImages = !placement.button && placement.enabled != nullptr;
		look.enabled = ImageOf(scheme, placement.enabled);
		look.hilite = ImageOf(scheme, placement.hilite);
		look.hiliteSelected = ImageOf(scheme, placement.pushed);
		look.disabled = ImageOf(scheme, placement.disabled);
		look.placed = placement.place != nullptr;
		if (look.placed)
		{
			const auto found = scheme.places.find(placement.place);
			const content::SchemeRect rect = found != scheme.places.end() ? found->second : content::SchemeRect{};
			const auto parent = parentScreen ? parentScreen(placement.window) : std::nullopt;
			const int parentX = parent ? parent->first : 0, parentY = parent ? parent->second : 0;
			look.x = static_cast<int>(static_cast<float>(rect.left) * multiplierX - static_cast<float>(parentX));
			look.y = static_cast<int>(static_cast<float>(rect.top) * multiplierY - static_cast<float>(parentY));
			look.screenX = look.x + parentX;
			look.screenY = look.y + parentY;
			look.width = static_cast<int>(static_cast<float>(rect.right - rect.left) * multiplierX);
			look.height = static_cast<int>(static_cast<float>(rect.bottom - rect.top) * multiplierY);
		}
		windows.push_back(std::move(look));
	}
	return windows;
}

// An AnimatingPart's state (ControlBarSchemeAnimation's current frame and start position) and where its image is now.
struct SchemeAnimationState
{
	std::uint32_t frame{0};
	int startX{0}, startY{0};
	int x{0}, y{0};
};

// The states of a scheme's animations, each at its image part's own position.
inline std::vector<SchemeAnimationState> StartSchemeAnimations(const content::ControlBarSchemeContent &scheme)
{
	std::vector<SchemeAnimationState> states(scheme.animations.size());
	for (std::size_t index = 0; index < scheme.animations.size(); ++index)
		if (scheme.animations[index].hasImage && scheme.animations[index].image < scheme.images.size())
		{
			const content::SchemeImagePart &part = scheme.images[scheme.animations[index].image];
			states[index].x = part.x;
			states[index].y = part.y;
		}
	return states;
}

// animSlideRight, once per ControlBarScheme::update: nothing without an image or a duration; at the last frame the image
// goes back to where it started and the frame to 0; at frame 0 its position is taken as the start; then the frame goes
// on by one and the image's x is start + (FinalPos.x - start) * frame / Duration (in the original's unsigned arithmetic).
inline void SlideRight(SchemeAnimationState &state, const content::SchemeAnimation &animation)
{
	if (!animation.hasImage || animation.durationFrames == 0)
		return;
	if (state.frame == animation.durationFrames)
	{
		state.x = state.startX;
		state.y = state.startY;
		state.frame = 0;
		return;
	}
	if (state.frame == 0)
	{
		state.startX = state.x;
		state.startY = state.y;
	}
	++state.frame;
	const std::uint32_t moved = static_cast<std::uint32_t>(animation.finalX - state.startX) * state.frame / animation.durationFrames;
	state.x = static_cast<int>(static_cast<std::uint32_t>(state.startX) + moved);
}

// ControlBarScheme::update: each animation by its kind (SLIDE_RIGHT; another kind does nothing).
inline void StepSchemeAnimations(const content::ControlBarSchemeContent &scheme, std::vector<SchemeAnimationState> &states)
{
	for (std::size_t index = 0; index < scheme.animations.size() && index < states.size(); ++index)
		if (scheme.animations[index].type == content::SchemeAnimation::SlideRight)
			SlideRight(states[index], scheme.animations[index]);
}

// An image of the scheme as drawn, in display pixels.
struct SchemeArtQuad
{
	std::string image;
	int left{0}, top{0}, right{0}, bottom{0};
	bool operator==(const SchemeArtQuad &) const = default;
};

// ControlBarScheme::drawBackground (layers 5, 4, 3) or drawForeground (2, 1, 0), each layer's images in the order read;
// one without an image is skipped. Each drawn at position * multiplier + offset to (position + size) * multiplier +
// offset, multiplier = display / ScreenCreationRes, truncated to whole pixels (drawImage's Int corners). An animated
// image is where its animation has it (`states`).
inline std::vector<SchemeArtQuad> SchemeArt(const content::ControlBarSchemeContent &scheme, std::span<const SchemeAnimationState> states,
	int displayWidth, int displayHeight, int offsetX, int offsetY, bool foreground)
{
	const float multiplierX = static_cast<float>(displayWidth) / static_cast<float>(scheme.width);
	const float multiplierY = static_cast<float>(displayHeight) / static_cast<float>(scheme.height);
	std::vector<std::pair<int, int>> positions;
	positions.reserve(scheme.images.size());
	for (const content::SchemeImagePart &part : scheme.images)
		positions.emplace_back(part.x, part.y);
	for (std::size_t index = 0; index < scheme.animations.size() && index < states.size(); ++index)
		if (scheme.animations[index].hasImage && scheme.animations[index].image < positions.size())
			positions[scheme.animations[index].image] = {states[index].x, states[index].y};
	std::vector<SchemeArtQuad> quads;
	const int first = foreground ? 2 : 5, last = foreground ? 0 : 3;
	for (int layer = first; layer >= last; --layer)
		for (std::size_t index = 0; index < scheme.images.size(); ++index)
		{
			const content::SchemeImagePart &part = scheme.images[index];
			if (part.layer != layer || part.image.empty())
				continue;
			const auto [x, y] = positions[index];
			quads.push_back({part.image, static_cast<int>(static_cast<float>(x) * multiplierX + static_cast<float>(offsetX)),
				static_cast<int>(static_cast<float>(y) * multiplierY + static_cast<float>(offsetY)),
				static_cast<int>(static_cast<float>(x + part.width) * multiplierX + static_cast<float>(offsetX)),
				static_cast<int>(static_cast<float>(y + part.height) * multiplierY + static_cast<float>(offsetY))});
		}
	return quads;
}

// The scheme in use and its animations: ControlBarSchemeManager's current scheme, chosen by side
// (setControlBarSchemeByPlayer), updated with the control bar (ControlBar::update, once a frame: here every 1/30 s of real
// time, the original's frame).
class ControlBarSchemeLook
{
public:
	explicit ControlBarSchemeLook(const content::ControlBarSchemes &schemes) : m_schemes(&schemes), m_states(schemes.schemes.size()) {}

	// setControlBarSchemeByPlayer(side): the current scheme kept when its side is the same (it is set up again); else the
	// side's scheme laid out at the largest resolution, "Observer"'s for no side, "Default" when the side has none.
	// Returns the scheme (none: none at all) to set up (ControlBarScheme::init).
	const content::ControlBarSchemeContent *Select(std::string_view side)
	{
		if (m_current == nullptr || m_current->side != side)
			m_current = m_schemes->ForSide(side);
		if (m_current != nullptr)
		{
			auto &states = StatesOf(*m_current);
			if (states.size() != m_current->animations.size())
				states = StartSchemeAnimations(*m_current);
		}
		return m_current;
	}

	const content::ControlBarSchemeContent *Current() const noexcept { return m_current; }

	// ControlBarSchemeManager::update on `seconds` more of real time: one update per 1/30 s.
	void Update(double seconds)
	{
		if (m_current == nullptr)
			return;
		m_pending += (std::max)(seconds, 0.0);
		auto &states = StatesOf(*m_current);
		while (m_pending >= FrameSeconds)
		{
			m_pending -= FrameSeconds;
			StepSchemeAnimations(*m_current, states);
		}
	}

	// drawBackground / drawForeground for a display of that size, moved by `offset` (the marker windows' movement).
	std::vector<SchemeArtQuad> Art(bool foreground, int displayWidth, int displayHeight, int offsetX, int offsetY)
	{
		if (m_current == nullptr)
			return {};
		return SchemeArt(*m_current, StatesOf(*m_current), displayWidth, displayHeight, offsetX, offsetY, foreground);
	}

	std::span<const SchemeAnimationState> States()
	{
		if (m_current == nullptr)
			return {};
		return StatesOf(*m_current);
	}

private:
	static constexpr double FrameSeconds = 1.0 / 30.0;

	std::vector<SchemeAnimationState> &StatesOf(const content::ControlBarSchemeContent &scheme)
	{
		const auto index = static_cast<std::size_t>(&scheme - m_schemes->schemes.data());
		if (m_states.size() < m_schemes->schemes.size())
			m_states.resize(m_schemes->schemes.size());
		return m_states[index];
	}

	const content::ControlBarSchemes *m_schemes;
	const content::ControlBarSchemeContent *m_current{nullptr};
	// Each scheme's animation states (the original keeps them on the scheme: they carry on when a side comes back).
	std::vector<std::vector<SchemeAnimationState>> m_states;
	double m_pending{0.0};
};
}
