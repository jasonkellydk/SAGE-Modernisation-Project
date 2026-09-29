export module games.generalszh.shell.replay.replay_menu_view_model;
import std;

export import games.generalszh.shell.model.shell_model;
export import games.generalszh.shell.dialog.message_box_view_model;
export import games.generalszh.shell.replay.replay_header;

// The replay menu (the original's ReplayMenu.cpp, without windows): the
// replays in the player's Replays folder, by file name, each shown as its
// name (the last game's as "Last Replay"), its time and date, the game
// version and its map; the first selected. Delete and Copy (to the Desktop)
// ask first; nothing selected, they say so. Load plays the replay.
export namespace generalszh::shell
{
// The player's replay files, as the host reaches them (DI; the menu touches no disk itself).
struct ReplayFiles
{
	std::function<std::vector<std::string>()> list;                          // file names in the folder
	std::function<std::optional<std::vector<std::byte>>(const std::string &)> read; // a file's bytes
	std::function<bool(const std::string &)> remove;                           // deleted
	std::function<bool(const std::string &)> copyToDesktop;                    // copied beside the player's desktop
	std::function<void(const std::string &)> play;                             // played back (empty: not yet possible)
};

// The texts it shows (Generals.csf), looked up by the host.
struct ReplayTexts
{
	std::u16string lastReplay{u"Last Replay"};
	std::u16string noFileSelected{u"No File Selected"}, pleaseSelectAFile{u"Please select a file."};
	std::u16string deleteFile{u"Delete File"}, areYouSureDelete{u"Are you sure you want to delete this file?"};
	std::u16string copyReplay{u"Copy Replay"}, areYouSureCopy{u"Are you sure you want to copy this replay to your desktop?"};
	std::u16string error{u"Error"};
	std::u16string cannotPlay{u"Replays cannot be played back yet."};
};

inline constexpr const char *LastReplayFile = "00000000.rep"; // Recorder.cpp lastReplayFileName + ".rep"

class ReplayMenuViewModel
{
public:
	ReplayMenuViewModel(ShellModel &model, MessageBoxViewModel &messages, ReplayFiles files, ReplayTexts texts)
		: m_model(model), m_messages(messages), m_files(std::move(files)), m_texts(std::move(texts))
	{
		back.SetAction([this] { m_model.Pop(); });
		load.SetAction([this] { Load(); });
		deleteReplay.SetAction([this] {
			if (Selected())
				m_messages.Show(MessageBoxYesNo(m_texts.deleteFile, m_texts.areYouSureDelete, [this] { Delete(); }));
		});
		copyReplay.SetAction([this] {
			if (Selected())
				m_messages.Show(MessageBoxYesNo(m_texts.copyReplay, m_texts.areYouSureCopy, [this] { Copy(); }));
		});
	}

	// PopulateReplayFileListbox, as the menu opens (and after a delete).
	void Populate()
	{
		std::vector<std::string> names = m_files.list ? m_files.list() : std::vector<std::string>{};
		std::sort(names.begin(), names.end()); // the original's FilenameList (a sorted set)
		m_rows.clear();
		std::vector<std::u16string> shown;
		for (const std::string &name : names)
		{
			if (name.size() < 4 || name.substr(name.size() - 4) != ".rep")
				continue;
			const auto bytes = m_files.read ? m_files.read(name) : std::nullopt;
			const auto header = bytes ? ReadReplayHeader(*bytes) : std::nullopt;
			if (!header)
				continue; // an unreadable replay is left out
			// columns: name, time and date, version, map
			std::u16string row = CaseBlindEqual(name, LastReplayFile) ? m_texts.lastReplay : Wide(name.substr(0, name.size() - 4));
			char when[32];
			std::snprintf(when, sizeof(when), "%02d:%02d %04d/%02d/%02d", header->date.hour, header->date.minute, header->date.year, header->date.month,
				header->date.day);
			row += u'\t' + Wide(when) + u'\t' + header->version + u'\t' + Wide(header->Map());
			shown.push_back(std::move(row));
			m_rows.push_back(name);
		}
		items.Set(std::move(shown));
		selected.Set(m_rows.empty() ? -1 : 0); // GadgetListBoxSetSelected(listbox, 0)
	}

	engine::gui::mvvm::Observable<std::vector<std::u16string>> items;
	engine::gui::mvvm::Observable<int> selected{-1};
	engine::gui::mvvm::Command back, load, deleteReplay, copyReplay;

	// The file of the selected row, if any.
	std::optional<std::string> SelectedFile() const
	{
		const int row = selected.Get();
		return row >= 0 && static_cast<std::size_t>(row) < m_rows.size() ? std::optional(m_rows[static_cast<std::size_t>(row)]) : std::nullopt;
	}

private:
	static std::u16string Wide(const std::string &text) { return std::u16string(text.begin(), text.end()); }

	static bool CaseBlindEqual(const std::string &a, const std::string &b)
	{
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return (x | 0x20) == (y | 0x20); });
	}

	bool Selected()
	{
		if (SelectedFile())
			return true;
		m_messages.Show(MessageBoxOk(m_texts.noFileSelected, m_texts.pleaseSelectAFile));
		return false;
	}

	void Load()
	{
		if (!Selected())
			return;
		if (m_files.play)
			m_files.play(*SelectedFile());
		else
			m_messages.Show(MessageBoxOk(m_texts.error, m_texts.cannotPlay));
	}

	void Delete()
	{
		if (const auto file = SelectedFile(); file && m_files.remove && !m_files.remove(*file))
			m_messages.Show(MessageBoxOk(m_texts.error, u"The file could not be deleted."));
		Populate();
	}

	void Copy()
	{
		if (const auto file = SelectedFile(); file && m_files.copyToDesktop && !m_files.copyToDesktop(*file))
			m_messages.Show(MessageBoxOk(m_texts.error, u"The file could not be copied."));
	}

	ShellModel &m_model;
	MessageBoxViewModel &m_messages;
	ReplayFiles m_files;
	ReplayTexts m_texts;
	std::vector<std::string> m_rows; // each row's file
};
}
