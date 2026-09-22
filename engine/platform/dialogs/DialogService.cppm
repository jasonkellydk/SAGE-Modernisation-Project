module;
#include <functional>
#include <string>
#include <vector>
export module engine.platform.dialogs;
import engine.platform.core.types;

export namespace engine::platform
{
enum class MessageBoxKind { information, warning, error, question };
struct MessageBoxButton { int id{}; std::string label; bool is_default{}; };
struct FileFilter { std::string name; std::string pattern; };
enum class FileDialogKind { open_file, save_file, open_folder };
using FileDialogCallback = std::function<void(bool, std::vector<std::string>)>;

class IDialogService
{
public:
	virtual ~IDialogService() = default;
	virtual int show_message_box(const std::string& title, const std::string& message,
		MessageBoxKind kind, const std::vector<MessageBoxButton>& buttons, WindowId parent = 0) = 0;
	virtual bool show_file_dialog(FileDialogKind kind, const std::string& title,
		const std::string& initial_location, const std::vector<FileFilter>& filters,
		bool allow_many, FileDialogCallback callback, WindowId parent = 0) = 0;
};
}
