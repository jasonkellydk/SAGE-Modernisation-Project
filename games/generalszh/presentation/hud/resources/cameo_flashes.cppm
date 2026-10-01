export module games.generalszh.presentation.hud.resources.cameo_flashes;
import std;

import engine.ecs.system.system;

// The command buttons scripts make flash (CAMEO_FLASH: the original's CommandButton m_flashCount, ControlBar m_flash and
// each command window's WIN_STATUS_FLASHING): how many flash halves each button (by name, a per-definition count as the
// original's is on the shared CommandButton) has left, whether the control bar still looks for flashes, and whether
// each command window (ButtonCommand01..18) shows its flash now.
export namespace generalszh::presentation
{
struct CameoFlashes
{
	static constexpr std::size_t Windows = 18; // MAX_COMMANDS_PER_SET
	std::map<std::string, std::int32_t, std::less<>> counts;
	bool flash{false};
	std::array<bool, Windows> flashing{};
	std::uint64_t lastTick{~std::uint64_t{0}};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::CameoFlashes>
{
	static constexpr std::string_view StableName = "generalszh.presentation.cameo_flashes";
};
}
