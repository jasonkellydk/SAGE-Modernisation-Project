export module engine.platform.libraries;
import std;
export import engine.platform.libraries.library;
export namespace engine::platform
{
class ISharedLibraryService
{
public:
	virtual ~ISharedLibraryService() = default;
	[[nodiscard]] virtual std::unique_ptr<ISharedLibrary> load(const std::string& path) = 0;
};
}
