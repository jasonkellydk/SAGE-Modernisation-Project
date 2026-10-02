export module engine.gameplay.common.status.components.ai_activity;
import std;

export import engine.ecs.core.component_registry;

// What an AI is at on its own account (the original's AIUpdateInterface state a unit's own behaviours read):
//   busy: it does something that is no order (the AI_BUSY state, such as packing or unpacking for an ability): it is
//   not idle;
//   usingAbility: it carries out an ability (OBJECT_STATUS_IS_USING_ABILITY);
//   commanded: its last order came from outside its AI, a player or a script (getLastCommandSource() != CMD_FROM_AI):
//   the behaviours that give it orders of its own (abilities, command button hunts) stop; its own AI's next order clears
//   it.
//   fromPlayer: that last order came from its player (getLastCommandSource() == CMD_FROM_PLAYER).
// Busy or using an ability, it picks no targets of its own (getNextMoodTarget returns none while using an ability; a busy
// AI runs no idle state). Simulation state: checkpointed.
export namespace engine::gameplay
{
struct AiActivity
{
	std::uint8_t busy{0};
	std::uint8_t usingAbility{0};
	std::uint8_t commanded{0};
	std::uint8_t fromPlayer{0};
	std::uint8_t reserved[4]{};

	bool Occupied() const noexcept { return busy != 0 || usingAbility != 0; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::AiActivity>
{
	static constexpr std::string_view StableName = "engine.gameplay.ai_activity";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
