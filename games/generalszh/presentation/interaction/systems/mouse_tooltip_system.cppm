export module games.generalszh.presentation.interaction.systems.mouse_tooltip_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.interaction.algorithms.mouse_tooltip;
export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.appearance.components.indicator_color;
export import engine.gameplay.rts.containment.components.garrison;
export import engine.gameplay.rts.stealth.components.stealth;
export import engine.gameplay.rts.harvesting.components.resource_store;
export import engine.gameplay.rts.vision.resources.shroud_map;
export import engine.gameplay.rts.teams.resources.team_roster;
export import games.generalszh.gameplay.ai.components.mob_member;
import games.generalszh.content.objects.kind_of;

// The mouse's tooltip and cursor text each frame, in the original's order:
//   Mouse::createStreamMessages: the tooltip shows once the pointer has been still for its delay; a move restarts that;
//   GameWindowManager::winProcessMouseEvent clears the tooltip, then the window under the pointer sets its own
//   (PointerInput::windowTooltip, found by the host: Engine::UI::WND::Tooltip_Target);
//   SelectionTranslator, the left button up: a mouse-over hint of the nearest drawn object the pointer is over whose
//   pick type is selectable or force-attackable (getPickTypesForContext(TRUE)), alive or ALWAYS_SELECTABLE, else of
//   the ground; InGameUI::createMouseoverHint (not while scrolling or drag-selecting, nor over an opaque window): an
//   object's tooltip (mouse_tooltip's MouseoverTooltip, its subject as the original finds it: the apparent controller
//   of a garrison, a disguise as enemies see it, its warehouse boxes, its colour), the object moused over (an
//   IGNORED_IN_GUI mob member: its nexus; else none) and, when that changed, Mouse::resetTooltipDelay;
//   Mouse::setCursor: the cursor's CursorText when the cursor (InGameUI::setMouseCursor, CursorState) changed.
export namespace generalszh::presentation
{
struct MouseTooltipSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::Garrison>, ecs::Read<engine::gameplay::Stealth>,
		ecs::Read<engine::gameplay::ResourceStore>, ecs::Read<engine::gameplay::IndicatorColor>, ecs::Read<generalszh::gameplay::MobMember>>;
	using Resources = ecs::Resources<ecs::Read<PointerInput>, ecs::Read<InteractionState>, ecs::Read<InteractionView>, ecs::Read<PointerHits>,
		ecs::Read<SelectionCatalog>, ecs::Read<LocalPlayer>, ecs::Read<engine::gameplay::Relationships>, ecs::Read<engine::gameplay::ShroudMap>,
		ecs::Read<engine::gameplay::TeamRoster>, ecs::Read<CursorState>, ecs::Read<MouseTooltipSettings>, ecs::Read<MouseoverNames>,
		ecs::Write<MouseTooltip>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const PointerInput &pointer = context.Read<PointerInput>();
		const MouseTooltipSettings &settings = context.Read<MouseTooltipSettings>();
		const content::MouseTooltipContent &look = settings.look;
		MouseTooltip &tooltip = context.Write<MouseTooltip>();
		const std::uint32_t now = pointer.timeMs;
		const int displayWidth = static_cast<int>(context.Read<InteractionView>().width);

		UpdateTooltipShowing(tooltip, look, now);
		if (pointer.x != tooltip.lastX || pointer.y != tooltip.lastY)
			PointerMoved(tooltip, now);
		tooltip.lastX = pointer.x;
		tooltip.lastY = pointer.y;

		SetCursorTooltip(tooltip, look, u"", -1, std::nullopt, 1.0f, displayWidth);
		if (pointer.windowTooltip)
			SetCursorTooltip(tooltip, look, pointer.windowTooltip->first, pointer.windowTooltip->second, std::nullopt, 1.0f, displayWidth);
		if ((pointer.down & pointer_button::Left) == 0 && !pointer.scrolling && !context.Read<InteractionState>().dragSelecting &&
			(!pointer.overInterface || pointer.overRadar))
			Mouseover(context, tooltip, look, now, displayWidth);

		SetCursorText(tooltip, settings, context.Read<CursorState>().cursor);
	}

