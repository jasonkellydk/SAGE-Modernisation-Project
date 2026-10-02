export module games.generalszh.gameplay.scripts.algorithms.object_edits;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.harvesting.components.resource_store;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.status.components.script_status;
import engine.gameplay.common.appearance.components.indicator_color;

// What the scripts set on objects directly.
export namespace generalszh::gameplay
{
// ScriptActions::changeObjectPanelFlagForSingleObject (UNIT_ / TEAM_AFFECT_OBJECT_PANEL_FLAGS) and the map properties
// that set the same (Object::updateObjValuesFromMapProperties): "Enabled" (off: DISABLED_SCRIPT_DISABLED), "Powered" (off:
// DISABLED_SCRIPT_UNDERPOWERED), "Indestructible" (the body), "Unsellable", "Selectable" (setSelectable), "AI Recruitable",
// "Player Targetable". Another name: nothing.
inline void SetObjectPanelFlag(GameWorld &game, ecs::Entity entity, std::string_view flag, bool value)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.IsAlive(entity))
		return;
	const auto disable = [&](std::uint32_t type, bool on) {
		if (!world.Has<gp::Disabled>(entity))
		{
			if (!on)
				return;
			world.Add<gp::Disabled>(entity);
		}
		auto &mask = world.Get<gp::Disabled>(entity)->mask;
		mask = on ? (mask | type) : (mask & ~type);
	};
	const auto status = [&](std::uint8_t bit, bool on) {
		if (!world.Has<gp::ScriptStatus>(entity))
			world.Add<gp::ScriptStatus>(entity);
		world.Get<gp::ScriptStatus>(entity)->Set(bit, on);
	};
	if (flag == "Enabled")
		disable(gp::disabled_type::ScriptDisabled, !value);
	else if (flag == "Powered")
		disable(gp::disabled_type::ScriptUnderpowered, !value);
	else if (flag == "Indestructible")
	{
		if (auto *health = world.Get<gp::Health>(entity))
			health->indestructible = value;
	}
	else if (flag == "Unsellable")
		status(gp::script_status::Unsellable, value);
	else if (flag == "Selectable")
	{
		status(gp::script_status::SelectableSet, true);
		status(gp::script_status::SelectableValue, value);
	}
	else if (flag == "AI Recruitable")
		status(gp::script_status::NotRecruitable, !value);
	else if (flag == "Player Targetable")
		status(gp::script_status::Targetable, value);
}

// ScriptActions::doNamedCustomColor (NAMED_CUSTOM_COLOR): Object::setCustomIndicatorColor, the colour it shows in place
// of its player's (0: its player's again).
inline void SetIndicatorColor(GameWorld &game, ecs::Entity entity, std::uint32_t argb)
{
	if (!game.world.IsAlive(entity))
		return;
	if (!game.world.Has<engine::gameplay::IndicatorColor>(entity))
		game.world.Add<engine::gameplay::IndicatorColor>(entity);
	game.world.Get<engine::gameplay::IndicatorColor>(entity)->argb = argb;
}

// SupplyWarehouseDockUpdate::setCashValue (WAREHOUSE_SET_VALUE): the warehouse holds ceil(cash / ValuePerSupplyBox) boxes
// (the original's float division rounded up into an Int; nothing below none). Not a warehouse: nothing.
inline void SetWarehouseCash(GameWorld &game, ecs::Entity warehouse, std::int64_t cash)
{
	auto *store = game.world.IsAlive(warehouse) ? game.world.Get<engine::gameplay::ResourceStore>(warehouse) : nullptr;
	const std::int64_t box = game.templates.Content().gameData.valuePerSupplyBox;
	if (store == nullptr || box <= 0)
		return;
	const std::int64_t boxes = cash > 0 ? (cash + box - 1) / box : -((-cash) / box);
	store->boxes = static_cast<std::uint32_t>(std::max<std::int64_t>(0, boxes));
}
}
