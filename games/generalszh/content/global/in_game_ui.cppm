export module games.generalszh.content.global.in_game_ui;
import std;

export import engine.config.binding.schema;
export import games.generalszh.content.global.radius_decal;

// InGameUI.ini's messages at the top of the screen (the original's InGameUI fields and their defaults): the colours
// the lines alternate, where the first line is, its font (Language.ini's MessageFont overrides it), and how long a
// message stays before it fades; and the superweapon countdowns (SuperweaponCountdown*): where they start (a share of
// the screen, names right-aligned to it), how long their ready flash lasts, its colour, and their normal and ready
// fonts; and the military caption's (MilitaryCaption*: colour, position on an 800x600 screen, title and line fonts).
export namespace generalszh::content
{
struct InGameUiContent
{
	std::array<std::uint8_t, 4> messageColor1{255, 255, 255, 255};
	std::array<std::uint8_t, 4> messageColor2{180, 180, 180, 255};
	std::array<int, 2> messagePosition{10, 10};
	std::string messageFont{"Arial"};
	int messagePointSize{10};
	bool messageBold{false};
	std::int64_t messageDelayMs{5000};
	std::array<Engine::Math::Fixed, 2> superweaponPosition{Engine::Math::Fixed::FromRatio(7, 10), Engine::Math::Fixed::FromRatio(7, 10)};
	// SuperweaponCountdownFlashDuration (INI::parseDurationReal: milliseconds to frames, a Real); 0: no flash.
	Engine::Math::Fixed superweaponFlashFrames{Engine::Math::Fixed::One()};
	std::array<std::uint8_t, 4> superweaponFlashColor{255, 255, 255, 255};
	std::string superweaponNormalFont{"Arial"};
	int superweaponNormalPointSize{10};
	bool superweaponNormalBold{false};
	std::string superweaponReadyFont{"Arial"};
	int superweaponReadyPointSize{10};
	bool superweaponReadyBold{false};
	std::array<std::uint8_t, 4> militaryCaptionColor{200, 200, 30, 255};
	// PopupMessageColor: the in-game popup message's text (InGameUI m_popupMessageColor, white by default).
	std::array<std::uint8_t, 4> popupMessageColor{255, 255, 255, 255};
	std::array<int, 2> militaryCaptionPosition{10, 380};
	std::string militaryCaptionTitleFont{"Courier"};
	int militaryCaptionTitlePointSize{12};
	bool militaryCaptionTitleBold{true};
	// The named timers (NamedTimerCountdown*): where they start (a share of the screen; from the middle right, their
	// right edges there), the ready ones' flash (NamedTimerCountdownFlashDuration, milliseconds as frames) and its
	// colour, their colour, and their normal and ready fonts.
	std::array<Engine::Math::Fixed, 2> namedTimerPosition{Engine::Math::Fixed::FromRatio(5, 100), Engine::Math::Fixed::FromRatio(7, 10)}; // (0.05, 0.7)
	Engine::Math::Fixed namedTimerFlashFrames{Engine::Math::Fixed::One()};
	std::array<std::uint8_t, 4> namedTimerFlashColor{0, 255, 255, 255};
	std::array<std::uint8_t, 4> namedTimerNormalColor{255, 255, 0, 255};
	std::string namedTimerNormalFont{"Arial"};
	int namedTimerNormalPointSize{10};
	bool namedTimerNormalBold{false};
	std::string namedTimerReadyFont{"Arial"};
	int namedTimerReadyPointSize{10};
	bool namedTimerReadyBold{false};
	std::string militaryCaptionFont{"Courier"};
	int militaryCaptionPointSize{12};
	bool militaryCaptionBold{false};
	// DrawableCaptionFont / PointSize / Bold / Color: the drawables' captions and construction percent (Language.ini's
	// DrawableCaptionFont overrides the font).
	std::string drawableCaptionFont{"Arial"};
	int drawableCaptionPointSize{10};
	bool drawableCaptionBold{false};
	std::array<std::uint8_t, 4> drawableCaptionColor{255, 255, 255, 255};
	// FloatingTextTimeOut (milliseconds; none: LOGICFRAMES_PER_SECOND / 3 frames), FloatingTextMoveUpSpeed and
	// FloatingTextVanishRate (a second; none: 1 and 0.1 a frame): as written, the presentation converts them.
	std::optional<std::uint32_t> floatingTextTimeoutMs;
	std::optional<Engine::Math::Fixed> floatingTextMoveUpSpeed;
	std::optional<Engine::Math::Fixed> floatingTextVanishRate;
	// DrawRMBScrollAnchor / MoveRMBScrollAnchor: the right-button scroll's anchor drawn, and dragged along to within half
	// the screen of the pointer.
	bool drawRmbScrollAnchor{false};
	bool moveRmbScrollAnchor{false};
	// MaxSelectionSize: the most a select-all / select-matching walk takes (below 1: no cap; -1 by default).
	int maxSelectionSize{-1};
	// The radius cursors (the *RadiusCursor templates), by RadiusCursorType.
	std::array<RadiusDecalLook, RadiusCursorNames.size()> radiusCursors{};
};

inline InGameUiContent BindInGameUi(const engine::config::Document &document)
{
	InGameUiContent ui;
	// INI::parseColorInt ("R:255 G:255 B:255 [A:255]", alpha 255 unless given) and parseICoord2D ("X:10 Y:10").
	const auto channels = [](const engine::config::Node &field, std::array<int, 4> out) {
		for (const std::string_view value : field.values)
			for (const auto &[prefix, index] : {std::pair{"R:", 0}, std::pair{"G:", 1}, std::pair{"B:", 2}, std::pair{"A:", 3}, std::pair{"X:", 0}, std::pair{"Y:", 1}})
				if (value.starts_with(prefix))
					std::from_chars(value.data() + 2, value.data() + value.size(), out[static_cast<std::size_t>(index)]);
		return out;
	};
	const auto color = [&](const engine::config::Node &field, std::array<std::uint8_t, 4> &out) {
		const auto read = channels(field, {out[0], out[1], out[2], 255});
		for (std::size_t index = 0; index < 4; ++index)
			out[index] = static_cast<std::uint8_t>(std::clamp(read[index], 0, 255));
	};
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "InGameUI")
			continue;
		for (const engine::config::Node &field : root.children)
		{
			const std::string_view key = field.key;
			for (const auto &[name, index] : RadiusCursorFields)
				if (key == name)
					ui.radiusCursors[index] = ReadRadiusDecal(field, 30);
			if (key == "MessageColor1")
				color(field, ui.messageColor1);
			else if (key == "MessageColor2")
				color(field, ui.messageColor2);
			else if (key == "PopupMessageColor")
				color(field, ui.popupMessageColor);
			else if (key == "MessagePosition")
			{
				const auto read = channels(field, {ui.messagePosition[0], ui.messagePosition[1], 0, 0});
				ui.messagePosition = {read[0], read[1]};
			}
			else if (key == "MessageFont" && !field.values.empty())
				ui.messageFont = std::string(field.Value());
			else if (key == "MessagePointSize" && !field.values.empty())
				std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), ui.messagePointSize);
			else if (key == "MessageBold" && !field.values.empty())
				ui.messageBold = engine::config::values::ParseBool(field.Value()).value_or(ui.messageBold);
			else if (key == "DrawableCaptionFont" && !field.values.empty())
				ui.drawableCaptionFont = std::string(field.Value());
			else if (key == "DrawableCaptionPointSize" && !field.values.empty())
				std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), ui.drawableCaptionPointSize);
			else if (key == "DrawableCaptionBold" && !field.values.empty())
				ui.drawableCaptionBold = engine::config::values::ParseBool(field.Value()).value_or(ui.drawableCaptionBold);
			else if (key == "FloatingTextTimeOut" && !field.values.empty())
			{
				std::uint32_t milliseconds = 0;
				if (std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), milliseconds).ec == std::errc{})
					ui.floatingTextTimeoutMs = milliseconds;
			}
			else if (key == "FloatingTextMoveUpSpeed" && !field.values.empty())
			{
				if (const auto speed = engine::config::values::ParseFixed(field.Value()))
					ui.floatingTextMoveUpSpeed = *speed;
			}
			else if (key == "FloatingTextVanishRate" && !field.values.empty())
			{
				if (const auto rate = engine::config::values::ParseFixed(field.Value()))
					ui.floatingTextVanishRate = *rate;
			}
			else if (key == "DrawRMBScrollAnchor" && !field.values.empty())
				ui.drawRmbScrollAnchor = engine::config::values::ParseBool(field.Value()).value_or(ui.drawRmbScrollAnchor);
			else if (key == "MoveRMBScrollAnchor" && !field.values.empty())
				ui.moveRmbScrollAnchor = engine::config::values::ParseBool(field.Value()).value_or(ui.moveRmbScrollAnchor);
			else if (key == "MaxSelectionSize" && !field.values.empty())
				std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), ui.maxSelectionSize);
			else if (key == "DrawableCaptionColor")
				color(field, ui.drawableCaptionColor);
			else if (key == "SuperweaponCountdownPosition")
			{
				// INI::parseCoord2D: X:0.90 Y:0.01.
				for (const std::string_view value : field.values)
					for (const auto &[prefix, index] : {std::pair{"X:", 0}, std::pair{"Y:", 1}})
						if (value.starts_with(prefix))
							ui.superweaponPosition[static_cast<std::size_t>(index)] =
								engine::config::values::ParseFixed(value.substr(2)).value_or(ui.superweaponPosition[static_cast<std::size_t>(index)]);
			}
			else if (key == "SuperweaponCountdownFlashDuration" && !field.values.empty())
				ui.superweaponFlashFrames = engine::config::values::ParseFixed(field.Value()).value_or(Engine::Math::Fixed{}) * Engine::Math::Fixed::FromInt(30) /
					Engine::Math::Fixed::FromInt(1000);
			else if (key == "SuperweaponCountdownFlashColor")
				color(field, ui.superweaponFlashColor);
			else if (key == "SuperweaponCountdownNormalFont" && !field.values.empty())
				ui.superweaponNormalFont = std::string(field.Value());
			else if (key == "SuperweaponCountdownNormalPointSize" && !field.values.empty())
				std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), ui.superweaponNormalPointSize);
			else if (key == "SuperweaponCountdownNormalBold" && !field.values.empty())
				ui.superweaponNormalBold = engine::config::values::ParseBool(field.Value()).value_or(ui.superweaponNormalBold);
			else if (key == "SuperweaponCountdownReadyFont" && !field.values.empty())
				ui.superweaponReadyFont = std::string(field.Value());
			else if (key == "SuperweaponCountdownReadyPointSize" && !field.values.empty())
				std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), ui.superweaponReadyPointSize);
			else if (key == "SuperweaponCountdownReadyBold" && !field.values.empty())
				ui.superweaponReadyBold = engine::config::values::ParseBool(field.Value()).value_or(ui.superweaponReadyBold);
			else if (key == "NamedTimerCountdownPosition")
			{
				for (const std::string_view value : field.values)
					for (const auto &[prefix, index] : {std::pair{"X:", 0}, std::pair{"Y:", 1}})
						if (value.starts_with(prefix))
							ui.namedTimerPosition[static_cast<std::size_t>(index)] =
								engine::config::values::ParseFixed(value.substr(2)).value_or(ui.namedTimerPosition[static_cast<std::size_t>(index)]);
			}
			else if (key == "NamedTimerCountdownFlashDuration" && !field.values.empty())
				ui.namedTimerFlashFrames = engine::config::values::ParseFixed(field.Value()).value_or(Engine::Math::Fixed{}) * Engine::Math::Fixed::FromInt(30) /
					Engine::Math::Fixed::FromInt(1000);
			else if (key == "NamedTimerCountdownFlashColor")
				color(field, ui.namedTimerFlashColor);
			else if (key == "NamedTimerCountdownNormalColor")
				color(field, ui.namedTimerNormalColor);
			else if (key == "NamedTimerCountdownNormalFont" && !field.values.empty())
				ui.namedTimerNormalFont = std::string(field.Value());
			else if (key == "NamedTimerCountdownNormalPointSize" && !field.values.empty())
				std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), ui.namedTimerNormalPointSize);
			else if (key == "NamedTimerCountdownNormalBold" && !field.values.empty())
				ui.namedTimerNormalBold = engine::config::values::ParseBool(field.Value()).value_or(ui.namedTimerNormalBold);
			else if (key == "NamedTimerCountdownReadyFont" && !field.values.empty())
				ui.namedTimerReadyFont = std::string(field.Value());
			else if (key == "NamedTimerCountdownReadyPointSize" && !field.values.empty())
				std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), ui.namedTimerReadyPointSize);
			else if (key == "NamedTimerCountdownReadyBold" && !field.values.empty())
				ui.namedTimerReadyBold = engine::config::values::ParseBool(field.Value()).value_or(ui.namedTimerReadyBold);
			else if (key == "MilitaryCaptionColor")
				color(field, ui.militaryCaptionColor);
			else if (key == "MilitaryCaptionPosition")
			{
				const auto read = channels(field, {ui.militaryCaptionPosition[0], ui.militaryCaptionPosition[1], 0, 0});
				ui.militaryCaptionPosition = {read[0], read[1]};
			}
			else if (key == "MilitaryCaptionTitleFont" && !field.values.empty())
			{
				// INI::parseAsciiString: the rest of the line ("Courier New").
				std::string name;
				for (const std::string_view token : field.values)
					name += (name.empty() ? "" : " ") + std::string(token);
				ui.militaryCaptionTitleFont = name;
			}
			else if (key == "MilitaryCaptionFont" && !field.values.empty())
			{
				std::string name;
				for (const std::string_view token : field.values)
					name += (name.empty() ? "" : " ") + std::string(token);
				ui.militaryCaptionFont = name;
			}
			else if (key == "MilitaryCaptionTitlePointSize" && !field.values.empty())
				std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), ui.militaryCaptionTitlePointSize);
			else if (key == "MilitaryCaptionPointSize" && !field.values.empty())
				std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), ui.militaryCaptionPointSize);
			else if (key == "MilitaryCaptionTitleBold" && !field.values.empty())
				ui.militaryCaptionTitleBold = engine::config::values::ParseBool(field.Value()).value_or(ui.militaryCaptionTitleBold);
			else if (key == "MilitaryCaptionBold" && !field.values.empty())
				ui.militaryCaptionBold = engine::config::values::ParseBool(field.Value()).value_or(ui.militaryCaptionBold);
			else if (key == "MessageDelayMS" && !field.values.empty())
				std::from_chars(field.Value().data(), field.Value().data() + field.Value().size(), ui.messageDelayMs);
		}
	}
	return ui;
}
}
