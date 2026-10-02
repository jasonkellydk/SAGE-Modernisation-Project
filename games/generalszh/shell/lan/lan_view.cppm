export module games.generalszh.shell.lan.lan_view;
import std;

export import games.generalszh.shell.lan.lan_game_options_view_model;
export import games.generalszh.shell.skirmish.skirmish_view;
export import Engine.UI.WND.Bindings;
import Engine.UI.WND;
import Engine.UI.WND.Document;

// Binds the LAN screens' layouts to their view models: LanLobbyMenu.wnd,
// NetworkDirectConnect.wnd, LanGameOptionsMenu.wnd and LanMapSelectMenu.wnd.
export namespace generalszh::shell
{
namespace wnd = Engine::UI::WND;

namespace detail
{
// A view's derived observables and the subscriptions feeding them.
class Derived
{
public:
	~Derived()
	{
		for (auto &release : m_releases)
			release();
	}
	template<class T, class Handler>
	void Watch(engine::gui::mvvm::Observable<T> &observable, Handler handler)
	{
		const auto id = observable.Subscribe([handler](const T &) { handler(); });
		m_releases.push_back([&observable, id] { observable.Unsubscribe(id); });
	}

private:
	std::vector<std::function<void()>> m_releases;
};

// A chat window's lines and their colours.
inline void SplitChat(const std::vector<ChatLine> &lines, engine::gui::mvvm::Observable<std::vector<std::u16string>> &text,
	engine::gui::mvvm::Observable<std::vector<std::uint32_t>> &colors, engine::gui::mvvm::Observable<int> &last)
{
	std::vector<std::u16string> shown;
	std::vector<std::uint32_t> tints;
	for (const ChatLine &line : lines)
	{
		shown.push_back(line.text);
		tints.push_back(line.rgba);
	}
	colors.Set(std::move(tints));
	text.Set(std::move(shown));
	(void)last;
}
}

class LanLobbyView : detail::Derived
{
public:
	LanLobbyView(wnd::WNDBindings &bindings, LanLobbyViewModel &viewModel)
	{
		const std::string menu = "LanLobbyMenu.wnd:";
		bindings.BindEntry(menu + "TextEntryPlayerName", viewModel.playerName);
		bindings.BindEntry(menu + "TextEntryChat", viewModel.chatEntry, &viewModel.send);
		bindings.BindList(menu + "ListboxPlayers", viewModel.players, viewModel.selectedPlayer);
		bindings.BindRowColors(menu + "ListboxPlayers", viewModel.playerColors);
		bindings.BindList(menu + "ListboxGames", viewModel.games, viewModel.selectedGame, &viewModel.join);
		bindings.BindRowColors(menu + "ListboxGames", viewModel.gameColors);
		bindings.BindLog(menu + "ListboxChatWindowLanLobby", m_chat);
		bindings.BindRowColors(menu + "ListboxChatWindowLanLobby", m_chatColors);
		bindings.BindCommand(menu + "ButtonBack", viewModel.back);
		bindings.BindCommand(menu + "ButtonHost", viewModel.host);
		bindings.BindCommand(menu + "ButtonJoin", viewModel.join);
		bindings.BindCommand(menu + "ButtonClear", viewModel.clear);
		bindings.BindCommand(menu + "ButtonEmote", viewModel.send);
		bindings.BindCommand(menu + "ButtonDirectConnect", viewModel.directConnect);
		Watch(viewModel.chat, [this, &viewModel] { detail::SplitChat(viewModel.chat.Get(), m_chat, m_chatColors, m_chatLast); });
	}

private:
	engine::gui::mvvm::Observable<std::vector<std::u16string>> m_chat;
	engine::gui::mvvm::Observable<std::vector<std::uint32_t>> m_chatColors;
	engine::gui::mvvm::Observable<int> m_chatLast{-1};
};

// GameInfoWindow.wnd over the lobby's StaticTextGameInfo: the selected game's host, map and players (RefreshGameInfoWindow:
// each name in the list's second column, its side's icon, 22 x 25, in the first).
class GameInfoView : detail::Derived
{
public:
	GameInfoView(wnd::WNDBindings &bindings, LanLobbyViewModel &viewModel, std::function<wnd::ImageRef(std::string_view)> resolve = {})
	{
		const std::string info = "GameInfoWindow.wnd:";
		bindings.BindVisible(info + "ParentGameInfo", m_shown);
		bindings.BindText(info + "StaticTextGameName", m_host);
		bindings.BindText(info + "StaticTextMapName", m_map);
		bindings.BindList(info + "ListBoxPlayers", m_players, m_noRow);
		bindings.BindRowColors(info + "ListBoxPlayers", m_colors);
		bindings.BindRowImages(info + "ListBoxPlayers", m_icons, std::move(resolve));
		Watch(viewModel.details, [this, &viewModel] {
			const GameDetails &details = viewModel.details.Get();
			m_host.Set(details.host);
			m_map.Set(details.map);
			std::vector<std::u16string> names;
			std::vector<std::uint32_t> colors;
			std::vector<std::vector<wnd::ListImageSource>> icons;
			for (std::size_t index = 0; index < details.players.size(); ++index)
			{
				names.push_back(u'\t' + details.players[index].text);
				colors.push_back(details.players[index].rgba);
				icons.push_back({{index < details.icons.size() ? details.icons[index] : std::string{}, 22, 25}});
			}
			m_colors.Set(std::move(colors));
			m_icons.Set(std::move(icons));
			m_players.Set(std::move(names));
			m_shown.Set(details.shown);
		});
	}

private:
	engine::gui::mvvm::Observable<bool> m_shown{false};
	engine::gui::mvvm::Observable<std::u16string> m_host, m_map;
	engine::gui::mvvm::Observable<std::vector<std::u16string>> m_players;
	engine::gui::mvvm::Observable<std::vector<std::uint32_t>> m_colors;
	engine::gui::mvvm::Observable<std::vector<std::vector<wnd::ListImageSource>>> m_icons;
	engine::gui::mvvm::Observable<int> m_noRow{-1};
};

inline void BindDirectConnectView(wnd::WNDBindings &bindings, DirectConnectViewModel &viewModel)
{
	const std::string menu = "NetworkDirectConnect.wnd:";
	bindings.BindEntry(menu + "EditPlayerName", viewModel.playerName);
	bindings.BindComboBox(menu + "ComboboxRemoteIP", viewModel.remoteItems, viewModel.remote);
	bindings.BindEntry(menu + "ComboboxRemoteIP", viewModel.remoteText); // an address can be typed
	bindings.BindText(menu + "StaticLocalIP", viewModel.localAddress);
	bindings.BindCommand(menu + "ButtonBack", viewModel.back);
	bindings.BindCommand(menu + "ButtonHost", viewModel.host);
	bindings.BindCommand(menu + "ButtonJoin", viewModel.join);
}

class LanGameOptionsView : detail::Derived
{
public:
	LanGameOptionsView(wnd::WNDBindings &bindings, wnd::WNDDocument &document, wnd::WNDBindings &mapBindings, wnd::WNDDocument &mapDocument,
		LanGameOptionsViewModel &viewModel, std::function<wnd::ImageRef(std::string_view)> resolve, wnd::WNDFrame frame)
		: m_viewModel(viewModel)
	{
		const std::string menu = "LanGameOptionsMenu.wnd:";
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			const auto at = static_cast<std::size_t>(index);
			const std::string slot = std::to_string(index);
			bindings.BindComboBox(menu + "ComboBoxPlayer" + slot, viewModel.playerItems[at], viewModel.player[at]);
			bindings.BindText(menu + "ComboBoxPlayer" + slot, viewModel.playerText[at]);
			bindings.BindEnabled(menu + "ComboBoxPlayer" + slot, viewModel.playerEnabled[at]);
			bindings.BindComboBox(menu + "ComboBoxColor" + slot, viewModel.colorItems[at], viewModel.color[at]);
			bindings.BindRowColors(menu + "ComboBoxColor" + slot, viewModel.colorRowColors[at]);
			bindings.BindComboBox(menu + "ComboBoxPlayerTemplate" + slot, viewModel.factionItems[at], viewModel.faction[at]);
			bindings.BindComboBox(menu + "ComboBoxTeam" + slot, viewModel.teamItems[at], viewModel.team[at]);
			for (const char *box : {"ComboBoxColor", "ComboBoxPlayerTemplate", "ComboBoxTeam"})
				bindings.BindEnabled(menu + box + slot, viewModel.slotEnabled[at]);
			// The accept lights: shown for the other humans, lit once they accept.
			bindings.BindVisible(menu + "ButtonAccept" + slot, index == 0 ? m_always : viewModel.acceptShown[at]);
			bindings.BindEnabled(menu + "ButtonAccept" + slot, index == 0 ? m_always : viewModel.accepted[at]);
			bindings.BindText(menu + "ButtonMapStartPosition" + slot, viewModel.startNumber[at]);
			bindings.BindVisible(menu + "ButtonMapStartPosition" + slot, viewModel.startShown[at]);
			bindings.BindCommand(menu + "ButtonMapStartPosition" + slot, viewModel.startPosition[at]);
			bindings.BindRightCommand(menu + "ButtonMapStartPosition" + slot, viewModel.startPositionClear[at]);
		}
		bindings.BindText(menu + "TextEntryMapDisplay", viewModel.mapName);
		bindings.BindComboBox(menu + "ComboBoxStartingCash", viewModel.moneyItems, viewModel.money);
		bindings.BindEnabled(menu + "ComboBoxStartingCash", viewModel.hostOptionsEnabled);
		bindings.BindChecked(menu + "CheckboxLimitSuperweapons", viewModel.limitSuperweapons);
		bindings.BindEnabled(menu + "CheckboxLimitSuperweapons", viewModel.hostOptionsEnabled);
		bindings.BindEnabled(menu + "ButtonSelectMap", viewModel.hostOptionsEnabled);
		bindings.BindText(menu + "ButtonStart", viewModel.startText);
		bindings.BindEnabled(menu + "ButtonStart", viewModel.startEnabled);
		bindings.BindCommand(menu + "ButtonStart", viewModel.start);
		bindings.BindCommand(menu + "ButtonSelectMap", viewModel.selectMap);
		bindings.BindCommand(menu + "ButtonBack", viewModel.back);
		bindings.BindEnabled(menu + "ButtonBack", viewModel.mainButtonsEnabled);
		bindings.BindEntry(menu + "TextEntryChat", viewModel.chatEntry, &viewModel.send);
		bindings.BindCommand(menu + "ButtonEmote", viewModel.send);
		bindings.BindLog(menu + "ListboxChatWindowLanGame", m_chat);
		bindings.BindRowColors(menu + "ListboxChatWindowLanGame", m_chatColors);
		Watch(viewModel.chat, [this] { detail::SplitChat(m_viewModel.chat.Get(), m_chat, m_chatColors, m_chatLast); });
		// Under the map list (LanMapSelectMenuInit hides these).
		for (const char *name : {"MapWindow", "StaticTextTeam", "StaticTextFaction", "StaticTextColor", "TextEntryMapDisplay", "ButtonSelectMap", "ButtonStart",
				 "StaticTextMapPreview"})
			bindings.BindVisible(menu + name, viewModel.mapSelectOpen, [](bool open) { return !open; });
		for (int index = 0; index < setup::MaxSlots; ++index)
			for (const char *box : {"ComboBoxTeam", "ComboBoxColor", "ComboBoxPlayerTemplate"})
				bindings.BindVisible(menu + box + std::to_string(index), viewModel.mapSelectOpen, [](bool open) { return !open; });
		bindings.BindPicture(menu + "MapWindow", m_picture, resolve, frame);
		const auto place = [this, &document, menu] {
			detail::PlaceStartButtons(document, menu + "MapWindow", menu + "ButtonMapStartPosition", m_viewModel.preview.Get(), m_viewModel.startSpots.Get());
		};
		Watch(viewModel.preview, [this, place] {
			m_picture.Set(detail::Picture(m_viewModel.preview.Get()));
			place();
		});
		Watch(viewModel.startSpots, place);

