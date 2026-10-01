export module games.generalszh.gameplay.effects.components.bone_fx;
import std;

export import engine.ecs.core.component_registry;

// A BoneFXUpdate's game-logic timers (its FX lists and object creation lists; its particle systems are the
// presentation's, on the client's random stream): the body damage state they run for, and for each of its eight slots
// the tick its FX list and its creation list play next (Off: not at all). `active`: its first update has timed the
// state's slots (initTimes). `timings` counts every initTimes (its first update, each change of damage state) and
// `stops` every stopAllBoneFX, for the presentation's particle systems to follow; `done` latches a topple or collapse
// finishing (the stop happens once, as it does). Simulation state: hashed and checkpointed.
export namespace generalszh::gameplay
{
struct BoneFx
{
	static constexpr std::int64_t Off = -1;

	std::array<std::int64_t, 8> nextFx{Off, Off, Off, Off, Off, Off, Off, Off};
	std::array<std::int64_t, 8> nextOcl{Off, Off, Off, Off, Off, Off, Off, Off};
	std::uint32_t timings{0};
	std::uint32_t stops{0};
	std::uint8_t state{0}; // BODY_PRISTINE 0, DAMAGED 1, REALLYDAMAGED 2, RUBBLE 3
	std::uint8_t active{0};
	std::uint8_t done{0};
	std::uint8_t reserved[5]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::BoneFx>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.bone_fx";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
