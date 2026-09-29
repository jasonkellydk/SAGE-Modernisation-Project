export module Engine.UI.WND.Bindings;
import std;

export import engine.gui.mvvm.observable;
import Engine.UI.WND;
import Engine.UI.WND.Document;
export import Engine.UI.WND.Controls;
export import Engine.UI.WND.Input;

// The View half of MVVM for WND layouts: binds named windows to view-model
// observables (visibility, text) and commands (activation). A view model
// never touches the document; the binder applies its changes and marks the
// document dirty so the owner rebuilds the render list once per frame.
export namespace Engine::UI::WND
{
// What a picture window shows (see WNDPicture), by image names the host resolves.
struct PictureMarkerSource
{
	int x = 0, y = 0; // per 10000 of the picture, from its top left
	std::string image;
	int size = 0;
	bool operator==(const PictureMarkerSource &) const = default;
};

// An image in a list's cell, by name (the host resolves it).
struct ListImageSource
{
	std::string image;
	int width = 0, height = 0; // layout units
	std::uint32_t rgba = 0xFFFFFFFF;
	bool operator==(const ListImageSource &) const = default;
};

struct PictureSource
{
	bool shown = false;  // false: the window's own look
	bool known = false;  // false: no picture (the window's own look, framed)
	std::string image;   // empty: a grey box
	int extentWidth = 1, extentHeight = 1;
	std::vector<PictureMarkerSource> markers;
	bool operator==(const PictureSource &) const = default;
};

class WNDBindings
{
public:
	explicit WNDBindings(WNDDocument &document) noexcept : m_document(document) {}
	WNDBindings(const WNDBindings &) = delete;
	WNDBindings &operator=(const WNDBindings &) = delete;

	~WNDBindings()
	{
		for (auto &release : m_releases)
			release();
	}

	// Window shown while `visible(value)` holds.
	template<class T, class Predicate>
	bool BindVisible(std::string_view window, engine::gui::mvvm::Observable<T> &source, Predicate visible)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = source.Subscribe([this, name, visible](const T &value) {
			m_document.Set_Window_Flag(name, WindowFlag::Hidden, !visible(value));
			m_dirty = true;
		});
		m_releases.push_back([&source, id] { source.Unsubscribe(id); });
		return true;
	}

	bool BindVisible(std::string_view window, engine::gui::mvvm::Observable<bool> &source)
	{
		return BindVisible(window, source, [](bool value) { return value; });
	}

