export module engine.gameplay.rts.emp.resources.emp_strikes;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// This tick's EMP strikes (EMPUpdate::doDisableAttack): each victim its pulse's sphere disabled, the pulse, and how long
// the disable lasts (what its victim's sparks are sized and timed by, in presentation); and the aircraft it killed
// (painted black: TINT_STATUS_DISABLED). Refilled by EmpPulseSystem each tick; not simulation state.
export namespace engine::gameplay
{
struct EmpStrike
{
	ecs::Entity victim;
	ecs::Entity pulse;
	std::uint64_t duration{0};
};

struct EmpStrikes
{
	std::vector<EmpStrike> list;
	std::vector<ecs::Entity> blackened;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::EmpStrikes>
{
	static constexpr std::string_view StableName = "engine.gameplay.emp_strikes";
};
}