private:
	static void Mouseover(ecs::SystemContext &context, MouseTooltip &tooltip, const content::MouseTooltipContent &look, std::uint32_t now, int displayWidth)
	{
		const auto lookup = context.Lookup<Lookup>();
		const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
		const ecs::Entity old = tooltip.mousedOver;
		tooltip.mousedOver = {};
		if (const auto under = Under(context, lookup, catalog))
		{
			const ecs::Entity entity = *under;
			const std::uint32_t definition = lookup.Get<engine::gameplay::DefinitionRef>(entity)->index;
			const SelectionLook *kind = catalog.Of(definition);
			if (kind != nullptr && (kind->kinds & select_kind::IgnoredInGui) != 0)
			{
				// A mob member: its nexus stands for it (the tooltip is still the member's).
				if (const auto *member = lookup.Get<generalszh::gameplay::MobMember>(entity);
					member != nullptr && lookup.Get<engine::gameplay::DefinitionRef>(member->nexus) != nullptr)
					tooltip.mousedOver = member->nexus;
			}
			else
				tooltip.mousedOver = entity;
			if (const auto shown = MouseoverTooltip(Subject(context, lookup, catalog, entity, definition), context.Read<MouseoverNames>()))
				SetCursorTooltip(tooltip, look, shown->first, -1, shown->second, 1.0f, displayWidth);
		}
		if (old != tooltip.mousedOver)
			ResetTooltipDelay(tooltip, now);
	}

	// The drawable the mouse-over hint names: the nearest hit whose pick type is SELECTABLE or FORCEATTACKABLE, alive
	// or ALWAYS_SELECTABLE (a dead one is a hint of the ground).
	template<typename L>
	static std::optional<ecs::Entity> Under(ecs::SystemContext &context, const L &lookup, const SelectionCatalog &catalog)
	{
		const PointerInput &pointer = context.Read<PointerInput>();
		const PointerHits &hits = context.Read<PointerHits>();
		// Over the radar (LeftHUDInput: no window in the way of the hints) nothing is picked: it is not see-through.
		if (pointer.overRadar || hits.x != pointer.x || hits.y != pointer.y)
			return std::nullopt;
		for (const PointerHit &hit : hits.hits)
		{
			const ecs::Entity entity{static_cast<std::uint32_t>(hit.key >> 32), static_cast<std::uint32_t>(hit.key & 0xFFFFFFFFu)};
			const auto *reference = lookup.template Get<engine::gameplay::DefinitionRef>(entity);
			if (reference == nullptr)
				continue;
			const SelectionLook *kind = catalog.Of(reference->index);
			if (kind == nullptr)
				continue;
			const auto *health = lookup.template Get<engine::gameplay::Health>(entity);
			const bool dead = health != nullptr && engine::gameplay::IsDead(*health);
			if ((PickTypes(kind->kinds, dead) & (pick_type::Selectable | pick_type::ForceAttackable)) == 0)
				continue;
			if (dead && (kind->kinds & select_kind::AlwaysSelectable) == 0)
				return std::nullopt;
			return entity;
		}
		return std::nullopt;
	}

	// What createMouseoverHint finds of the object.
	template<typename L>
	static MouseoverSubject Subject(ecs::SystemContext &context, const L &lookup, const SelectionCatalog &catalog, ecs::Entity entity, std::uint32_t definition)
	{
		static constexpr content::KindOfName Disguiser{"DISGUISER"};
		const MouseoverNames &names = context.Read<MouseoverNames>();
		const LocalPlayer &local = context.Read<LocalPlayer>();
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		const auto name = [](const std::vector<std::u16string> &list, std::size_t index) -> std::u16string_view {
			return index < list.size() ? std::u16string_view(list[index]) : std::u16string_view{};
		};
		const auto color = [&](std::uint32_t player) { return player < names.playerColors.size() ? names.playerColors[player] : 0u; };
		MouseoverSubject subject;
		subject.displayName = name(names.displayNames, definition);
		subject.templateName = name(names.templateNames, definition);
		const auto *owner = lookup.template Get<engine::gameplay::Owner>(entity);
		const SelectionLook *kind = catalog.Of(definition);
		const bool garrisonable = kind != nullptr && kind->contain == ContainKind::Garrison;
		// ContainModuleInterface::getApparentControllingPlayer (GarrisonContain's: its original owner's to those it hides
		// from), else the controlling player.
		std::optional<std::uint32_t> apparent;
		if (garrisonable && owner != nullptr)
		{
			apparent = owner->player;
			if (const auto *garrison = lookup.template Get<engine::gameplay::Garrison>(entity); garrison != nullptr && local.valid &&
				local.player < 32 && ((garrison->hiddenFrom >> local.player) & 1u) != 0 && garrison->originalPlayer != engine::gameplay::Garrison::NoTeam)
				apparent = garrison->originalPlayer;
		}
		std::optional<std::uint32_t> player = apparent ? apparent : owner != nullptr ? std::optional<std::uint32_t>(owner->player) : std::nullopt;
		// A disguiser disguised: those not allied to its player (while playing) see the disguise's player and template.
		bool disguised = false;
		if (player && definition < catalog.kinds.size() && content::HasKindOf(catalog.kinds[definition], Disguiser.bit))
			if (const auto *stealth = lookup.template Get<engine::gameplay::Stealth>(entity); stealth != nullptr && stealth->IsDisguised())
				if (local.valid && relationships.Between(*player, local.player) != engine::gameplay::Relationship::Allies && stealth->disguisePlayer >= 0)
				{
					player = static_cast<std::uint32_t>(stealth->disguisePlayer);
					disguised = true;
					subject.displayName = name(names.displayNames, stealth->disguiseAs);
				}
		if (const auto *store = lookup.template Get<engine::gameplay::ResourceStore>(entity))
		{
			subject.warehouse = true;
			subject.boxes = store->boxes;
		}
		if (!player)
			return subject;
		subject.hasPlayer = true;
		subject.playerName = name(names.playerNames, *player);
		const auto *roster = context.Find<engine::gameplay::TeamRoster>();
		subject.playableSide = roster != nullptr && *player < roster->PlayerCount() && roster->PlayerAt(*player).playable;
		if (const auto *transform = lookup.template Get<engine::gameplay::Transform>(entity))
			subject.clear = context.Read<engine::gameplay::ShroudMap>().StatusAt(local.player, transform->position.x, transform->position.y) ==
				engine::gameplay::CellShroud::Clear;
		// The colour: a disguise's player's; else Object::getIndicatorColor (a script's colour, else its own player's),
		// a garrisonable building's apparent controller's.
		if (disguised)
			subject.color = color(*player);
		else
		{
			const auto *indicator = lookup.template Get<engine::gameplay::IndicatorColor>(entity);
			subject.color = indicator != nullptr && indicator->argb != 0 ? (indicator->argb & 0xFFFFFFu) : owner != nullptr ? color(owner->player) : 0u;
			if (garrisonable && apparent)
				subject.color = color(*apparent);
		}
		return subject;
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::MouseTooltipSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.mouse_tooltip";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// After the frame's pick and the pointer's interaction (its cursor, drag and selection): composition orders it.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
