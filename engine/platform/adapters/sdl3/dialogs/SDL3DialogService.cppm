module;
#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>
export module engine.platform.adapters.sdl3.dialogs;
import engine.platform;
import engine.platform.adapters.sdl3.dialogs.context;

export namespace engine::platform::sdl3
{
class SDL3DialogService final : public IDialogService
{
	static void SDLCALL complete(void* raw, const char* const* files, int)
	{
		std::unique_ptr<SDL3FileDialogContext> context(static_cast<SDL3FileDialogContext*>(raw));
		if (!context->callback) return;
		if (!files) { context->callback(false, {}); return; }
		std::vector<std::string> selected; for (auto p = files; *p; ++p) selected.emplace_back(*p);
		context->callback(true, std::move(selected));
	}
public:
	int show_message_box(const std::string& title, const std::string& message,
		MessageBoxKind kind, const std::vector<MessageBoxButton>& buttons, WindowId parent) override
	{
		std::vector<SDL_MessageBoxButtonData> native; native.reserve(buttons.size());
		for (const auto& b : buttons) native.push_back({b.is_default ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0, b.id, b.label.c_str()});
		Uint32 flags = SDL_MESSAGEBOX_INFORMATION;
		if (kind == MessageBoxKind::warning) flags = SDL_MESSAGEBOX_WARNING;
		if (kind == MessageBoxKind::error) flags = SDL_MESSAGEBOX_ERROR;
		SDL_MessageBoxData data{}; data.flags = flags; data.window = parent ? SDL_GetWindowFromID(parent) : nullptr;
		data.title = title.c_str(); data.message = message.c_str(); data.numbuttons = static_cast<int>(native.size()); data.buttons = native.data();
		int selected = -1; return SDL_ShowMessageBox(&data, &selected) ? selected : -1;
	}
	bool show_file_dialog(FileDialogKind kind, const std::string& title, const std::string& location,
		const std::vector<FileFilter>& filters, bool allow_many, FileDialogCallback callback, WindowId parent) override
	{
		auto context = std::make_unique<SDL3FileDialogContext>(); context->filters = filters; context->callback = std::move(callback);
		for (const auto& f : context->filters) context->native_filters.push_back({f.name.c_str(), f.pattern.c_str()});
		const SDL_PropertiesID props = SDL_CreateProperties(); if (!props) return false;
		if (parent) SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, SDL_GetWindowFromID(parent));
		if (!location.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str());
		if (!title.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
		if (!context->native_filters.empty()) {
			SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, context->native_filters.data());
			SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, static_cast<Sint64>(context->native_filters.size()));
		}
		SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, allow_many);
		const auto type = kind == FileDialogKind::open_file ? SDL_FILEDIALOG_OPENFILE :
			kind == FileDialogKind::save_file ? SDL_FILEDIALOG_SAVEFILE : SDL_FILEDIALOG_OPENFOLDER;
		auto* context_ptr = context.release();
		SDL_ShowFileDialogWithProperties(type, complete, context_ptr, props);
		SDL_DestroyProperties(props);
		return true;
	}
};
}
