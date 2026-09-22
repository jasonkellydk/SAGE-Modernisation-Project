module;
#include <memory>
#include <string>
#include <vector>
export module engine.platform.process;
export import engine.platform.process.interface;
export namespace engine::platform
{
struct ProcessConfig
{
	std::vector<std::string> arguments;
	std::string working_directory;
	bool capture_standard_io{};
};
class IProcessService
{
public:
	virtual ~IProcessService() = default;
	[[nodiscard]] virtual std::unique_ptr<IProcess> create(const ProcessConfig& config) = 0;
};
}
