export module engine.platform.threading.thread;
import std;
export namespace engine::platform
{
class IThread
{
public:
	virtual ~IThread() = default;
	virtual int join() = 0;
};
}
