export module games.generalszh.shell.save_load.save_load_view_model;
import std;

export import games.generalszh.shell.model.shell_model;
export import games.generalszh.shell.dialog.message_box_view_model;
export import games.generalszh.shell.save_load.save_game_info;

// The save/load menu (the original's PopupSaveLoad.cpp). From the main menu (SaveLoadMenuFullScreenInit: SLLT_LOAD_ONLY,
// SaveLoad.wnd): the saves in the player's Save folder, newest first, each shown as its description (else its map's
// name), the time and the date; mission saves in green, the others alternating white and lavender. Save is off; Load and
// Delete need a save selected. Delete asks in the layout's own confirm panel, the list and the buttons off meanwhile.
// Escape backs out (and drops the confirm panel).
// In a game (SaveLoadMenuInit: SLLT_SAVE_AND_LOAD, PopupSaveLoad.wnd over the quit menu): the list starts with a new
// save ("GUI:NewSaveGame", 200/200/255); Save on it asks for a description (the campaign's name and mission number,
// else the map's file name), on a save asks to overwrite it (keeping its description); Load asks first; Back closes the
// popup.
export namespace generalszh::shell
{
// The player's save files, as the host reaches them (DI; the menu touches no disk itself).
struct SaveFiles
{
	std::function<std::vector<std::string>()> list;                          // file names in the folder
	std::function<std::optional<std::vector<std::byte>>(const std::string &)> read; // a file's start
	std::function<bool(const std::string &)> remove;                           // deleted
	std::function<void(const std::string &)> load;                             // loaded (empty: not yet possible)
	std::function<std::u16string(const std::string &)> mapName;                // a map label's text (TheGameText->fetch), empty if none
	// In a game: the game saved (GameState::saveGame) to that file (none: a new one) with that description.
	std::function<void(const std::optional<std::string> &, const std::u16string &)> save;
	// setEditDescription's text for a new save.
	std::function<std::u16string()> defaultDescription;
	// closeSaveMenu (the popup hides).
	std::function<void()> close;
};

struct SaveLoadTexts
{
	std::u16string error{u"Error"};
	std::u16string cannotLoad{u"Saved games cannot be loaded yet."};
	std::u16string newSave{u"New Save Game"}; // GUI:NewSaveGame
};

enum class SaveLoadMode : std::uint8_t
{
	LoadOnly,    // SLLT_LOAD_ONLY
	SaveAndLoad, // SLLT_SAVE_AND_LOAD
};

// GameMakeColor in populateSaveGameListbox (0xRRGGBBAA).
inline constexpr std::uint32_t MissionSaveColor = 0xC8FFC8FF, OddSaveColor = 0xFFFFFFFF, EvenSaveColor = 0xAAAAEBFF, NewSaveColor = 0xC8C8FFFF;

class SaveLoadViewModel
{
public:
	SaveLoadViewModel(ShellModel &model, MessageBoxViewModel &messages, SaveFiles files, SaveLoadTexts texts = {}, SaveLoadMode mode = SaveLoadMode::LoadOnly)
		: m_model(model), m_messages(messages), m_files(std::move(files)), m_texts(std::move(texts)), m_mode(mode)
	{
		back.SetAction([this] {
			if (m_mode == SaveLoadMode::SaveAndLoad)
			{
				if (m_files.close)
					m_files.close();
			}
			else
				m_model.Pop();
		});
		escape.SetAction([this] {
			deleteConfirmOpen.Set(false); // Patch 1.01: Escape drops the confirm panel too
			back.Execute();
		});
		load.SetAction([this] {
			if (!SelectedFile())
				return;
			if (m_mode == SaveLoadMode::SaveAndLoad)
				loadConfirmOpen.Set(true); // processLoadButtonPress in a game: it asks first
			else
				Load();
		});
		loadConfirm.SetAction([this] {
			loadConfirmOpen.Set(false);
			if (m_files.close)
				m_files.close();
			Load();
		});
		loadCancel.SetAction([this] { loadConfirmOpen.Set(false); });
		save.SetAction([this] {
			if (m_mode != SaveLoadMode::SaveAndLoad)
				return;
			if (SelectedFile())
				overwriteConfirmOpen.Set(true);
			else
			{
				description.Set(m_files.defaultDescription ? m_files.defaultDescription() : std::u16string{});
				saveDescOpen.Set(true);
			}
		});
		saveDescConfirm.SetAction([this] {
			saveDescOpen.Set(false);
			if (m_files.close)
				m_files.close();
			if (m_files.save)
				m_files.save(SelectedFile(), description.Get());
		});
		saveDescCancel.SetAction([this] { saveDescOpen.Set(false); });
		overwriteConfirm.SetAction([this] {
			overwriteConfirmOpen.Set(false);
			const auto file = SelectedFile();
			if (m_files.close)
				m_files.close();
			if (file && m_files.save)
				m_files.save(file, SelectedDescription());
		});
		overwriteCancel.SetAction([this] { overwriteConfirmOpen.Set(false); });
		deleteGame.SetAction([this] {
			if (SelectedFile())
				deleteConfirmOpen.Set(true);
		});
		deleteConfirm.SetAction([this] {
			if (const auto file = SelectedFile(); file && m_files.remove)
				m_files.remove(*file);
			Populate();
			deleteConfirmOpen.Set(false);
		});
		deleteCancel.SetAction([this] { deleteConfirmOpen.Set(false); });
		m_selected = selected.Subscribe([this](int) { Update(); });
		for (auto *panel : {&deleteConfirmOpen, &loadConfirmOpen, &overwriteConfirmOpen, &saveDescOpen})
			m_panels.push_back({panel, panel->Subscribe([this](bool) {
				menuEnabled.Set(!deleteConfirmOpen.Get() && !loadConfirmOpen.Get() && !overwriteConfirmOpen.Get() && !saveDescOpen.Get());
			})});
	}
	~SaveLoadViewModel()
	{
		selected.Unsubscribe(m_selected);
		for (const auto &[panel, id] : m_panels)
			panel->Unsubscribe(id);
	}
	SaveLoadViewModel(const SaveLoadViewModel &) = delete;
	SaveLoadViewModel &operator=(const SaveLoadViewModel &) = delete;

