export module games.generalszh.content.control_bar.control_bar_scheme;
import std;

export import engine.config.binding.schema;

// A side's control bar art (Data/INI/ControlBarScheme.ini, the original's ControlBarScheme): the resolution it was laid
// out at, its images drawn behind the control bar's windows (ImagePart: where, how big, which mapped image, on which
// layer: 0 on top to 5 at the back), and the places of its money and power readouts. A player's side picks its scheme
// (ControlBarSchemeManager::setControlBarSchemeByPlayer: the side's scheme laid out at the largest resolution; none:
// "Default"; no side: "Observer").
export namespace generalszh::content
{
struct SchemeImagePart
{
	int x{0}, y{0}, width{0}, height{0};
	std::string image;
	int layer{0};
};

struct SchemeRect
{
	int left{0}, top{0}, right{0}, bottom{0};
};

// An AnimatingPart (ControlBarSchemeAnimation): its image part (also drawn on its layer like any other), how it moves
// (Animation: SLIDE_RIGHT, the only kind; another: none), in how many frames (Duration: milliseconds rounded up to
// logic frames, parseDurationUnsignedInt) and to where (FinalPos).
struct SchemeAnimation
{
	static constexpr int SlideRight = 0;
	std::string name;
	int type{-1};
	std::uint32_t durationFrames{0};
	int finalX{0}, finalY{0};
	std::size_t image{0}; // its image part, in `images`
	bool hasImage{false};
};

struct ControlBarSchemeContent
{
	std::string name;
	std::string side;
	int width{800}, height{600}; // ScreenCreationRes
	std::vector<SchemeImagePart> images;
	std::vector<SchemeAnimation> animations;
	std::string queueButtonImage;
	// BuildUpClockColor (R G B A, 0..255): the production queue's build clock.
	std::array<int, 4> buildUpClockColor{0, 0, 0, 100};
	// ButtonBorderBuildColor / UpgradeColor / ActionColor / SystemColor and CommandBarBorderColor (INI::parseColorInt,
	// A 255 when left out), 0xAARRGGBB; 0: not given (GAME_COLOR_UNDEFINED).
	std::uint32_t borderBuild{0}, borderUpgrade{0}, borderAction{0}, borderSystem{0}, commandBarBorder{0};
	SchemeRect money;
	SchemeRect powerBar;
	// Every place it names (MoneyUL / MoneyLR -> "Money", ChatUL / ChatLR -> "Chat", ...) and every image it names by
	// key (IdleWorkerButtonEnable -> SUWorkerE, ...).
	std::map<std::string, SchemeRect, std::less<>> places;
	std::map<std::string, std::string, std::less<>> namedImages;

	const SchemeRect *Place(std::string_view name) const
	{
		const auto found = places.find(name);
		return found == places.end() || found->second.right <= found->second.left ? nullptr : &found->second;
	}
	std::string_view Image(std::string_view key) const
	{
		const auto found = namedImages.find(key);
		return found == namedImages.end() ? std::string_view{} : std::string_view(found->second);
	}

	// ControlBar::setUpDownImages: the minimise button's (ButtonLarge) images, enabled, pointed at and pressed while
	// pointed at: the scheme's ToggleButtonUp* while the bar is minimised (CONTROL_BAR_STAGE_LOW), its ToggleButtonDown*
	// at any other stage. (Its MinMaxButton* images are never applied: ControlBarScheme::init has them commented out.)
	struct ToggleImages
	{
		std::string_view enabled, hilite, hiliteSelected;
	};
	ToggleImages MinimizeButtonImages(bool minimised) const
	{
		return minimised ? ToggleImages{Image("ToggleButtonUpOn"), Image("ToggleButtonUpIn"), Image("ToggleButtonUpPushed")}
			: ToggleImages{Image("ToggleButtonDownOn"), Image("ToggleButtonDownIn"), Image("ToggleButtonDownPushed")};
	}

