export module engine.platform.libraries.library;
export namespace engine::platform
{
class ISharedLibrary
{
public:
	virtual ~ISharedLibrary() = default;
	[[nodiscard]] virtual void* symbol(const char* name) const noexcept = 0;
};
}
