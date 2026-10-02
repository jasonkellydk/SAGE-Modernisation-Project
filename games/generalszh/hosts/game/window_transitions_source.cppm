export module games.generalszh.hosts.game.window_transitions_source;
import std;

export import engine.config.binding.schema;
export import Engine.UI.WND.Transitions;

// WindowTransitions.ini (the original's INI::parseWindowTransitions): each
// WindowTransition group's windows, their style and frame delay, and whether
// the group plays once. A group named twice keeps its first definition.
export namespace generalszh::host
{
namespace detail
{
inline bool SameName(std::string_view a, std::string_view b) noexcept
{
	if (a.size() != b.size())
		return false;
	for (std::size_t at = 0; at < a.size(); ++at)
		if ((a[at] | 0x20) != (b[at] | 0x20))
			return false;
	return true;
}
}


inline std::vector<Engine::UI::WND::TransitionGroupDefinition> BindWindowTransitions(const engine::config::Document &document,
	engine::config::BindContext &context)
{
	using namespace engine::config;
	using Engine::UI::WND::TransitionStyle;
	static constexpr std::pair<std::string_view, TransitionStyle> Styles[] = {{"FLASH", TransitionStyle::Flash},
		{"BUTTONFLASH", TransitionStyle::ButtonFlash}, {"WINFADE", TransitionStyle::WinFade}, {"WINSCALEUP", TransitionStyle::WinScaleUp},
		{"MAINMENUSCALEUP", TransitionStyle::MainMenuScaleUp}, {"TYPETEXT", TransitionStyle::TypeText}, {"SCREENFADE", TransitionStyle::ScreenFade},
		{"COUNTUP", TransitionStyle::CountUp}, {"FULLFADE", TransitionStyle::FullFade}, {"TEXTONFRAME", TransitionStyle::TextOnFrame},
		{"MAINMENUMEDIUMSCALEUP", TransitionStyle::MainMenuMediumScaleUp}, {"MAINMENUSMALLSCALEDOWN", TransitionStyle::MainMenuSmallScaleDown},
		{"CONTROLBARARROW", TransitionStyle::ControlBarArrow}, {"SCORESCALEUP", TransitionStyle::ScoreScaleUp},
		{"REVERSESOUND", TransitionStyle::ReverseSound}};
	std::vector<Engine::UI::WND::TransitionGroupDefinition> groups;
	for (const Node &root : document.Roots())
	{
		if (root.key != "WindowTransition" || root.values.empty())
			continue;
		Engine::UI::WND::TransitionGroupDefinition group;
		group.name = std::string(root.values.front());
		bool known = false;
		for (const auto &existing : groups)
			known = known || detail::SameName(existing.name, group.name);
		if (known)
			continue;
		for (const Node &field : root.children)
		{
			if (field.key == "FireOnce")
				group.fireOnce = ReadBool(field, context).value_or(false);
			else if (field.key == "Window")
			{
				Engine::UI::WND::TransitionWindowDefinition window;
				for (const Node &part : field.children)
					if (part.key == "WinName")
						window.window = ReadText(part);
					else if (part.key == "FrameDelay")
						window.frameDelay = static_cast<int>(ReadInt(part, context).value_or(0));
					else if (part.key == "Style")
					{
						const std::string style = ReadText(part);
						for (const auto &[name, value] : Styles)
							if (detail::SameName(name, style))
								window.style = value;
					}
				group.windows.push_back(std::move(window));
			}
		}
		groups.push_back(std::move(group));
	}
	return groups;
}
}
