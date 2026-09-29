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

struct ControlBarSchemeContent
{
	std::string name;
	std::string side;
	int width{800}, height{600}; // ScreenCreationRes
	std::vector<SchemeImagePart> images;
	std::string queueButtonImage;
	// BuildUpClockColor (R G B A, 0..255): the production queue's build clock.
	std::array<int, 4> buildUpClockColor{0, 0, 0, 100};
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
				scheme.images.push_back(std::move(part));
			}
		}
		if (const auto found = scheme.places.find("Money"); found != scheme.places.end())
			scheme.money = found->second;
		if (const auto found = scheme.places.find("PowerBar"); found != scheme.places.end())
			scheme.powerBar = found->second;
		std::erase_if(result.schemes, [&](const ControlBarSchemeContent &old) { return old.name == scheme.name; });
		result.schemes.push_back(std::move(scheme));
	}
	return result;
}
}
