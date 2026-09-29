export module engine.gameplay.rts.radar.resources.player_radar;
import std;

import engine.ecs.system.system;

// Which players have radar this tick (Player::hasRadar), derived from what gives it: counted afresh every tick, so
// nothing to save.
export namespace engine::gameplay
{
struct PlayerRadar
{
	std::vector<std::uint8_t> has; // by player

	bool Has(std::uint32_t player) const noexcept { return player < has.size() && has[player] != 0; }
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::PlayerRadar>
{
	static constexpr std::string_view StableName = "engine.gameplay.player_radar";
};
}