	// The right HUD's own image (RightHUDImage: ControlBarScheme::init -> updateRightHUDImage).
	std::string_view RightHudImage() const { return Image("RightHUDImage"); }
};

struct ControlBarSchemes
{
	std::vector<ControlBarSchemeContent> schemes;

	const ControlBarSchemeContent *ForSide(std::string_view side) const
	{
		const auto same = [](std::string_view a, std::string_view b) {
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
				return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
			});
		};
		const std::string_view wanted = side.empty() ? std::string_view{"Observer"} : side;
		const ControlBarSchemeContent *best = nullptr;
		for (const ControlBarSchemeContent &scheme : schemes)
			if (same(scheme.side, wanted) && (best == nullptr || best->width < scheme.width))
				best = &scheme;
		if (best == nullptr)
			for (const ControlBarSchemeContent &scheme : schemes)
				if (same(scheme.name, "Default"))
					best = &scheme;
		return best;
	}
};

namespace control_bar_scheme_detail
{
// "X:800 Y:600" -> (800, 600).
inline std::pair<int, int> Point(const engine::config::Node &node)
{
	int x = 0, y = 0;
	for (const std::string_view value : node.values)
	{
		const auto read = [&](std::string_view prefix, int &out) {
			if (value.size() > prefix.size() && value.substr(0, prefix.size()) == prefix)
				std::from_chars(value.data() + prefix.size(), value.data() + value.size(), out);
		};
		read("X:", x);
		read("Y:", y);
	}
	return {x, y};
}

// INI::parseColorInt: "R:44 G:139 B:101 A:255" (A may be left out: 255) -> 0xAARRGGBB.
inline std::uint32_t ColorInt(const engine::config::Node &node)
{
	std::array<int, 4> channels{0, 0, 0, 255};
	for (const std::string_view value : node.values)
		for (const auto &[prefix, channel] : {std::pair{"R:", 0}, std::pair{"G:", 1}, std::pair{"B:", 2}, std::pair{"A:", 3}})
			if (value.starts_with(prefix))
				std::from_chars(value.data() + 2, value.data() + value.size(), channels[static_cast<std::size_t>(channel)]);
	const auto byte = [&](int index) { return static_cast<std::uint32_t>(std::clamp(channels[static_cast<std::size_t>(index)], 0, 255)); };
	return (byte(3) << 24) | (byte(0) << 16) | (byte(1) << 8) | byte(2);
}

// parseImagePart / parseAnimatingPartImage: Position, Size, ImageName and Layer.
inline SchemeImagePart ImagePart(const engine::config::Node &field)
{
	SchemeImagePart part;
	for (const engine::config::Node &child : field.children)
	{
		if (child.key == "Position")
			std::tie(part.x, part.y) = Point(child);
		else if (child.key == "Size")
			std::tie(part.width, part.height) = Point(child);
		else if (child.key == "ImageName" && !child.values.empty())
			part.image = std::string(child.Value());
		else if (child.key == "Layer" && !child.values.empty())
			std::from_chars(child.Value().data(), child.Value().data() + child.Value().size(), part.layer);
	}
	// ControlBarScheme::addImage: a layer out of 0..5 goes to the front (0).
	if (part.layer < 0 || part.layer >= 6)
		part.layer = 0;
	return part;
}
}