	bool BindText(std::string_view window, engine::gui::mvvm::Observable<std::u16string> &source)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = source.Subscribe([this, name](const std::u16string &text) {
			if (WNDWindow *target = m_document.Find_Window(name))
				target->text = text;
			m_dirty = true;
		});
		m_releases.push_back([&source, id] { source.Unsubscribe(id); });
		return true;
	}

	// The window's enabled text colour (winSetEnabledTextColors) follows `argb` (0xAARRGGBB; 0: as the layout has it).
	bool BindTextColor(std::string_view window, engine::gui::mvvm::Observable<std::uint32_t> &argb)
	{
		WNDWindow *found = m_document.Find_Window(window);
		if (found == nullptr)
			return Missing(window);
		const std::string name(window);
		const Graphics::Color2D authored = found->text_styles[0].color;
		const auto id = argb.Subscribe([this, name, authored](std::uint32_t value) {
			if (WNDWindow *target = m_document.Find_Window(name))
				target->text_styles[0].color = value == 0 ? authored
					: Graphics::Color2D{static_cast<float>((value >> 16) & 0xFF) / 255.0f, static_cast<float>((value >> 8) & 0xFF) / 255.0f,
						  static_cast<float>(value & 0xFF) / 255.0f, static_cast<float>((value >> 24) & 0xFF) / 255.0f};
			m_dirty = true;
		});
		m_releases.push_back([&argb, id] { argb.Unsubscribe(id); });
		return true;
	}

	// A check box shows `source` and sets it when clicked.
	bool BindChecked(std::string_view window, engine::gui::mvvm::Observable<bool> &source)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = source.Subscribe([this, name](bool value) {
			if (WNDWindow *target = m_document.Find_Window(name))
				target->checked = value;
			m_dirty = true;
		});
		m_releases.push_back([&source, id] { source.Unsubscribe(id); });
		m_checks.insert_or_assign(name, &source);
		return true;
	}

	// A progress bar shows `percent` (0..100: GadgetProgressBarSetProgress).
	bool BindProgress(std::string_view window, engine::gui::mvvm::Observable<int> &percent)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = percent.Subscribe([this, name](const int &value) {
			if (WNDWindow *target = m_document.Find_Window(name))
				target->progress = (std::clamp)(value, 0, 100);
			m_dirty = true;
		});
		m_releases.push_back([&percent, id] { percent.Unsubscribe(id); });
		return true;
	}

	// A window status flag follows `on` (winSetStatus / winClearStatus: ALWAYS_COLOR and the like).
	bool BindFlag(std::string_view window, WindowFlag flag, engine::gui::mvvm::Observable<bool> &on)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = on.Subscribe([this, name, flag](bool value) {
			m_document.Set_Window_Flag(name, flag, value);
			m_dirty = true;
		});
		m_releases.push_back([&on, id] { on.Unsubscribe(id); });
		return true;
	}

	// A slider shows `source` (clamped to its range) and sets it when moved.
	bool BindSlider(std::string_view window, engine::gui::mvvm::Observable<int> &source)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = source.Subscribe([this, name](int value) {
			if (WNDWindow *target = m_document.Find_Window(name))
				target->position = value < target->minimum ? target->minimum : value > target->maximum ? target->maximum : value;
			m_dirty = true;
		});
		m_releases.push_back([&source, id] { source.Unsubscribe(id); });
		m_sliders.insert_or_assign(name, &source);
		return true;
	}

	// A combo box lists `items` and shows `selected` (-1: none), setting it when an item is picked.
	bool BindComboBox(std::string_view window, engine::gui::mvvm::Observable<std::vector<std::u16string>> &items, engine::gui::mvvm::Observable<int> &selected)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto listed = items.Subscribe([this, name](const std::vector<std::u16string> &entries) {
			if (WNDWindow *target = m_document.Find_Window(name))
				target->entries = entries;
			m_dirty = true;
		});
		const auto chosen = selected.Subscribe([this, name](int index) {
			if (WNDWindow *target = m_document.Find_Window(name))
			{
				target->selected = index;
				// A list keeps a row chosen past its last shown one in view, near the middle
				// (populateMapListbox's GadgetListBoxSetTopVisibleEntry).
				if (target->type == WindowType::ListBox && index >= 0)
				{
					const int rows = List_Visible_Rows(*target);
					if (index >= target->list_top + rows)
						target->list_top = (std::max)(0, index - (std::max)(1, rows / 2));
					else if (index < target->list_top)
						target->list_top = index;
				}
			}
			m_dirty = true;
		});
		m_releases.push_back([&items, listed] { items.Unsubscribe(listed); });
		m_releases.push_back([&selected, chosen] { selected.Unsubscribe(chosen); });
		m_sliders.insert_or_assign(name, &selected); // picks arrive as indices
		return true;
	}

	// A list box lists `items` (columns split by tabs) and shows `selected`, setting it as rows are
	// picked; a double click runs `open` (if any).
	bool BindList(std::string_view window, engine::gui::mvvm::Observable<std::vector<std::u16string>> &items, engine::gui::mvvm::Observable<int> &selected,
		engine::gui::mvvm::Command *open = nullptr)
	{
		if (!BindComboBox(window, items, selected))
			return false;
		if (open != nullptr)
			m_doubleClicks.insert_or_assign(std::string(window), open);
		return true;
	}

	// Each row of a list box in its own text colour (0xRRGGBBAA; 0: the list's).
	bool BindRowColors(std::string_view window, engine::gui::mvvm::Observable<std::vector<std::uint32_t>> &colors)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = colors.Subscribe([this, name](const std::vector<std::uint32_t> &rows) {
			if (WNDWindow *target = m_document.Find_Window(name))
				target->entry_colors = rows;
			m_dirty = true;
		});
		m_releases.push_back([&colors, id] { colors.Unsubscribe(id); });
		return true;
	}

	// A picture window shows `source` letterboxed, images resolved by `resolve` (a texture or a
	// mapped image name), framed by `frame`.
	bool BindPicture(std::string_view window, engine::gui::mvvm::Observable<PictureSource> &source, std::function<ImageRef(std::string_view)> resolve,
		WNDFrame frame = {})
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = source.Subscribe([this, name, resolve = std::move(resolve), frame](const PictureSource &shown) {
			WNDWindow *target = m_document.Find_Window(name);
			if (target == nullptr)
				return;
			if (!shown.shown)
				target->picture.reset();
			else
			{
				WNDPicture picture;
				picture.frame = frame;
				picture.extent_width = shown.known ? shown.extentWidth : 0;
				picture.extent_height = shown.known ? shown.extentHeight : 0;
				if (!shown.image.empty() && resolve)
					picture.image = resolve(shown.image);
				for (const PictureMarkerSource &marker : shown.markers)
					picture.markers.push_back({marker.x, marker.y, resolve ? resolve(marker.image) : ImageRef{}, marker.size});
				target->picture = std::move(picture);
			}
			m_dirty = true;
		});
		m_releases.push_back([&source, id] { source.Unsubscribe(id); });
		return true;
	}

	// The pointer coming onto the window runs `entering`, going off it `leaving` (GBM_MOUSE_ENTERING / LEAVING).
	bool BindHover(std::string_view window, engine::gui::mvvm::Command &entering, engine::gui::mvvm::Command *leaving = nullptr)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		m_hovers.insert_or_assign(std::string(window), std::pair{&entering, leaving});
		return true;
	}

	// The window shows the image `source` names (its first enabled image; empty: none).
	bool BindImage(std::string_view window, engine::gui::mvvm::Observable<std::string> &source, std::function<ImageRef(std::string_view)> resolve)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = source.Subscribe([this, name, resolve = std::move(resolve)](const std::string &image) {
			if (WNDWindow *target = m_document.Find_Window(name))
			{
				WNDDrawCell &cell = target->draw_states[0].cells[0];
				cell.image_name = image;
				cell.image = image.empty() || !resolve ? ImageRef{} : resolve(image);
			}
			m_dirty = true;
		});
		m_releases.push_back([&source, id] { source.Unsubscribe(id); });
		return true;
	}

	// A push button's clock follows `perMille` (0..1000, shown as whole percent as the original's Int percent), swept
	// in `color`; `remaining`: the inverse clock (the part still to go darkened).
	bool BindClock(std::string_view window, engine::gui::mvvm::Observable<std::uint32_t> &perMille, Graphics::Color2D color, bool remaining)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = perMille.Subscribe([this, name, color, remaining](const std::uint32_t &value) {
			if (WNDWindow *target = m_document.Find_Window(name))
			{
				target->clock = true;
				target->clock_percent = static_cast<int>((std::min)(value, 1000u) / 10u);
				target->clock_remaining = remaining;
				target->clock_color = color;
			}
			m_dirty = true;
		});
		m_releases.push_back([&perMille, id] { perMille.Unsubscribe(id); });
		return true;
	}

	// A RIGHT_CLICK push button runs `command` when right-clicked.
	bool BindRightCommand(std::string_view window, engine::gui::mvvm::Command &command)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		m_rightCommands.insert_or_assign(std::string(window), &command);
		return true;
	}

	// A list showing `lines` with nothing to select, kept scrolled to the newest (a chat window).
	bool BindLog(std::string_view window, engine::gui::mvvm::Observable<std::vector<std::u16string>> &lines)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = lines.Subscribe([this, name](const std::vector<std::u16string> &rows) {
			if (WNDWindow *target = m_document.Find_Window(name))
			{
				target->entries = rows;
				target->selected = -1;
				target->list_top = List_Last_Top(*target);
			}
			m_dirty = true;
		});
		m_releases.push_back([&lines, id] { lines.Unsubscribe(id); });
		return true;
	}

	// A list's rows' cell images (by row, then column), resolved by `resolve`.
	bool BindRowImages(std::string_view window, engine::gui::mvvm::Observable<std::vector<std::vector<ListImageSource>>> &images,
		std::function<ImageRef(std::string_view)> resolve)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = images.Subscribe([this, name, resolve = std::move(resolve)](const std::vector<std::vector<ListImageSource>> &rows) {
			WNDWindow *target = m_document.Find_Window(name);
			if (target == nullptr)
				return;
			target->entry_images.clear();
			for (const auto &row : rows) {
				std::vector<WNDListImage> cells;
				for (const ListImageSource &cell : row)
					cells.push_back({cell.image.empty() || !resolve ? ImageRef{} : resolve(cell.image), cell.width, cell.height, cell.rgba});
				target->entry_images.push_back(std::move(cells));
			}
			m_dirty = true;
		});
		m_releases.push_back([&images, id] { images.Unsubscribe(id); });
		return true;
	}

	// Window enabled while `enabled` holds (winEnable): disabled, it and all inside it take no input.
	bool BindEnabled(std::string_view window, engine::gui::mvvm::Observable<bool> &enabled)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		const std::string name(window);
		const auto id = enabled.Subscribe([this, name](bool value) {
			m_document.Set_Window_Flag(name, WindowFlag::Enabled, value);
			m_dirty = true;
		});
		m_releases.push_back([&enabled, id] { enabled.Unsubscribe(id); });
		return true;
	}

	// A text entry shows `text` and sets it as the player types; Enter runs `done` (if any).
	bool BindEntry(std::string_view window, engine::gui::mvvm::Observable<std::u16string> &text, engine::gui::mvvm::Command *done = nullptr)
	{
		if (!BindText(window, text))
			return false;
		m_entries.insert_or_assign(std::string(window), std::pair{&text, done});
		return true;
	}

	// Carries what the pointer did to the view model: clicks run commands,
	// check boxes and sliders set their values. False if nothing is bound.
	bool Apply(const WNDInputEvent &event)
	{
		switch (event.kind)
		{
		case WNDInputEvent::Kind::Clicked: return Activate(event.window);
		case WNDInputEvent::Kind::Hovered:
		{
			bool handled = false;
			if (const auto found = m_hovers.find(event.left); found != m_hovers.end() && found->second.second != nullptr)
				handled = found->second.second->Execute() || handled;
			if (const auto found = m_hovers.find(event.window); found != m_hovers.end())
				handled = found->second.first->Execute() || handled;
			return handled;
		}
		case WNDInputEvent::Kind::RightClicked:
			if (const auto found = m_rightCommands.find(event.window); found != m_rightCommands.end())
			{
				found->second->Execute();
				return true;
			}
			return false;
		case WNDInputEvent::Kind::Toggled:
			if (const auto found = m_checks.find(event.window); found != m_checks.end())
			{
				found->second->Set(event.value != 0);
				return true;
			}
			return false;
		case WNDInputEvent::Kind::TextChanged:
			if (const auto found = m_entries.find(event.window); found != m_entries.end())
			{
				found->second.first->Set(event.text);
				return true;
			}
			return false;
		case WNDInputEvent::Kind::EditDone:
			if (const auto found = m_entries.find(event.window); found != m_entries.end() && found->second.second != nullptr)
				return found->second.second->Execute();
			return false;
		case WNDInputEvent::Kind::ListDoubleClicked:
			if (const auto found = m_doubleClicks.find(event.window); found != m_doubleClicks.end())
				return found->second->Execute();
			return false;
		case WNDInputEvent::Kind::ListSelected:
		case WNDInputEvent::Kind::ComboSelected:
		case WNDInputEvent::Kind::SliderMoved:
			if (const auto found = m_sliders.find(event.window); found != m_sliders.end())
			{
				found->second->Set(event.value);
				return true;
			}
			return false;
		default: return false;
		}
	}

	// Activating the window (a click, a key) executes the command.
	bool BindCommand(std::string_view window, engine::gui::mvvm::Command &command)
	{
		if (m_document.Find_Window(window) == nullptr)
			return Missing(window);
		m_commands.insert_or_assign(std::string(window), &command);
		return true;
	}

	// Routes an activation from input handling; false if nothing is bound.
	bool Activate(std::string_view window)
	{
		const auto found = m_commands.find(window);
		return found != m_commands.end() && found->second->Execute();
	}

	// True once after any bound change; the owner then rebuilds its render list.
	bool TakeDirty() noexcept { return std::exchange(m_dirty, false); }

	// Window names a binding referred to that the layout does not contain.
	const std::vector<std::string> &MissingWindows() const noexcept { return m_missing; }

private:
	bool Missing(std::string_view window)
	{
		m_missing.emplace_back(window);
		return false;
	}

	WNDDocument &m_document;
	std::map<std::string, engine::gui::mvvm::Command *, std::less<>> m_commands;
	std::map<std::string, engine::gui::mvvm::Observable<bool> *, std::less<>> m_checks;
	std::map<std::string, engine::gui::mvvm::Observable<int> *, std::less<>> m_sliders;
	std::map<std::string, std::pair<engine::gui::mvvm::Observable<std::u16string> *, engine::gui::mvvm::Command *>, std::less<>> m_entries;
	std::map<std::string, engine::gui::mvvm::Command *, std::less<>> m_rightCommands;
	std::map<std::string, std::pair<engine::gui::mvvm::Command *, engine::gui::mvvm::Command *>, std::less<>> m_hovers;
	std::map<std::string, engine::gui::mvvm::Command *, std::less<>> m_doubleClicks;
	std::vector<std::function<void()>> m_releases;
	std::vector<std::string> m_missing;
	bool m_dirty{true};
};
}
