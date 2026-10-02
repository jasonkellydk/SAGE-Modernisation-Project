export module games.generalszh.gameplay.stealth.algorithms.supply_stealth;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import engine.gameplay.rts.stealth.components.stealth;
import engine.gameplay.common.spatial.components.targetable;
import engine.gameplay.rts.stealth.systems.stealth_system;

// GrantTemporaryStealth (SupplyCenterDockUpdate::action, SupplyCenterProductionExitUpdate::exitObjectViaDoor): a supply
// centre that is itself stealthed (OBJECT_STATUS_STEALTHED) grants a harvester its ticks of stealth, only to the default
// stealth (its StealthUpdate's grant is already a temporary one, or it may not stealth otherwise: the GPS scrambler's
// or an innate stealth take precedence). One without a StealthUpdate gets none (the exit's retail null dereference taken
// as none).
export namespace generalszh::gameplay
{
inline bool TemporaryStealthApplies(const engine::gameplay::Stealth *center, const engine::gameplay::Stealth *harvester) noexcept
{
	namespace gp = engine::gameplay;
	return center != nullptr && center->Has(gp::stealth_flag::Stealthed) && harvester != nullptr &&
		(gp::TemporaryGrant(*harvester) || !harvester->Has(gp::stealth_flag::CanStealth));
}

// SupplyCenterProductionExitUpdate::exitObjectViaDoor: the unit brought out of `center` takes its grant at once.
inline void GrantExitStealth(GameWorld &game, ecs::Entity center, ecs::Entity unit, std::uint32_t ticks)
{
	namespace gp = engine::gameplay;
	if (ticks == 0 || !game.world.IsAlive(center) || !game.world.IsAlive(unit))
		return;
	gp::Stealth *stealth = game.world.Get<gp::Stealth>(unit);
	if (!TemporaryStealthApplies(game.world.Get<gp::Stealth>(center), stealth) || !gp::ReceiveGrant(*stealth, game.tick, ticks))
		return;
	if (gp::Targetable *targetable = game.world.Get<gp::Targetable>(unit))
		gp::stealth_detail::MarkClasses(*stealth, *targetable);
}
}