	// populateSaveGameListbox, as the menu opens and after a delete.
	void Populate()
	{
		std::vector<std::string> names = m_files.list ? m_files.list() : std::vector<std::string>{};
		std::sort(names.begin(), names.end()); // the folder's order (FindFirstFile)
		struct Found
		{
			std::string file;
			SaveGameInfo info;
		};
		std::vector<Found> found;
		for (const std::string &name : names)
		{
			if (name.size() < SaveGameExtension.size() || !CaseBlindEnd(name, SaveGameExtension))
				continue;
			const auto bytes = m_files.read ? m_files.read(name) : std::nullopt;
			if (auto info = bytes ? ReadSaveGameInfo(*bytes) : std::nullopt)
				found.push_back({name, std::move(*info)}); // an unreadable save is left out
		}
		// addGameToAvailableList: each goes ahead of the first older one (the newest on top).
		std::stable_sort(found.begin(), found.end(), [](const Found &a, const Found &b) { return a.info.date.NewerThan(b.info.date); });
		m_rows.clear();
		m_descriptions.clear();
		std::vector<std::u16string> shown;
		std::vector<std::uint32_t> colors;
		if (m_mode != SaveLoadMode::LoadOnly)
		{
			shown.push_back(m_texts.newSave);
			colors.push_back(NewSaveColor);
			m_rows.push_back({});
			m_descriptions.push_back({});
		}
		for (std::size_t count = 0; count < found.size(); ++count)
		{
			const SaveGameInfo &info = found[count].info;
			std::u16string label = info.description;
			if (label.empty())
				label = m_files.mapName ? m_files.mapName(info.mapLabel) : std::u16string{};
			if (label.empty())
				label = Wide(info.mapLabel);
			char time[16], date[16];
			std::snprintf(time, sizeof(time), "%02d:%02d", info.date.hour, info.date.minute);
			std::snprintf(date, sizeof(date), "%04d/%02d/%02d", info.date.year, info.date.month, info.date.day);
			shown.push_back(label + u'\t' + Wide(time) + u'\t' + Wide(date));
			colors.push_back(info.type == SaveFileType::Mission ? MissionSaveColor : (count & 1) != 0 ? OddSaveColor : EvenSaveColor);
			m_rows.push_back(found[count].file);
			m_descriptions.push_back(info.description);
		}
		items.Set(std::move(shown));
		rowColors.Set(std::move(colors));
		selected.Set(m_rows.empty() ? -1 : 0); // GadgetListBoxSetSelected(listbox, 0)
		canSave.Set(m_mode != SaveLoadMode::LoadOnly);
		Update();
	}

