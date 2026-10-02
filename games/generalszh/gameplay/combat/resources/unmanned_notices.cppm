export module games.generalszh.gameplay.combat.resources.unmanned_notices;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// Object::setDisabledUntil(DISABLED_UNMANNED) calls this tick, each one (a thing already unmanned is set so again): a
// KILL_PILOT hit on a vehicle, a neutron blast over one, a script's SET_UNMANNED: what and of which definition (the
// presentation's SplatterVehiclePilotsBrain where it is, not for a drone). Cleared as a tick begins; not saved.
export namespace generalszh::gameplay
{
struct UnmannedNotice
{
	ecs::Entity entity;
	std::uint32_t definition{0};
};

struct UnmannedNotices
{
	std::vector<UnmannedNotice> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::UnmannedNotices>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.unmanned_notices";
};
}
