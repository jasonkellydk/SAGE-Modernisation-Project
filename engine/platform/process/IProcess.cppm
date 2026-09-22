module;
#include <cstddef>
#include <span>
export module engine.platform.process.interface;
export namespace engine::platform
{
class IProcess
{
public:
	virtual ~IProcess() = default;
	virtual bool wait(bool block, int& exit_code) = 0;
	virtual bool terminate(bool force) = 0;
	virtual std::size_t read_stdout(std::span<std::byte> destination) = 0;
	virtual std::size_t write_stdin(std::span<const std::byte> source) = 0;
};
}