	engine::gui::mvvm::Observable<std::vector<std::u16string>> items;
	engine::gui::mvvm::Observable<std::vector<std::uint32_t>> rowColors;
	engine::gui::mvvm::Observable<int> selected{-1};
	engine::gui::mvvm::Observable<bool> canLoad{false}; // Load and Delete (updateMenuActions)
	engine::gui::mvvm::Observable<bool> canSave{false}; // off in SLLT_LOAD_ONLY
	engine::gui::mvvm::Observable<bool> deleteConfirmOpen{false};
	engine::gui::mvvm::Observable<bool> loadConfirmOpen{false};
	engine::gui::mvvm::Observable<bool> overwriteConfirmOpen{false};
	engine::gui::mvvm::Observable<bool> saveDescOpen{false};
	engine::gui::mvvm::Observable<std::u16string> description; // EntryDesc
	engine::gui::mvvm::Observable<bool> menuEnabled{true};      // the list and the buttons, off under a panel
	engine::gui::mvvm::Command back, escape, load, deleteGame, deleteConfirm, deleteCancel, loadConfirm, loadCancel, save, saveDescConfirm,
		saveDescCancel, overwriteConfirm, overwriteCancel;

	// The selected row's save file (none: nothing selected, or the new save).
	std::optional<std::string> SelectedFile() const
	{
		const int row = selected.Get();
		return row >= 0 && static_cast<std::size_t>(row) < m_rows.size() && !m_rows[static_cast<std::size_t>(row)].empty()
			? std::optional(m_rows[static_cast<std::size_t>(row)]) : std::nullopt;
	}

private:
	static std::u16string Wide(const std::string &text) { return std::u16string(text.begin(), text.end()); }

	static bool CaseBlindEnd(const std::string &name, std::string_view end)
	{
		return std::equal(end.begin(), end.end(), name.end() - static_cast<std::ptrdiff_t>(end.size()),
			[](char x, char y) { return (x | 0x20) == (y | 0x20); });
	}

	std::u16string SelectedDescription() const
	{
		const int row = selected.Get();
		return row >= 0 && static_cast<std::size_t>(row) < m_descriptions.size() ? m_descriptions[static_cast<std::size_t>(row)] : std::u16string{};
	}

	void Update() { canLoad.Set(SelectedFile().has_value()); }

	void Load()
	{
		const auto file = SelectedFile();
		if (!file)
			return;
		if (m_files.load)
			m_files.load(*file);
		else
			m_messages.Show(MessageBoxOk(m_texts.error, m_texts.cannotLoad));
	}

	ShellModel &m_model;
	MessageBoxViewModel &m_messages;
	SaveFiles m_files;
	SaveLoadTexts m_texts;
	SaveLoadMode m_mode;
	std::vector<std::string> m_rows;             // each row's file (the new save: empty)
	std::vector<std::u16string> m_descriptions; // each row's description
	engine::gui::mvvm::SubscriptionId m_selected{0};
	std::vector<std::pair<engine::gui::mvvm::Observable<bool> *, engine::gui::mvvm::SubscriptionId>> m_panels;
};
}
