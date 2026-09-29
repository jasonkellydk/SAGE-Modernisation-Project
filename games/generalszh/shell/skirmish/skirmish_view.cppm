export module games.generalszh.shell.skirmish.skirmish_view;
import std;

export import games.generalszh.shell.skirmish.skirmish_view_model;
export import Engine.UI.WND.Bindings;
import Engine.UI.WND;
import Engine.UI.WND.Document;

// Binds Window/Menus/SkirmishGameOptionsMenu.wnd and SkirmishMapSelectMenu.wnd
// to the SkirmishViewModel: every slot's boxes, the map preview with its start
// spot buttons placed over it (positionStartSpotControls), the options, the
// record, and the map list shown over the screen, which hides what lies
// under it (showSkirmishGameOptionsUnderlyingGUIElements).
export namespace generalszh::shell
{
namespace wnd = Engine::UI::WND;

namespace detail
{
inline wnd::PictureSource Picture(const MapPreview &preview)
{
	wnd::PictureSource source;
	source.shown = true;
	source.known = preview.known;
	source.image = preview.image;
	source.extentWidth = preview.extentWidth;
	source.extentHeight = preview.extentHeight;
	constexpr int SupplyTechSize = 15; // SUPPLY_TECH_SIZE
	for (const MapPoint &tech : preview.techs)
		source.markers.push_back({tech.x, tech.y, "TecBuilding", SupplyTechSize});
	for (const MapPoint &supply : preview.supplies)
		source.markers.push_back({supply.x, supply.y, "Cash", SupplyTechSize});
	return source;
}

// positionStartSpotControls: each button centred on its spot of the letterboxed map. (The
// original's nudge off an earlier button compares a position inside the map window with
// screen positions, so it never moves one; it is left out.)
inline void PlaceStartButtons(wnd::WNDDocument &document, std::string_view mapWindow, std::string_view buttonPrefix, const MapPreview &preview,
	const std::vector<MapPoint> &spots)
{
	const wnd::WNDWindow *map = document.Find_Window(mapWindow);
	if (map == nullptr)
		return;
	const wnd::Rect fit = wnd::Letterbox(map->screen_region, preview.extentWidth, preview.extentHeight);
	for (std::size_t spot = 0; spot < spots.size() && spot < 8; ++spot)
	{
		const std::string name = std::string(buttonPrefix) + std::to_string(spot);
		const wnd::WNDWindow *button = document.Find_Window(name);
		if (button == nullptr)
			continue;
		const int width = button->screen_region.right - button->screen_region.left, height = button->screen_region.bottom - button->screen_region.top;
		const int x = fit.left + static_cast<int>(static_cast<long long>(spots[spot].x) * (fit.right - fit.left) / 10000) - width / 2;
		const int y = fit.top + static_cast<int>(static_cast<long long>(spots[spot].y) * (fit.bottom - fit.top) / 10000) - height / 2;
		document.Move_Window(name, x, y);
	}
}
}

class SkirmishView
{
public:
	// `resolve`: an image by name (a mapped image or a texture path); `frame`: the previews' frame images.
	SkirmishView(wnd::WNDBindings &bindings, wnd::WNDDocument &document, wnd::WNDBindings &mapBindings, wnd::WNDDocument &mapDocument,
		SkirmishViewModel &viewModel, std::function<wnd::ImageRef(std::string_view)> resolve, wnd::WNDFrame frame)
		: m_viewModel(viewModel)
	{
		const std::string menu = "SkirmishGameOptionsMenu.wnd:";
		for (int index = 0; index < setup::MaxSlots; ++index)
		{
			const auto at = static_cast<std::size_t>(index);
			const std::string slot = std::to_string(index);
			if (index > 0)
				bindings.BindComboBox(menu + "ComboBoxPlayer" + slot, viewModel.playerItems[at], viewModel.player[at]);
			bindings.BindComboBox(menu + "ComboBoxColor" + slot, viewModel.colorItems[at], viewModel.color[at]);
			bindings.BindRowColors(menu + "ComboBoxColor" + slot, viewModel.colorRowColors[at]);
			bindings.BindComboBox(menu + "ComboBoxPlayerTemplate" + slot, viewModel.factionItems[at], viewModel.faction[at]);
			bindings.BindComboBox(menu + "ComboBoxTeam" + slot, viewModel.teamItems[at], viewModel.team[at]);
			for (const char *box : {"ComboBoxColor", "ComboBoxPlayerTemplate", "ComboBoxTeam"})
				bindings.BindEnabled(menu + box + slot, viewModel.slotEnabled[at]);
			bindings.BindText(menu + "ButtonMapStartPosition" + slot, viewModel.startText[at]);
			bindings.BindVisible(menu + "ButtonMapStartPosition" + slot, viewModel.startShown[at]);
			bindings.BindCommand(menu + "ButtonMapStartPosition" + slot, viewModel.startPosition[at]);
			bindings.BindRightCommand(menu + "ButtonMapStartPosition" + slot, viewModel.startPositionClear[at]);
		}
		bindings.BindEntry(menu + "TextEntryPlayerName", viewModel.playerName);
		bindings.BindText(menu + "TextEntryMapDisplay", viewModel.mapName);
		bindings.BindComboBox(menu + "ComboBoxStartingCash", viewModel.moneyItems, viewModel.money);
		bindings.BindChecked(menu + "CheckboxLimitSuperweapons", viewModel.limitSuperweapons);
		bindings.BindSlider(menu + "SliderGameSpeed", viewModel.gameSpeed);
		bindings.BindText(menu + "StaticTextGameSpeed", viewModel.gameSpeedText);
		bindings.BindEnabled(menu + "StaticTextGameSpeed", m_gameSpeedEnabled);
		bindings.BindText(menu + "StaticTextWinsValue", viewModel.wins);
		bindings.BindText(menu + "StaticTextLossesValue", viewModel.losses);
		bindings.BindText(menu + "StaticTextStreakValue", viewModel.streak);
		bindings.BindText(menu + "StaticTextBestStreakValue", viewModel.bestStreak);
		// The honours (InsertBattleHonor): 40 x 41 at 800 x 600, dark (80, 80, 80) until gained; spacer rows 10 high.
		bindings.BindList(menu + "ListboxInfo", m_honorRows, m_noHonor);
		bindings.BindRowImages(menu + "ListboxInfo", m_honorImages, resolve);
		const int unit = document.Creation_Width() > 0 ? document.Creation_Width() : 800;
		Watch(viewModel.honors, [this, unit] {
			std::vector<std::u16string> rows;
			std::vector<std::vector<wnd::ListImageSource>> images;
			for (const auto &row : m_viewModel.honors.Get())
			{
				rows.emplace_back();
				std::vector<wnd::ListImageSource> cells;
				if (row.empty())
					cells.push_back({{}, 10 * unit / 800, 10 * unit / 800});
				for (const BattleHonor &honor : row)
					cells.push_back({honor.image, 40 * unit / 800, 41 * unit / 800, honor.gained ? 0xFFFFFFFFu : 0x505050FFu});
				images.push_back(std::move(cells));
			}
			m_honorImages.Set(std::move(images));
			m_honorRows.Set(std::move(rows));
		});
		bindings.BindCommand(menu + "ButtonStart", viewModel.start);
		bindings.BindCommand(menu + "ButtonSelectMap", viewModel.selectMap);
		bindings.BindCommand(menu + "ButtonBack", viewModel.back);
		bindings.BindEnabled(menu + "ButtonBack", viewModel.mainButtonsEnabled);
		// Under the map list: these hide, and show again when it closes. (The original also shows
		// ButtonReset then, which the layout keeps hidden; it stays hidden here.)
		for (const char *name : {"MapWindow", "StaticTextTitle", "StaticTextTeam", "StaticTextFaction", "StaticTextColor", "TextEntryMapDisplay",
				 "ButtonSelectMap", "ButtonStart", "StaticTextMapPreview"})
			bindings.BindVisible(menu + name, viewModel.mapSelectOpen, [](bool open) { return !open; });
		for (int index = 0; index < setup::MaxSlots; ++index)
			for (const char *box : {"ComboBoxTeam", "ComboBoxColor", "ComboBoxPlayerTemplate"})
				bindings.BindVisible(menu + box + std::to_string(index), viewModel.mapSelectOpen, [](bool open) { return !open; });
		// The preview, its start spots placed as the map or the preview changes.
		bindings.BindPicture(menu + "MapWindow", m_picture, resolve, frame);
		Watch(viewModel.preview, [this, &document, menu] {
			m_picture.Set(detail::Picture(m_viewModel.preview.Get()));
			detail::PlaceStartButtons(document, menu + "MapWindow", menu + "ButtonMapStartPosition", m_viewModel.preview.Get(), m_viewModel.startSpots.Get());
		});
		Watch(viewModel.startSpots, [this, &document, menu] {
			detail::PlaceStartButtons(document, menu + "MapWindow", menu + "ButtonMapStartPosition", m_viewModel.preview.Get(), m_viewModel.startSpots.Get());
		});
		Watch(viewModel.gameSpeedIsDefault, [this] { m_gameSpeedEnabled.Set(!m_viewModel.gameSpeedIsDefault.Get()); });

		// The map list (SkirmishMapSelectMenu.wnd): its start spots show where players begin, taking no input.
		const std::string list = "SkirmishMapSelectMenu.wnd:";
		mapBindings.BindList(list + "ListboxMap", viewModel.mapItems, viewModel.mapSelected, &viewModel.mapOk);
		mapBindings.BindRowImages(list + "ListboxMap", m_medalImages, resolve);
		Watch(viewModel.mapMedals, [this, &mapDocument, list] {
			// As wide as the gold star (26 at 800 x 600), no wider than the first column; square.
			const wnd::WNDWindow *listBox = mapDocument.Find_Window(list + "ListboxMap");
			const int unit = mapDocument.Creation_Width() > 0 ? mapDocument.Creation_Width() : 800;
			int size = 26 * unit / 800;
			if (listBox != nullptr && !listBox->column_widths.empty())
				size = std::min(size, (listBox->screen_region.right - listBox->screen_region.left) * listBox->column_widths.front() / 100);
			std::vector<std::vector<wnd::ListImageSource>> images;
			for (const std::string &medal : m_viewModel.mapMedals.Get())
				images.push_back({wnd::ListImageSource{medal, medal.empty() ? 0 : size, medal.empty() ? 0 : size}});
			m_medalImages.Set(std::move(images));
		});
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
		const auto placeListSpots = [this, &mapDocument, list] {
			const auto &spots = m_viewModel.mapListStartSpots.Get();
			for (std::size_t spot = 0; spot < m_listSpotShown.size(); ++spot)
				m_listSpotShown[spot].Set(spot < spots.size());
			detail::PlaceStartButtons(mapDocument, list + "WinMapPreview", list + "ButtonMapStartPosition", m_viewModel.mapListPreview.Get(), spots);
		};
		Watch(viewModel.mapListPreview, [this, placeListSpots] {
			m_listPicture.Set(detail::Picture(m_viewModel.mapListPreview.Get()));
			placeListSpots();
		});
		Watch(viewModel.mapListStartSpots, placeListSpots);
	}
	~SkirmishView()
	{
		for (auto &release : m_releases)
			release();
	}
	SkirmishView(const SkirmishView &) = delete;
	SkirmishView &operator=(const SkirmishView &) = delete;

private:
	template<class T, class Handler>
	void Watch(engine::gui::mvvm::Observable<T> &observable, Handler handler)
	{
		const auto id = observable.Subscribe([handler](const T &) { handler(); });
		m_releases.push_back([&observable, id] { observable.Unsubscribe(id); });
	}

	SkirmishViewModel &m_viewModel;
	engine::gui::mvvm::Observable<wnd::PictureSource> m_picture, m_listPicture;
	engine::gui::mvvm::Observable<bool> m_gameSpeedEnabled{false}, m_userMaps{false}, m_never{false};
	engine::gui::mvvm::Observable<std::vector<std::u16string>> m_honorRows;
	engine::gui::mvvm::Observable<std::vector<std::vector<wnd::ListImageSource>>> m_honorImages;
	engine::gui::mvvm::Observable<int> m_noHonor{-1};
	engine::gui::mvvm::Observable<std::vector<std::vector<wnd::ListImageSource>>> m_medalImages;
	std::array<engine::gui::mvvm::Observable<bool>, 8> m_listSpotShown{};
	engine::gui::mvvm::Command m_pickSystemMaps, m_pickUserMaps;
	std::vector<std::function<void()>> m_releases;
};
}
