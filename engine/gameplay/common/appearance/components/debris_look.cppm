export module engine.gameplay.common.appearance.components.debris_look;
import std;

export import engine.ecs.core.component_registry;

// How a piece of debris is drawn beyond its model (the original's
// DebrisDrawInterface, set by an object creation list): the animations it
// plays as it is thrown, flies and lands (ids into the game's model and
// animation names, 0 none), the effect it plays as it lands, whether it
// holds its flying animation's first frame on landing ("STOP"), and whether
// it shows its player's colour, and the particle system riding on it (an id into the game's names, 0 none).
export namespace engine::gameplay
{
struct DebrisLook
{
	static constexpr std::uint32_t NoEffect = 0xFFFFFFFFu;
	std::array<std::uint32_t, 3> animations{}; // initial, flying, final
	std::uint32_t finalEffect{NoEffect};
	std::uint8_t finalStop{0};
	std::uint8_t playerColor{0};
	std::uint16_t reserved{0};
	std::uint32_t particleSystem{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DebrisLook>
{
	static constexpr std::string_view StableName = "engine.gameplay.debris_look";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