// Later schemes of a name replace earlier ones (the retail file after the defaults).
inline ControlBarSchemes BindControlBarSchemes(const engine::config::Document &document)
{
	using control_bar_scheme_detail::Point;
	ControlBarSchemes result;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "ControlBarScheme" || root.values.empty())
			continue;
		ControlBarSchemeContent scheme;
		scheme.name = std::string(root.Value());
		for (const engine::config::Node &field : root.children)
		{
			const std::string_view key = field.key;
			if (key == "ScreenCreationRes")
				std::tie(scheme.width, scheme.height) = Point(field);
			else if (key == "Side" && !field.values.empty())
				scheme.side = std::string(field.Value());
			else if (key == "BuildUpClockColor")
			{
				// INI::parseColorInt: "R:0 G:0 B:0 A:160".
				for (const std::string_view value : field.values)
					for (const auto &[prefix, channel] : {std::pair{"R:", 0}, std::pair{"G:", 1}, std::pair{"B:", 2}, std::pair{"A:", 3}})
						if (value.starts_with(prefix))
							std::from_chars(value.data() + 2, value.data() + value.size(), scheme.buildUpClockColor[static_cast<std::size_t>(channel)]);
			}
			else if (key == "ButtonBorderBuildColor")
				scheme.borderBuild = control_bar_scheme_detail::ColorInt(field);
			else if (key == "ButtonBorderUpgradeColor")
				scheme.borderUpgrade = control_bar_scheme_detail::ColorInt(field);
			else if (key == "ButtonBorderActionColor")
				scheme.borderAction = control_bar_scheme_detail::ColorInt(field);
			else if (key == "ButtonBorderSystemColor")
				scheme.borderSystem = control_bar_scheme_detail::ColorInt(field);
			else if (key == "CommandBarBorderColor")
				scheme.commandBarBorder = control_bar_scheme_detail::ColorInt(field);
			else if (key == "AnimatingPart")
			{
				// parseAnimatingPart: the animation, and its image part added to its layer (addImage) like any other.
				SchemeAnimation animation;
				for (const engine::config::Node &child : field.children)
				{
					if (child.key == "Name" && !child.values.empty())
						animation.name = std::string(child.Value());
					else if (child.key == "Animation" && !child.values.empty())
						animation.type = child.Value() == "SLIDE_RIGHT" ? SchemeAnimation::SlideRight : -1;
					else if (child.key == "Duration" && !child.values.empty())
					{
						// parseDurationUnsignedInt: ceil(ms * 30 / 1000).
						std::uint64_t milliseconds = 0;
						std::from_chars(child.Value().data(), child.Value().data() + child.Value().size(), milliseconds);
						animation.durationFrames = static_cast<std::uint32_t>((milliseconds * 30 + 999) / 1000);
					}
					else if (child.key == "FinalPos")
						std::tie(animation.finalX, animation.finalY) = Point(child);
					else if (child.key == "ImagePart")
					{
						animation.image = scheme.images.size();
						animation.hasImage = true;
						scheme.images.push_back(control_bar_scheme_detail::ImagePart(child));
					}
				}
				scheme.animations.push_back(std::move(animation));
			}
			else if (key == "QueueButtonImage" && !field.values.empty())
				scheme.queueButtonImage = std::string(field.Value());
			else if (key.size() > 2 && (key.ends_with("UL") || key.ends_with("LR")) && !field.values.empty() && field.values.front().starts_with("X:"))
			{
				SchemeRect &rect = scheme.places[std::string(key.substr(0, key.size() - 2))];
				if (key.ends_with("UL"))
					std::tie(rect.left, rect.top) = Point(field);
				else
					std::tie(rect.right, rect.bottom) = Point(field);
			}
			else if (field.children.empty() && field.values.size() == 1 && key != "Side" && key != "ImagePart" && key != "AnimatingPart")
				scheme.namedImages.insert_or_assign(std::string(key), std::string(field.Value()));
			else if (key == "ImagePart")
				scheme.images.push_back(control_bar_scheme_detail::ImagePart(field));
		}
		if (const auto found = scheme.places.find("Money"); found != scheme.places.end())
			scheme.money = found->second;
		if (const auto found = scheme.places.find("PowerBar"); found != scheme.places.end())
			scheme.powerBar = found->second;
		// newControlBarScheme: a scheme of a name already read is reset and read again in its place in the list.
		const auto existing = std::ranges::find_if(result.schemes, [&](const ControlBarSchemeContent &old) { return old.name == scheme.name; });
		if (existing != result.schemes.end())
			*existing = std::move(scheme);
		else
			result.schemes.push_back(std::move(scheme));
	}
	return result;
}
}
