export module engine.platform.threading.mutex;
export namespace engine::platform
{
class IMutex
{
public:
	virtual ~IMutex() = default;
	virtual void lock() = 0;
	[[nodiscard]] virtual bool try_lock() = 0;
	virtual void unlock() noexcept = 0;
};
}
