export module games.generalszh.presentation.interaction.algorithms.rule_queries;
import std;

export import engine.ecs.core.world;
export import games.generalszh.session.session_view;
import Engine.Core.Math.FixedPresentation;
import games.generalszh.presentation.interaction.resources.interaction_resources;
import games.generalszh.presentation.interaction.resources.build_placement;
import games.generalszh.presentation.interaction.components.selected;
import engine.gameplay.rts.vision.resources.shroud_map;

// What the interaction asks the game's rules before its frame (between ticks, through the session's view): whether
// the structure being placed may go where its ghost is, and what a command waiting for a target may be used on.
export namespace generalszh::presentation
{
// BuildAssistant::isLocationLegalToBuild for the ghost on the ground (its point and facing in the simulation's fixed
// point, rounded).
inline void QueryPlacementLegality(ecs::World &world, session::SessionView &game)
{
	auto &placement = world.Resource<BuildPlacement>();
	if (!placement.active || !placement.onGround)
		return;
	const float turns = placement.facing / (2.0f * std::numbers::pi_v<float>);
	placement.legal = game.CanBuildAt(placement.builder, placement.structure,
		{Engine::Math::Fixed::FromRaw(std::llround(static_cast<double>(placement.at[0]) * 65536.0)),
			Engine::Math::Fixed::FromRaw(std::llround(static_cast<double>(placement.at[1]) * 65536.0))},
		Engine::Math::TurnAngle{static_cast<std::uint32_t>(static_cast<std::int64_t>(std::llround(static_cast<double>(turns) * 4294967296.0)))},
		!placement.specialPower.empty());
}

// CommandTranslator::handleDefaultMoveCommand's quick path for the point the last move hint asked about: blocked unless
// the local player's view of it is not clear (fogged or shrouded: the check skipped), or a selected unit could path
// there (AIUpdateInterface::isQuickPathAvailable, or a CLIFF locomotor over a cliff cell; units without an AI do not
// count).
inline void QueryQuickPath(ecs::World &world, session::SessionView &game)
{
	auto *cursor = world.FindResource<CursorState>();
	if (cursor == nullptr || !cursor->quickPathAsked)
		return;
	const auto fixed = [](float value) { return Engine::Math::Fixed::FromRaw(std::llround(static_cast<double>(value) * 65536.0)); };
	const Engine::Math::FixedVector2 point{fixed(cursor->quickPathAt[0]), fixed(cursor->quickPathAt[1])};
	cursor->quickPathAnsweredAt = cursor->quickPathAt;
	cursor->quickPathBlocked = false;
	const auto *local = world.FindResource<LocalPlayer>();
	const auto *shroud = world.FindResource<engine::gameplay::ShroudMap>();
	if (local == nullptr || shroud == nullptr || shroud->StatusAt(local->player, point.x, point.y) != engine::gameplay::CellShroud::Clear)
		return;
	bool valid = false;
	for (const ecs::Entity unit : world.Side<Selected>().Entities())
		if (game.QuickPathAvailable(unit, point))
		{
			valid = true;
			break;
		}
	cursor->quickPathBlocked = !valid;
}

// A command waiting for an object: the shortcut's source of its power (the local player's), then whether it may be used
// on the one under the pointer as the interaction last picked it: canSelectedObjectsDoAction(ACTIONTYPE_COMBATDROP_INTO,
// SELECTION_ANY) for a combat drop (any selected unit may drop into it), canDoSpecialPowerAtObject otherwise.
inline void QueryTargetingValidity(ecs::World &world, session::SessionView &game)
{
	auto &targeting = world.Resource<GuiTargeting>();
	if (!targeting.active)
		return;
	if (!targeting.shortcutType.empty())
	{
		const auto *local = world.FindResource<LocalPlayer>();
		const auto source = local != nullptr && local->valid ? game.ShortcutPowerSource(local->player, targeting.shortcutType) : std::nullopt;
		targeting.source = source.value_or(ecs::Entity{});
	}
	if (targeting.kind == GuiCommandKind::CombatDrop)
	{
		bool valid = false;
		if (targeting.hovered != ecs::Entity{})
			for (const ecs::Entity unit : world.Side<Selected>().Entities())
				valid = valid || game.CanCombatDropInto(unit, targeting.hovered);
		targeting.validFor = valid ? targeting.hovered : ecs::Entity{};
	}
	else
		targeting.validFor = targeting.hovered != ecs::Entity{} && game.CanTargetWithPower(targeting.source, targeting.power, targeting.hovered)
			? targeting.hovered : ecs::Entity{};
}
}
