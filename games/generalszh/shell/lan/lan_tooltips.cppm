export module games.generalszh.shell.lan.lan_tooltips;
import std;

export import games.generalszh.shell.skirmish.skirmish_tooltips;

// The LAN screens' tooltip callbacks (winSetTooltipFunc), as skirmish_tooltips: what each would set as the mouse's
// tooltip (a label to fetch with its arguments, its delay and width), or nothing set.
export namespace generalszh::shell
{
// LanGameOptionsMenu.cpp setLANPlayerTooltip: TOOLTIP:LANPlayer with the player's login and machine, set only when
// either is known. (Its debug-only address is not part of a release game.)
inline std::optional<TooltipCall> LanPlayerTooltip(const std::string &login, const std::string &host)
{
	if (login.empty() && host.empty())
		return std::nullopt;
	TooltipCall call{"TOOLTIP:LANPlayer"};
	call.arguments = {std::u16string(login.begin(), login.end()), std::u16string(host.begin(), host.end())};
	return call;
}

// LanLobbyMenu.cpp playerTooltip over the players' list: off its rows nothing; else the row's player's.
inline std::optional<TooltipCall> LanLobbyPlayerTooltip(int row, int column, const std::optional<std::pair<std::string, std::string>> &player)
{
	if (row == -1 || column == -1 || !player)
		return std::nullopt;
	return LanPlayerTooltip(player->first, player->second);
}

// LanGameOptionsMenu.cpp playerTooltip over a slot's player box: a slot without a human an empty tooltip, else its
// player's.
inline std::optional<TooltipCall> LanSlotPlayerTooltip(const std::optional<std::pair<std::string, std::string>> &player)
{
	if (!player)
		return TooltipCall{};
	return LanPlayerTooltip(player->first, player->second);
}

// WOLGameSetupMenu.cpp gameAcceptTooltip over a slot's accept mark: TOOLTIP:GameAcceptance strictly inside the window
// (its edges give nothing).
inline std::optional<TooltipCall> GameAcceptTooltip(int x, int y, int left, int top, int width, int height)
{
	if (x > left && x < left + width && y > top && y < top + height)
		return TooltipCall{"TOOLTIP:GameAcceptance"};
	return std::nullopt;
}
}
