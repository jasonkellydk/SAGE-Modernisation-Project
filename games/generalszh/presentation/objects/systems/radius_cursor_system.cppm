export module games.generalszh.presentation.objects.systems.radius_cursor_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.objects.resources.radius_cursor;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import games.generalszh.presentation.interaction.systems.interaction_systems;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.rts.combat.components.aggression;
export import engine.gameplay.rts.combat.resources.mood_ranges;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.common.spatial.resources.ground_height;
import Engine.Core.Math.FixedPresentation;

// InGameUI's radius cursor, each frame after the pointer's interaction:
//   setRadiusCursor: while a command waits for its target with the pointer over the world (under an opaque window the
//     mouse-over hint sets none), its RadiusCursorType is wanted. A type other than the cursor's makes it anew (the same
//     keeps it, radius and colour as made): none, or no living source with a controller, or a radius not above zero,
//     leave no cursor. The radius by type: ATTACK_DAMAGE_AREA its source's weapon in the command's slot's primary
//     damage radius (with its weapon bonus: Weapon::getPrimaryDamageRadius); ATTACK_SCATTER_AREA its ScatterRadius
//     plus ScatterTargetScalar; ATTACK_CONTINUE_AREA and CLEARMINES its ContinueAttackRange; GUARD_AREA the source's
//     inner guard range (AIGuardMachine::getStdGuardRange; no AI: 0); every other its special power's
//     RadiusCursorRadius.
//   RadiusDecalTemplate::createRadiusDecal: its template's texture and style, coloured by its Color (none: the source's
//     controller's colour); seen (a decal made) only by that player unless OnlyVisibleToOwningPlayer is No.
//   handleRadiusCursor / RadiusDecal::update: centred on the ground under the pointer (off the ground: where it was),
//     its opacity throbbing with the logic frame: (min + (sin(2pi (frame % throb) / throb) + 1) / 2 (max - min)) x 255,
//     truncated; 0 while scripts have icon UI off.
// (The double-click attack-move guard hint's cursor and the radar's pointer mapping are not ported.)
export namespace generalszh::presentation
{
struct RadiusCursorSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Armament>, ecs::Read<engine::gameplay::WeaponSlots>,
		ecs::Read<engine::gameplay::WeaponBonusConditions>, ecs::Read<engine::gameplay::Aggression>>;
	using Resources = ecs::Resources<ecs::Read<GuiTargeting>, ecs::Read<PointerInput>, ecs::Read<InteractionView>, ecs::Read<engine::gameplay::GroundHeight>,
		ecs::Read<LocalPlayer>, ecs::Read<LookCatalog>, ecs::Read<RadiusCursorLooks>, ecs::Read<PresentationFrame>, ecs::Read<engine::gameplay::WeaponCatalog>,
		ecs::Read<engine::gameplay::MoodRanges>, ecs::Read<engine::gameplay::TeamRoster>, ecs::Write<RadiusCursor>>;

	void Execute(ecs::SystemContext &context) const
	{
		RadiusCursor &cursor = context.Write<RadiusCursor>();
		const GuiTargeting &targeting = context.Read<GuiTargeting>();
		const PointerInput &pointer = context.Read<PointerInput>();
		const std::uint8_t wanted = targeting.active && !pointer.overInterface ? targeting.radiusCursor : content::radius_cursor::None;
		if (wanted != cursor.type)
			Make(context, cursor, wanted);
		if (cursor.type == content::radius_cursor::None)
			return;
		const InteractionView &view = context.Read<InteractionView>();
		if (view.valid)
			if (const auto under = PointerInteractionSystem::GroundUnder(view, context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y))
				cursor.at = *under;
		const content::RadiusDecalLook &look = context.Read<RadiusCursorLooks>().looks[cursor.type];
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		cursor.opacity = RadiusDecalOpacity(look, frame.tick, frame.drawIconUi);
	}

private:
	static void Make(ecs::SystemContext &context, RadiusCursor &cursor, std::uint8_t type)
	{
		namespace rc = content::radius_cursor;
		cursor = RadiusCursor{};
		const RadiusCursorLooks &looks = context.Read<RadiusCursorLooks>();
		if (type == rc::None || type >= looks.looks.size())
			return;
		const GuiTargeting &targeting = context.Read<GuiTargeting>();
		const auto lookup = context.Lookup<Lookup>();
		const auto *owner = lookup.Get<engine::gameplay::Owner>(targeting.source);
		if (owner == nullptr)
			return;
		const auto &weapons = context.Read<engine::gameplay::WeaponCatalog>();
		// Object::getWeaponInWeaponSlot.
		const auto weaponInSlot = [&]() -> const engine::gameplay::WeaponDefinition * {
			std::uint32_t weapon = engine::gameplay::WeaponCatalog::None;
			if (const auto *set = lookup.Get<engine::gameplay::WeaponSlots>(targeting.source))
				weapon = targeting.weaponSlot < set->slots.size() ? set->slots[targeting.weaponSlot].weapon : engine::gameplay::WeaponCatalog::None;
			else if (const auto *armament = lookup.Get<engine::gameplay::Armament>(targeting.source); armament != nullptr && targeting.weaponSlot == 0)
				weapon = armament->weapon;
			return weapon != engine::gameplay::WeaponCatalog::None ? &weapons.At(weapon) : nullptr;
		};
		Engine::Math::Fixed radius;
		switch (type)
		{
		case rc::AttackDamageArea:
			if (const auto *weapon = weaponInSlot())
			{
				const auto *conditions = lookup.Get<engine::gameplay::WeaponBonusConditions>(targeting.source);
				radius = weapon->primaryRadius * weapons.Bonus(*weapon, conditions != nullptr ? conditions->Effective() : 0u).Get(engine::gameplay::WeaponBonusField::Radius);
			}
			break;
		case rc::AttackScatterArea:
			if (const auto *weapon = weaponInSlot())
				radius = weapon->scatterRadius + weapon->scatterTargetScalar;
			break;
		case rc::AttackContinueArea:
		case rc::ClearMines:
			if (const auto *weapon = weaponInSlot())
				radius = weapon->continueAttackRange;
			break;
		case rc::GuardArea:
			if (const auto *aggression = lookup.Get<engine::gameplay::Aggression>(targeting.source))
			{
				const auto &roster = context.Read<engine::gameplay::TeamRoster>();
				const bool human = owner->player < roster.PlayerCount() && roster.PlayerAt(owner->player).human;
				radius = engine::gameplay::GuardVision(context.Read<engine::gameplay::MoodRanges>(), aggression->vision, human, true, aggression->attitude);
			}
			break;
		default: radius = targeting.powerCursorRadius; break;
		}
		const content::RadiusDecalLook &look = looks.looks[type];
		if (radius <= Engine::Math::Fixed{} || !look.Present())
			return;
		cursor.type = type;
		const LocalPlayer &local = context.Read<LocalPlayer>();
		cursor.shown = !look.onlyOwner || (local.valid && local.player == owner->player);
		cursor.additive = look.additive;
		cursor.texture = look.texture;
		cursor.radius = Engine::Math::ToFloat(radius);
		if (look.hasColor)
			cursor.color = {look.color[0] / 255.0f, look.color[1] / 255.0f, look.color[2] / 255.0f};
		else
		{
			const auto color = context.Read<LookCatalog>().ColorOf(owner->player);
			cursor.color = {color[0], color[1], color[2]};
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::RadiusCursorSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.radius_cursor";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