		const std::string list = "LanMapSelectMenu.wnd:";
		mapBindings.BindList(list + "ListboxMap", viewModel.mapItems, viewModel.mapSelected, &viewModel.mapOk);
		mapBindings.BindCommand(list + "ButtonOK", viewModel.mapOk);
		mapBindings.BindCommand(list + "ButtonBack", viewModel.mapBack);
		mapBindings.BindChecked(list + "RadioButtonSystemMaps", viewModel.systemMaps);
		mapBindings.BindChecked(list + "RadioButtonUserMaps", m_userMaps);
		mapBindings.BindCommand(list + "RadioButtonSystemMaps", m_pickSystemMaps);
		mapBindings.BindCommand(list + "RadioButtonUserMaps", m_pickUserMaps);
		m_pickSystemMaps.SetAction([this] { m_viewModel.systemMaps.Set(true); });
		m_pickUserMaps.SetAction([this] { m_viewModel.systemMaps.Set(false); });
		Watch(viewModel.systemMaps, [this] { m_userMaps.Set(!m_viewModel.systemMaps.Get()); });
		mapBindings.BindPicture(list + "WinMapPreview", m_listPicture, resolve, frame);
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			const auto at = static_cast<std::size_t>(index);
			mapBindings.BindVisible(list + "ButtonMapStartPosition" + std::to_string(index), m_listSpotShown[at]);
			mapBindings.BindEnabled(list + "ButtonMapStartPosition" + std::to_string(index), m_never);
		}
		const auto placeList = [this, &mapDocument, list] {
			const auto &spots = m_viewModel.mapListStartSpots.Get();
			for (std::size_t spot = 0; spot < m_listSpotShown.size(); ++spot)
				m_listSpotShown[spot].Set(spot < spots.size());
			detail::PlaceStartButtons(mapDocument, list + "WinMapPreview", list + "ButtonMapStartPosition", m_viewModel.mapListPreview.Get(), spots);
		};
		Watch(viewModel.mapListPreview, [this, placeList] {
			m_listPicture.Set(detail::Picture(m_viewModel.mapListPreview.Get()));
			placeList();
		});
		Watch(viewModel.mapListStartSpots, placeList);
	}

private:
	LanGameOptionsViewModel &m_viewModel;
	engine::gui::mvvm::Observable<wnd::PictureSource> m_picture, m_listPicture;
	engine::gui::mvvm::Observable<std::vector<std::u16string>> m_chat;
	engine::gui::mvvm::Observable<std::vector<std::uint32_t>> m_chatColors;
	engine::gui::mvvm::Observable<int> m_chatLast{-1};
	engine::gui::mvvm::Observable<bool> m_always{true}, m_userMaps{false}, m_never{false};
	std::array<engine::gui::mvvm::Observable<bool>, 8> m_listSpotShown{};
	engine::gui::mvvm::Command m_pickSystemMaps, m_pickUserMaps;
};
}
