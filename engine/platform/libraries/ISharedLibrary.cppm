export module engine.platform.libraries.library;
import std;
export namespace engine::platform
{
class ISharedLibrary
{
public:
	virtual ~ISharedLibrary() = default;
	[[nodiscard]] virtual void* symbol(const char* name) const noexcept = 0;
};
}
