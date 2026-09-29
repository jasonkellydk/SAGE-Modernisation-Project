export module games.generalszh.presentation.interaction.systems.interaction_systems;
import games.generalszh.gameplay.powers.components.particle_cannon;
import games.generalszh.gameplay.powers.components.spectre_gunship;
import engine.gameplay.rts.vision.resources.shroud_map;
import std;
import engine.gameplay.rts.containment.components.transport;

export import engine.ecs.system.system;
export import games.generalszh.presentation.interaction.components.selected;
export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import games.generalszh.presentation.interaction.algorithms.selection_rules;
export import engine.gameplay.common.spatial.systems.snapshot_system;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.status.components.script_status;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.rts.slaves.components.slaved;
export import engine.gameplay.common.identity.components.definition_ref;
import Engine.Core.Math.FixedPresentation;
import games.generalszh.gameplay.world.resources.deselections;
export import engine.gameplay.common.status.components.status_flags;
export import engine.gameplay.rts.loadout.components.loadout;
import games.generalszh.content.objects.object_status;
import games.generalszh.content.combat.loadout_content;
export import games.generalszh.presentation.interaction.resources.build_placement;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import games.generalszh.gameplay.powers.algorithms.power_targeting;
import games.generalszh.content.control_bar.command_catalog;

// The player's pointer in the world, on real frame time (the original's
// SelectionTranslator and CommandTranslator for the default mouse setup):
//   left down anchors; moving past DragTolerance (either axis) drags a box;
//   left up is a click, a point when the box is under DragTolerance on both
//   axes: the selection rules pick (a point: the object under the pointer;
//   a box: selectable objects whose position projects into it) and select,
//   or leave the click to the command translator, which on a point with a
//   selection of the player's issues the context command: attack what can be
//   attacked, else move to the ground clicked (never onto an object);
//   a right click (within DragTolerance, DragToleranceMS and the camera's
//   DragTolerance3D) deselects all.
// Selection is the Selected side table; orders go to PlayerOrders. Every frame the same evaluation, as a hint
// (CommandTranslator's DO_HINT through InGameUI::createMouseoverHint / createCommandHint), picks the mouse cursor
// (CursorState).

export namespace generalszh::presentation
{
namespace interaction_detail
{
// A pointer's world position into the simulation's numbers (once, on the issuing client: the command carries it).
inline Engine::Math::Fixed ToFixed(float value) noexcept
{
	return Engine::Math::Fixed::FromRaw(static_cast<std::int64_t>(std::llround(static_cast<double>(value) * static_cast<double>(Engine::Math::Fixed::One().Raw()))));
}

struct Candidate
{
	ecs::Entity entity;
	float depth{0};
	PickedObject picked;
	float x{0}, y{0}; // its position
	std::uint32_t definition{0};
};

// CanAttackResult, in its order (the best of a selection is the highest).
enum class AttackResult : std::uint8_t
{
	NotPossible, // ATTACKRESULT_NOT_POSSIBLE
	InvalidShot, // ATTACKRESULT_INVALID_SHOT: armed, but no clear shot
	AfterMoving, // ATTACKRESULT_POSSIBLE_AFTER_MOVING
	Possible,    // ATTACKRESULT_POSSIBLE
};
}

struct PointerInteractionSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Owner>>;
	using SideTables = ecs::SideTables<ecs::Write<Selected>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Targetable>, ecs::Read<engine::gameplay::OffMap>,
		ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::Armament>, ecs::Read<engine::gameplay::WeaponSlots>,
		ecs::Read<engine::gameplay::Slaved>, ecs::Read<engine::gameplay::ScriptStatus>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Read<engine::gameplay::StatusFlags>, ecs::Read<engine::gameplay::Loadout>, ecs::Read<generalszh::gameplay::ParticleCannon>,
		ecs::Read<generalszh::gameplay::SpectreGunship>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::WeaponBonusConditions>,
		ecs::Read<engine::gameplay::Transport>>;
	using Resources = ecs::Resources<ecs::Read<PointerInput>, ecs::Read<InteractionView>, ecs::Write<InteractionState>, ecs::Read<MouseSettings>,
		ecs::Write<SelectionBox>, ecs::Read<SelectionCatalog>, ecs::Read<LocalPlayer>, ecs::Write<PlayerOrders>, ecs::Read<engine::gameplay::VisibleObjects>,
		ecs::Read<engine::gameplay::Relationships>, ecs::Read<engine::gameplay::WeaponCatalog>, ecs::Read<engine::gameplay::GroundHeight>,
		ecs::Read<generalszh::gameplay::Deselections>, ecs::Read<engine::gameplay::ShroudMap>, ecs::Write<CursorState>, ecs::Read<BuildPlacement>, ecs::Write<GuiTargeting>>;

	void Execute(ecs::SystemContext &context) const
	{
		using namespace interaction_detail;
		// GameLogic::deselectObject: what the logic took out of every selection.
		for (const ecs::Entity entity : context.Read<generalszh::gameplay::Deselections>().list)
			context.Side<SideTables, Selected>().Erase(entity);
		const PointerInput &pointer = context.Read<PointerInput>();
		const InteractionView &view = context.Read<InteractionView>();
		InteractionState &state = context.Write<InteractionState>();
		const MouseSettings &mouse = context.Read<MouseSettings>();
		SelectionBox &box = context.Write<SelectionBox>();
		const LocalPlayer &local = context.Read<LocalPlayer>();
		auto &selected = context.Side<SideTables, Selected>();
		if (!view.valid || !local.valid)
			return;

		// Left button: anchor, drag, release.
		if ((pointer.pressed & pointer_button::Left) != 0 && !pointer.overInterface)
		{
			state.leftDown = true;
			state.leftAnchorX = pointer.x;
			state.leftAnchorY = pointer.y;
		}
		if (state.leftDown && !state.dragSelecting &&
			(std::abs(pointer.x - state.leftAnchorX) > mouse.dragTolerance || std::abs(pointer.y - state.leftAnchorY) > mouse.dragTolerance))
			state.dragSelecting = true;
		box = state.dragSelecting ? SelectionBox{true, state.leftAnchorX, state.leftAnchorY, pointer.x, pointer.y} : SelectionBox{};
		if ((pointer.released & pointer_button::Left) != 0 && state.leftDown)
		{
			state.leftDown = false;
			state.dragSelecting = false;
			box = {};
			const bool isPoint = IsPointClick(state.leftAnchorX, state.leftAnchorY, pointer.x, pointer.y, mouse.dragTolerance);
			// With a command waiting for its target, nothing is selected (currentlyLookingForSelection); a point click
			// on a valid target gives it (handleGuiCommand, DO_COMMAND) and ends the wait.
			if (context.Read<GuiTargeting>().active)
			{
				if (isPoint)
					GuiClick(context, pointer);
			}
			else
				LeftClick(context, pointer, isPoint);
		}

		// Right button: a click deselects all.
		if ((pointer.pressed & pointer_button::Right) != 0 && !pointer.overInterface)
		{
			state.rightDown = true;
			state.rightAnchorX = pointer.x;
			state.rightAnchorY = pointer.y;
			state.rightDownTimeMs = pointer.timeMs;
			state.rightDownCamera = view.eye;
		}
		if ((pointer.released & pointer_button::Right) != 0 && state.rightDown)
		{
			state.rightDown = false;
			const float dx = pointer.x - state.rightAnchorX, dy = pointer.y - state.rightAnchorY;
			float moved = 0.0f;
			for (std::size_t axis = 0; axis < 3; ++axis)
				moved += (view.eye[axis] - state.rightDownCamera[axis]) * (view.eye[axis] - state.rightDownCamera[axis]);
			const bool click = pointer.timeMs - state.rightDownTimeMs <= mouse.dragToleranceMs && dx * dx + dy * dy <= mouse.dragTolerance * mouse.dragTolerance &&
				moved <= mouse.dragTolerance3D * mouse.dragTolerance3D;
			// A right click gives a waiting command up without deselecting (onRawMouseRightButtonUp); else deselects all.
			if (click)
			{
				if (GuiTargeting &targeting = context.Write<GuiTargeting>(); targeting.active)
					targeting = {};
				else
					selected.Clear();
			}
		}

		HoverCursor(context, pointer);
	}

	// W3DMouse::setCursorDirection: of a cursor's `directions` (frame 0 pointing right, then clockwise on the screen),
	// the one nearest the scroll offset's direction; 0 without an offset.
	static std::uint8_t ScrollDirection(float x, float y, int directions) noexcept
	{
		if (directions <= 1 || (x == 0.0f && y == 0.0f))
			return 0;
		constexpr float turn = 2.0f * std::numbers::pi_v<float>;
		const float theta = std::fmod(std::atan2(y, x) + turn, turn);
		const int frame = static_cast<int>(theta / (turn / static_cast<float>(directions)) + 0.5f);
		return static_cast<std::uint8_t>(frame >= directions ? 0 : frame);
	}

	// What a click would do (CommandTranslator::evaluateContextCommand, or evaluateForceAttack when force-attacking),
	// as the message it would post: nothing (MSG_INVALID), or a hint of the command.
	enum class Hint : std::uint8_t
	{
		Invalid,
		OverrideDestination, // MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION
		GetRepaired,         // MSG_GET_REPAIRED
		Hijack,              // MSG_HIJACK_HINT: an enter (createEnterMessage)
		ConvertToCarBomb,    // MSG_CONVERT_TO_CARBOMB
		AttackObject,        // MSG_DO_ATTACK_OBJECT
		AttackAfterMoving,   // MSG_DO_ATTACK_OBJECT_AFTER_MOVING
		ImpossibleAttack,    // MSG_IMPOSSIBLE_ATTACK
		ForceAttackObject,   // MSG_DO_FORCE_ATTACK_OBJECT
		ForceAttackGround,   // MSG_DO_FORCE_ATTACK_GROUND
		SetRallyPoint,       // MSG_SET_RALLY_POINT (handleSetRallyPointCommand)
		Move,                // handleDefaultMoveCommand (a move only onto the ground)
	};

private:
	template<typename Lookup>
	static std::optional<PickedObject> Classify(ecs::Entity entity, const Lookup &lookup, const SelectionCatalog &catalog, std::uint32_t definition,
		std::uint32_t player, const engine::gameplay::Relationships &relationships, std::uint32_t local)
	{
		const SelectionLook *look = catalog.Of(definition);
		if (look == nullptr)
			return std::nullopt;
		PickedObject picked;
		picked.entity = entity;
		if (player == local)
			picked.side = PickSide::Mine;
		else
			switch (relationships.Between(local, player))
			{
			case engine::gameplay::Relationship::Allies: picked.side = PickSide::Friend; break;
			case engine::gameplay::Relationship::Enemies: picked.side = PickSide::Enemy; break;
			default: picked.side = PickSide::Civilian; break;
			}
		picked.structure = (look->kinds & select_kind::Structure) != 0;
		picked.infantry = (look->kinds & select_kind::Infantry) != 0;
		picked.crate = (look->kinds & select_kind::Crate) != 0;
		picked.contained = lookup.template Get<engine::gameplay::OffMap>(entity) != nullptr;
		return picked;
	}

	// Drawable::isSelectable with addDrawableToList's kinds: SELECTABLE (or ALWAYS_SELECTABLE, FORCEATTACKABLE in
	// force-attack mode), alive unless ALWAYS_SELECTABLE, not UNSELECTABLE (an enslaved drone), not carried.
	template<typename Lookup>
	static bool Pickable(ecs::Entity entity, const SelectionLook &look, const Lookup &lookup, bool forceAttack)
	{
		const std::uint16_t kinds = select_kind::Selectable | select_kind::AlwaysSelectable | (forceAttack ? select_kind::ForceAttackable : 0);
		if ((look.kinds & kinds) == 0)
			return false;
		if (const auto *health = lookup.template Get<engine::gameplay::Health>(entity); health != nullptr && engine::gameplay::IsDead(*health) &&
			(look.kinds & select_kind::AlwaysSelectable) == 0)
			return false;
		if (const auto *slave = lookup.template Get<engine::gameplay::Slaved>(entity); slave != nullptr && slave->enslaved != 0)
			return false;
		if (lookup.template Get<engine::gameplay::OffMap>(entity) != nullptr)
			return false;
		return true;
	}

public:
	// The ground under a pixel (W3DView::screenToTerrain): along the ray until below the ground, then halved in.
	static std::optional<std::array<float, 3>> GroundUnder(const InteractionView &view, const engine::gameplay::GroundHeight &ground, float sx, float sy)
	{
		const auto ray = view.Ray(sx, sy);
		const auto height = [&](float t) {
			const float x = view.eye[0] + ray[0] * t, y = view.eye[1] + ray[1] * t;
			return view.eye[2] + ray[2] * t - Engine::Math::ToFloat(ground.At({interaction_detail::ToFixed(x), interaction_detail::ToFixed(y)}));
		};
		float previous = 0.0f;
		for (float t = 2.0f; t < 8000.0f; t += 4.0f)
		{
			if (height(t) > 0.0f)
			{
				previous = t;
				continue;
			}
			float low = previous, high = t;
			for (int step = 0; step < 16; ++step)
			{
				const float middle = (low + high) * 0.5f;
				(height(middle) > 0.0f ? low : high) = middle;
			}
			return std::array<float, 3>{view.eye[0] + ray[0] * high, view.eye[1] + ray[1] * high, view.eye[2] + ray[2] * high};
		}
		return std::nullopt;
	}

private:
	// pickDrawable: of the pickable objects, the nearest whose pick sphere is under the pixel.
	template<typename Lookup>
	static std::optional<interaction_detail::Candidate> PickAt(ecs::SystemContext &context, const Lookup &lookup, float x, float y, bool forceAttack)
	{
		using namespace interaction_detail;
		const InteractionView &view = context.Read<InteractionView>();
		const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
		const std::uint32_t local = context.Read<LocalPlayer>().player;
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		std::optional<Candidate> nearest;
		context.Read<engine::gameplay::VisibleObjects>().ForEach([&](const engine::gameplay::VisibleObject &object) {
			const SelectionLook *look = catalog.Of(object.definition);
			if (look == nullptr || !Pickable(object.entity, *look, lookup, forceAttack))
				return;
			const float px = Engine::Math::ToFloat(object.transform.position.x), py = Engine::Math::ToFloat(object.transform.position.y),
						pz = Engine::Math::ToFloat(object.transform.position.z);
			const auto picked = Classify(object.entity, lookup, catalog, object.definition, object.player, relationships, local);
			if (!picked)
				return;
			float sx = 0, sy = 0;
			if (!view.Project(px, py, pz + look->center, sx, sy))
				return;
			float edgeX = 0, edgeY = 0;
			const float depth = (px - view.eye[0]) * view.forward[0] + (py - view.eye[1]) * view.forward[1] + (pz + look->center - view.eye[2]) * view.forward[2];
			if (!view.Project(px + view.right[0] * look->radius, py + view.right[1] * look->radius, pz + look->center + view.right[2] * look->radius, edgeX, edgeY))
				return;
			const float screenRadius = std::hypot(edgeX - sx, edgeY - sy);
			if (std::hypot(x - sx, y - sy) > screenRadius)
				return;
			if (!nearest || depth < nearest->depth)
				nearest = Candidate{object.entity, depth, *picked, px, py, object.definition};
		});
		return nearest;
	}

	// What is selected now, and which of it is the player's (by index).
	struct Selection
	{
		std::vector<PickedObject> current;
		std::vector<ecs::Entity> mine;
	};

	template<typename Lookup>
	static Selection SelectedNow(ecs::SystemContext &context, const Lookup &lookup)
	{
		const std::uint32_t local = context.Read<LocalPlayer>().player;
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		Selection selection;
		for (const ecs::Entity entity : context.Side<SideTables, Selected>().Entities())
		{
			const auto *owner = lookup.template Get<engine::gameplay::Owner>(entity);
			if (owner == nullptr)
				continue;
			PickedObject object;
			object.entity = entity;
			object.side = owner->player == local ? PickSide::Mine
				: relationships.Allies(local, owner->player) ? PickSide::Friend
				: relationships.Enemies(local, owner->player) ? PickSide::Enemy : PickSide::Civilian;
			selection.current.push_back(object);
			if (object.side == PickSide::Mine)
				selection.mine.push_back(entity);
		}
		std::sort(selection.mine.begin(), selection.mine.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
		return selection;
	}

	// InGameUI::getCanSelectedObjectsAttack (SELECTION_ANY: the best of the player's selected objects) through
	// ActionManager::getCanAttackObject and WeaponSet::getAbleToAttackSpecificObject (CMD_FROM_PLAYER): nothing against
	// an unattackable object or, not forced, one not an enemy's (unless a script or the map made it player-targetable,
	// OBJECT_STATUS_SCRIPT_TARGETABLE, and it is not an ally's), nor from one with no weapon; an invalid shot when none
	// of its weapons may hit the target's kind, or when it cannot close in (IMMOBILE, SPAWNS_ARE_THE_WEAPONS, or
	// inside something) and its current weapon is out of range; else possible, now within its current weapon's
	// range (with its bonuses, between the bounding circles) or after moving.
	template<typename Lookup>
	static interaction_detail::AttackResult SelectionAttack(ecs::SystemContext &context, const std::vector<ecs::Entity> &mine, ecs::Entity target,
		const Lookup &lookup, bool forceAttack)
	{
		using interaction_detail::AttackResult;
		const auto &weapons = context.Read<engine::gameplay::WeaponCatalog>();
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
		const std::uint32_t local = context.Read<LocalPlayer>().player;
		const auto *targetable = lookup.template Get<engine::gameplay::Targetable>(target);
		const auto *owner = lookup.template Get<engine::gameplay::Owner>(target);
		if (targetable == nullptr || owner == nullptr || (targetable->classes & engine::gameplay::target_class::Unattackable) != 0)
			return AttackResult::NotPossible;
		if (!forceAttack && !relationships.Enemies(local, owner->player))
		{
			const auto *script = lookup.template Get<engine::gameplay::ScriptStatus>(target);
			if (script == nullptr || !script->Has(engine::gameplay::script_status::Targetable) || relationships.Allies(local, owner->player))
				return AttackResult::NotPossible;
		}
		static const std::size_t immobileBit = content::KindOfBit("IMMOBILE"), spawnsBit = content::KindOfBit("SPAWNS_ARE_THE_WEAPONS");
		const auto *targetAt = lookup.template Get<engine::gameplay::Transform>(target);
		AttackResult best = AttackResult::NotPossible;
		for (const ecs::Entity unit : mine)
		{
			if (unit == target)
				continue;
			const auto *armament = lookup.template Get<engine::gameplay::Armament>(unit);
			if (armament == nullptr)
				continue;
			const auto canUse = [&](std::uint32_t weapon) {
				return weapon != engine::gameplay::WeaponCatalog::None && engine::gameplay::CanTarget(weapons.At(weapon), targetable->classes);
			};
			bool usable = canUse(armament->weapon);
			if (const auto *slots = lookup.template Get<engine::gameplay::WeaponSlots>(unit))
				for (const auto &slot : slots->slots)
					usable = usable || canUse(slot.weapon);
			bool inRange = false;
			const auto *unitAt = lookup.template Get<engine::gameplay::Transform>(unit);
			if (usable && armament->weapon != engine::gameplay::WeaponCatalog::None && unitAt != nullptr && targetAt != nullptr)
			{
				const engine::gameplay::WeaponDefinition &weapon = weapons.At(armament->weapon);
				const auto *conditions = lookup.template Get<engine::gameplay::WeaponBonusConditions>(unit);
				const Engine::Math::Fixed range =
					engine::gameplay::BonusAttackRange(weapon.attackRange, weapons.Bonus(weapon, conditions != nullptr ? conditions->Effective() : 0u));
				const auto *unitBody = lookup.template Get<engine::gameplay::Targetable>(unit);
				const Engine::Math::Fixed reach = range + (unitBody != nullptr ? unitBody->radius : Engine::Math::Fixed{}) + targetable->radius;
				inRange = Engine::Math::DistanceSquared(unitAt->position.XY(), targetAt->position.XY()) <= reach * reach;
			}
			const auto *ref = lookup.template Get<engine::gameplay::DefinitionRef>(unit);
			const bool pinned = lookup.template Get<engine::gameplay::OffMap>(unit) != nullptr ||
				(ref != nullptr && ref->index < catalog.kinds.size() &&
					(content::HasKindOf(catalog.kinds[ref->index], immobileBit) || content::HasKindOf(catalog.kinds[ref->index], spawnsBit)));
			const AttackResult result = (pinned && !inRange) || !usable ? AttackResult::InvalidShot
				: inRange ? AttackResult::Possible : AttackResult::AfterMoving;
			best = std::max(best, result);
		}
		return best;
	}

	// canSelectedObjectsDoAction(ACTIONTYPE_SET_RALLY_POINT, SELECTION_ALL): each selected object AUTO_RALLYPOINT and the
	// player's (isLocallyControlled).
	template<typename Lookup>
	static bool AllSetRallyPoints(ecs::SystemContext &context, const Lookup &lookup, const Selection &selection)
	{
		static const std::size_t rallyBit = content::KindOfBit("AUTO_RALLYPOINT");
		const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
		if (selection.current.empty() || selection.current.size() != selection.mine.size())
			return false;
		return std::ranges::all_of(selection.mine, [&](ecs::Entity entity) {
			const auto *ref = lookup.template Get<engine::gameplay::DefinitionRef>(entity);
			return ref != nullptr && ref->index < catalog.kinds.size() && content::HasKindOf(catalog.kinds[ref->index], rallyBit);
		});
	}

	// CommandTranslator::evaluateContextCommand (evaluateForceAttack when force-attacking) for the object `under` the
	// pointer (none: the ground `at`), with the player's selected objects `mine`.
	template<typename Lookup>
	static Hint Evaluate(ecs::SystemContext &context, const Lookup &lookup, const std::vector<ecs::Entity> &mine, const PickedObject *under,
		const std::optional<std::array<float, 3>> &at, bool forceAttack, bool prefer, bool allSetRallyPoints = false)
	{
		using namespace interaction_detail;
		const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
		// One of mine under prefer-selection is never a command; nothing is with none of mine selected
		// (areSelectedObjectsControllable).
		if (under != nullptr && under->side == PickSide::Mine && prefer && !forceAttack)
			return Hint::Invalid;
		if (mine.empty())
			return Hint::Invalid;
		if (forceAttack)
		{
			// canAnyForceAttack: an object, else the ground (any of mine able to shoot at it).
			if (under != nullptr)
			{
				const AttackResult result = SelectionAttack(context, mine, under->entity, lookup, true);
				return result >= AttackResult::AfterMoving ? Hint::ForceAttackObject : result == AttackResult::InvalidShot ? Hint::ImpossibleAttack : Hint::Invalid;
			}
			if (!at)
				return Hint::Invalid;
			const auto &weapons = context.Read<engine::gameplay::WeaponCatalog>();
			const bool any = std::ranges::any_of(mine, [&](ecs::Entity unit) {
				const auto *armament = lookup.template Get<engine::gameplay::Armament>(unit);
				return armament != nullptr && armament->weapon != engine::gameplay::WeaponCatalog::None &&
					engine::gameplay::CanTarget(weapons.At(armament->weapon), engine::gameplay::target_class::Ground);
			});
			return any ? Hint::ForceAttackGround : Hint::Invalid;
		}
		// InGameUI::canSelectedObjectsOverrideSpecialPowerDestination (SELECTION_ANY): one of mine has a special power
		// whose destination may be driven now (doesSpecialPowerHaveOverridableDestinationActive: a Particle Cannon
		// pre-firing, firing or after its beam; a Spectre Gunship inserting or orbiting), the spot not black to its
		// player (ActionManager::canOverrideSpecialPowerDestination). It comes before every other context command.
		if (at)
		{
			const auto &shroud = context.Read<engine::gameplay::ShroudMap>();
			const Engine::Math::Fixed x = ToFixed((*at)[0]), y = ToFixed((*at)[1]);
			const bool drives = std::ranges::any_of(mine, [&](ecs::Entity unit) {
				namespace domain = generalszh::gameplay;
				bool active = false;
				if (const auto *cannon = lookup.template Get<domain::ParticleCannon>(unit))
					active = cannon->status == domain::CannonStatus::PreFire || cannon->status == domain::CannonStatus::Firing ||
						cannon->status == domain::CannonStatus::PostFire;
				if (const auto *gunship = lookup.template Get<domain::SpectreGunship>(unit))
					active = active || gunship->status == domain::GunshipStatus::Inserting || gunship->status == domain::GunshipStatus::Orbiting;
				const auto *owner = lookup.template Get<engine::gameplay::Owner>(unit);
				return active && owner != nullptr && shroud.StatusAt(owner->player, x, y) != engine::gameplay::CellShroud::Shrouded;
			});
			if (drives)
				return Hint::OverrideDestination;
		}
		// ACTIONTYPE_SET_RALLY_POINT (SELECTION_ALL, nothing under the pointer): every selected one AUTO_RALLYPOINT and the
		// player's.
		if (under == nullptr)
			return allSetRallyPoints ? Hint::SetRallyPoint : Hint::Move;
		const auto kindsOf = [&](ecs::Entity entity) -> std::uint16_t {
			const auto *ref = lookup.template Get<engine::gameplay::DefinitionRef>(entity);
			const SelectionLook *look = ref != nullptr ? catalog.Of(ref->index) : nullptr;
			return look != nullptr ? look->kinds : 0;
		};
		// ACTIONTYPE_GET_REPAIRED_AT (SELECTION_ANY): a repair pad of the player's or an ally's that some selected damaged
		// ground vehicle may go to (the game checks canGetRepairedAt for each as it takes the order).
		const bool repairsAt = (under->side == PickSide::Mine || under->side == PickSide::Friend) && (kindsOf(under->entity) & select_kind::RepairPad) != 0 &&
			std::ranges::any_of(mine, [&](ecs::Entity unit) {
				const auto *health = lookup.template Get<engine::gameplay::Health>(unit);
				const std::uint16_t kinds = kindsOf(unit);
				return (kinds & select_kind::Vehicle) != 0 && (kinds & select_kind::Aircraft) == 0 && health != nullptr && health->current < health->maximum;
			});
		if (repairsAt)
			return Hint::GetRepaired;
		// ACTIONTYPE_HIJACK_VEHICLE (SELECTION_ANY): canHijackVehicle for some selected hijacker: an enemy VEHICLE, alive,
		// not AIRCRAFT, BOAT, DRONE or IMMUNE_TO_CAPTURE, with an AI, not HIJACKED, not a transport with anyone in it, of
		// the kinds its collide takes.
		const auto hijacks = [&]() {
			if (under->side != PickSide::Enemy)
				return false;
			const auto *ref = lookup.template Get<engine::gameplay::DefinitionRef>(under->entity);
			if (ref == nullptr || ref->index >= catalog.kinds.size())
				return false;
			const content::KindOfMask &kinds = catalog.kinds[ref->index];
			const auto is = [&](std::string_view name) { return content::HasKindOf(kinds, content::KindOfBit(name)); };
			if (!is("VEHICLE") || is("AIRCRAFT") || is("BOAT") || is("DRONE") || is("IMMUNE_TO_CAPTURE"))
				return false;
			if (const auto *health = lookup.template Get<engine::gameplay::Health>(under->entity); health != nullptr && engine::gameplay::IsDead(*health))
				return false;
			static const std::uint64_t hijacked = std::uint64_t{1} << content::ObjectStatusBit("HIJACKED");
			if (const auto *flags = lookup.template Get<engine::gameplay::StatusFlags>(under->entity); flags != nullptr && (flags->bits & hijacked) != 0)
				return false;
			if (const auto *carrier = lookup.template Get<engine::gameplay::Transport>(under->entity); carrier != nullptr && is("TRANSPORT") && carrier->occupied > 0)
				return false;
			return std::ranges::any_of(mine, [&](ecs::Entity unit) {
				const auto *unitRef = lookup.template Get<engine::gameplay::DefinitionRef>(unit);
				const SelectionLook *jacker = unitRef != nullptr ? catalog.Of(unitRef->index) : nullptr;
				if (jacker == nullptr || jacker->hijacker == SelectionLook::NoCarBomber)
					return false;
				const CarBomberKinds &wants = catalog.hijackers[jacker->hijacker];
				for (std::size_t word = 0; word < kinds.size(); ++word)
					if ((kinds[word] & wants.required[word]) != wants.required[word] || (kinds[word] & wants.forbidden[word]) != 0)
						return false;
				return true;
			});
		};
		if (hijacks())
			return Hint::Hijack;
		// ACTIONTYPE_CONVERT_OBJECT_TO_CARBOMB (SELECTION_ANY): a living vehicle that may be made a car bomb (with an AI,
		// not AIRCRAFT or BOAT, a CARBOMB weapon set not in use, not IS_CARBOMB) of the kinds some selected car bomber's
		// collide takes (the game checks canConvertObjectToCarBomb for each as it takes the order).
		const auto convertsToCarBomb = [&]() {
			const auto *ref = lookup.template Get<engine::gameplay::DefinitionRef>(under->entity);
			const SelectionLook *look = ref != nullptr ? catalog.Of(ref->index) : nullptr;
			if (look == nullptr || !look->carBombable)
				return false;
			if (const auto *health = lookup.template Get<engine::gameplay::Health>(under->entity); health != nullptr && engine::gameplay::IsDead(*health))
				return false;
			static const std::uint64_t carBombStatus = std::uint64_t{1} << content::ObjectStatusBit("IS_CARBOMB");
			static const std::uint32_t carBombFlag = content::SetFlag(content::WeaponSetFlagNames, "CARBOMB");
			if (const auto *flags = lookup.template Get<engine::gameplay::StatusFlags>(under->entity); flags != nullptr && (flags->bits & carBombStatus) != 0)
				return false;
			if (const auto *loadout = lookup.template Get<engine::gameplay::Loadout>(under->entity); loadout != nullptr && (loadout->weaponFlags & carBombFlag) != 0)
				return false;
			const content::KindOfMask &kinds = catalog.kinds[ref->index];
			return std::ranges::any_of(mine, [&](ecs::Entity unit) {
				const auto *unitRef = lookup.template Get<engine::gameplay::DefinitionRef>(unit);
				const SelectionLook *bomber = unitRef != nullptr ? catalog.Of(unitRef->index) : nullptr;
				if (bomber == nullptr || bomber->carBomber == SelectionLook::NoCarBomber || unit == under->entity)
					return false;
				const CarBomberKinds &wants = catalog.carBombers[bomber->carBomber];
				for (std::size_t word = 0; word < kinds.size(); ++word)
					if ((kinds[word] & wants.required[word]) != wants.required[word] || (kinds[word] & wants.forbidden[word]) != 0)
						return false;
				return true;
			});
		};
		if (convertsToCarBomb())
			return Hint::ConvertToCarBomb;
		switch (SelectionAttack(context, mine, under->entity, lookup, false))
		{
		case AttackResult::Possible: return Hint::AttackObject;
		case AttackResult::AfterMoving: return Hint::AttackAfterMoving;
		case AttackResult::InvalidShot: return Hint::ImpossibleAttack;
		default: return Hint::Move;
		}
	}

	// The hint as the cursor (CommandTranslator's MSG_MOUSEOVER_*_HINT: evaluateContextCommand with DO_HINT, then
	// InGameUI::createMouseoverHint and createCommandHint), in MOUSEMODE_DEFAULT and MOUSEMODE_BUILD_PLACE; left as it
	// is where the original sets none.
	static void HoverCursor(ecs::SystemContext &context, const PointerInput &pointer)
	{
		using namespace interaction_detail;
		using content::MouseCursorKind;
		CursorState &cursor = context.Write<CursorState>();
		const MouseSettings &mouse = context.Read<MouseSettings>();
		// InGameUI::setScrolling: SCROLL while the camera scrolls (no hints meanwhile), turned the scroll's way.
		if (pointer.scrolling)
		{
			cursor.cursor = MouseCursorKind::Scroll;
			cursor.direction = ScrollDirection(pointer.scrollX, pointer.scrollY, mouse.cursorDirections[static_cast<std::size_t>(MouseCursorKind::Scroll)]);
			return;
		}
		cursor.direction = 0;
		if (cursor.cursor == MouseCursorKind::Scroll)
			cursor.cursor = MouseCursorKind::Arrow; // setScrolling(FALSE)
		if (context.Read<InteractionState>().dragSelecting)
			return; // m_isSelecting: no hints
		// A window that is not see-through under the pointer: the arrow.
		if (pointer.overInterface)
		{
			cursor.cursor = MouseCursorKind::Arrow;
			return;
		}
		// MOUSEMODE_GUI_COMMAND: the button's cursor over a valid target, else its invalid one.
		if (const GuiTargeting &targeting = context.Read<GuiTargeting>(); targeting.active)
		{
			// A context command (a special power): valid or invalid; any other needing a target (a guard): its cursor.
			if (targeting.kind == GuiCommandKind::FireWeapon && (targeting.options & content::button_option::ContextModeCommand) != 0)
				cursor.cursor = FireWeaponValid(context, pointer) ? targeting.cursor : targeting.invalidCursor;
			else if (targeting.kind != GuiCommandKind::SpecialPower)
				cursor.cursor = targeting.cursor;
			else
				cursor.cursor = GuiTargetValid(context, pointer) ? targeting.cursor : targeting.invalidCursor;
			return;
		}
		const auto lookup = context.Lookup<Lookup>();
		const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
		const bool forceAttack = pointer.ctrl;
		const auto under = PickAt(context, lookup, pointer.x, pointer.y, forceAttack);
		// CanSelectDrawable(draw, FALSE).
		const SelectionLook *underLook = under ? catalog.Of(under->definition) : nullptr;
		const bool drawSelectable = under && underLook != nullptr && Pickable(under->entity, *underLook, lookup, false);
		const Selection selection = SelectedNow(context, lookup);
		const bool placing = context.Read<BuildPlacement>().active;
		if (selection.current.empty())
		{
			// createMouseoverHint, nothing selected: one of mine that may be selected, else the arrow.
			if (!placing)
				cursor.cursor = drawSelectable && under->picked.side == PickSide::Mine ? MouseCursorKind::Select : MouseCursorKind::Arrow;
			return;
		}
		const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
		Hint hint = Evaluate(context, lookup, selection.mine, under ? &under->picked : nullptr, at, forceAttack, pointer.shift,
			AllSetRallyPoints(context, lookup, selection));
		// createCommandHint: an attack hint on an object black to the player is a move hint.
		if (under && (hint == Hint::AttackObject || hint == Hint::AttackAfterMoving) &&
			context.Read<engine::gameplay::ShroudMap>().StatusAt(context.Read<LocalPlayer>().player, ToFixed(under->x), ToFixed(under->y)) ==
				engine::gameplay::CellShroud::Shrouded)
			hint = Hint::Move;
		if (placing)
		{
			// MOUSEMODE_BUILD_PLACE.
			if (hint == Hint::Move)
				cursor.cursor = MouseCursorKind::Build;
			else if (hint == Hint::AttackObject || hint == Hint::AttackAfterMoving)
				cursor.cursor = MouseCursorKind::InvalidBuild;
			return;
		}
		// MOUSEMODE_DEFAULT: one selected object not the player's shows the arrow.
		if (selection.current.size() == 1 && selection.current.front().side != PickSide::Mine)
		{
			cursor.cursor = MouseCursorKind::Arrow;
			return;
		}
		const auto kindsOf = [&](ecs::Entity entity) -> std::uint16_t {
			const auto *ref = lookup.Get<engine::gameplay::DefinitionRef>(entity);
			const SelectionLook *look = ref != nullptr ? catalog.Of(ref->index) : nullptr;
			return look != nullptr ? look->kinds : 0;
		};
		switch (hint)
		{
		case Hint::Invalid: return;
		case Hint::Move:
			if (!drawSelectable && selection.current.size() == 1 && (kindsOf(selection.current.front().entity) & select_kind::Structure) != 0)
				cursor.cursor = MouseCursorKind::GenericInvalid;
			else if (drawSelectable && under->picked.side == PickSide::Mine && (underLook->kinds & select_kind::Mine) == 0)
				cursor.cursor = MouseCursorKind::Select;
			else
				cursor.cursor = MouseCursorKind::Move;
			return;
		case Hint::AttackObject: cursor.cursor = MouseCursorKind::AttackObj; return;
		case Hint::AttackAfterMoving: cursor.cursor = MouseCursorKind::OutRange; return;
		case Hint::ForceAttackObject: cursor.cursor = MouseCursorKind::ForceAttackObj; return;
		case Hint::ForceAttackGround: cursor.cursor = MouseCursorKind::ForceAttackGround; return;
		case Hint::GetRepaired: cursor.cursor = MouseCursorKind::GetRepaired; return;
		case Hint::Hijack:
		case Hint::ConvertToCarBomb: cursor.cursor = MouseCursorKind::EnterAggressive; return;
		case Hint::ImpossibleAttack: cursor.cursor = MouseCursorKind::GenericInvalid; return;
		case Hint::OverrideDestination: cursor.cursor = MouseCursorKind::ParticleUplinkCannon; return;
		// MSG_SET_RALLY_POINT_HINT: SET_RALLY_POINT over the ground (SELECTING over something selectable).
		case Hint::SetRallyPoint: cursor.cursor = drawSelectable ? MouseCursorKind::Select : MouseCursorKind::SetRallyPoint; return;
		}
	}

	// handleGuiCommand's validity for GUI_COMMAND_SPECIAL_POWER (canSelectedObjectsDoSpecialPower, the waiting command's
	// object): with COMMAND_OPTION_NEED_OBJECT_TARGET (first), the object under the pointer (resolveGuiCommandTarget: a
	// pickable one) when the host found its power may be fired at it (canDoSpecialPowerAtObject); with NEED_TARGET_POS, a
	// spot where ActionManager::canDoSpecialPowerAtLocation lets its power go for its player.
	static bool GuiTargetValid(ecs::SystemContext &context, const PointerInput &pointer)
	{
		GuiTargeting &targeting = context.Write<GuiTargeting>();
		if ((targeting.options & content::button_option::NeedObjectTarget) != 0)
		{
			const auto lookup = context.Lookup<Lookup>();
			const auto under = PickAt(context, lookup, pointer.x, pointer.y, false);
			targeting.hovered = under ? under->entity : ecs::Entity{};
			return under && targeting.validFor == under->entity;
		}
		if ((targeting.options & content::button_option::NeedTargetPos) == 0)
			return false;
		const auto lookup = context.Lookup<Lookup>();
		const auto *owner = lookup.Get<engine::gameplay::Owner>(targeting.source);
		const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
		if (owner == nullptr || !at)
			return false;
		return generalszh::gameplay::CanDoSpecialPowerAtLocation(targeting.powerType, owner->player,
			{interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])}, context.Read<engine::gameplay::GroundHeight>(),
			context.Read<engine::gameplay::ShroudMap>());
	}

	// CommandButton::isValidRelationshipTarget: the button takes an object of this side (one's own count as allies).
	static bool TakesRelationship(std::uint32_t options, PickSide side) noexcept
	{
		namespace bo = content::button_option;
		const std::uint32_t wanted = side == PickSide::Enemy ? bo::NeedTargetEnemy : side == PickSide::Civilian ? bo::NeedTargetNeutral : bo::NeedTargetAlly;
		return (options & wanted) != 0;
	}

	// InGameUI::canSelectedObjectsEffectivelyUseWeapon (SELECTION_ANY) through ActionManager::canFireWeaponAtObject /
	// canFireWeaponAtLocation: one of the player's selected has a weapon in the button's slot; at an object, one it may
	// attack (getAbleToAttackSpecificObject: an enemy's, or one a script made player-targetable not an ally's) that the
	// slot's weapon can hit (its kind). (A sniper's KILLPILOT rules are not ported.)
	static bool FireWeaponValid(ecs::SystemContext &context, const PointerInput &pointer)
	{
		const GuiTargeting &targeting = context.Read<GuiTargeting>();
		const auto lookup = context.Lookup<Lookup>();
		const Selection selection = SelectedNow(context, lookup);
		const auto &weapons = context.Read<engine::gameplay::WeaponCatalog>();
		const auto slotWeapon = [&](ecs::Entity unit) -> std::uint32_t {
			if (const auto *set = lookup.Get<engine::gameplay::WeaponSlots>(unit))
				return targeting.weaponSlot < set->slots.size() ? set->slots[targeting.weaponSlot].weapon : engine::gameplay::WeaponCatalog::None;
			const auto *armament = lookup.Get<engine::gameplay::Armament>(unit);
			return armament != nullptr && targeting.weaponSlot == 0 ? armament->weapon : engine::gameplay::WeaponCatalog::None;
		};
		if ((targeting.options & content::button_option::NeedObjectTarget) != 0)
		{
			const auto under = PickAt(context, lookup, pointer.x, pointer.y, false);
			if (!under)
				return false;
			const auto *targetable = lookup.Get<engine::gameplay::Targetable>(under->entity);
			if (targetable == nullptr || SelectionAttack(context, selection.mine, under->entity, lookup, false) == interaction_detail::AttackResult::NotPossible)
				return false;
			return std::ranges::any_of(selection.mine, [&](ecs::Entity unit) {
				const std::uint32_t weapon = slotWeapon(unit);
				return weapon != engine::gameplay::WeaponCatalog::None && engine::gameplay::CanTarget(weapons.At(weapon), targetable->classes);
			});
		}
		if ((targeting.options & content::button_option::NeedTargetPos) != 0 &&
			!GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y))
			return false;
		return std::ranges::any_of(selection.mine, [&](ecs::Entity unit) { return slotWeapon(unit) != engine::gameplay::WeaponCatalog::None; });
	}

	// issueSpecialPowerCommand: the power fired for the waiting command's object, and the wait is over
	// (setGUICommand(nullptr)); not on an invalid target.
	static void GuiClick(ecs::SystemContext &context, const PointerInput &pointer)
	{
		if (context.Read<GuiTargeting>().kind == GuiCommandKind::Guard)
		{
			GuardClick(context, pointer);
			return;
		}
		// FIRE_WEAPON: a context command (CONTEXTMODE_COMMAND: handleGuiCommand) only on a target its weapon may be used on
		// (canSelectedObjectsEffectivelyUseWeapon), the wait ending then; otherwise GUICommandTranslator's doFireWeaponCommand:
		// at the ground clicked (NEED_TARGET_POS: MSG_DO_WEAPON_AT_LOCATION), or at the object under the pointer the button
		// takes (validUnderCursor: its relationship to the player; MSG_DO_WEAPON_AT_OBJECT), the wait ending either way.
		if (context.Read<GuiTargeting>().kind == GuiCommandKind::FireWeapon)
		{
			const GuiTargeting waiting = context.Read<GuiTargeting>();
			const bool contextCommand = (waiting.options & content::button_option::ContextModeCommand) != 0;
			if (contextCommand && !FireWeaponValid(context, pointer))
				return;
			context.Write<GuiTargeting>() = {};
			const auto lookup = context.Lookup<Lookup>();
			const Selection selection = SelectedNow(context, lookup);
			if (selection.mine.empty())
				return;
			commands::FireWeapon fire{selection.mine, waiting.weaponSlot, 0, waiting.maxShots, {}, {}};
			if ((waiting.options & content::button_option::NeedTargetPos) != 0)
			{
				const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
				if (!at)
					return;
				fire.at = 1;
				fire.position = {interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])};
			}
			else if ((waiting.options & content::button_option::NeedObjectTarget) != 0)
			{
				const auto under = PickAt(context, lookup, pointer.x, pointer.y, false);
				if (!under || !TakesRelationship(waiting.options, under->picked.side))
					return;
				fire.at = 2;
				fire.target = under->entity;
			}
			context.Write<PlayerOrders>().pending.push_back(fire);
			return;
		}
		// GUICommandTranslator's doAttackMoveCommand: the selection attack-moves to the ground clicked (MSG_DO_ATTACKMOVETO);
		// the wait ends either way.
		if (context.Read<GuiTargeting>().kind == GuiCommandKind::AttackMove)
		{
			context.Write<GuiTargeting>() = {};
			const auto lookup = context.Lookup<Lookup>();
			const Selection selection = SelectedNow(context, lookup);
			const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
			if (!selection.mine.empty() && at)
				context.Write<PlayerOrders>().pending.push_back(
					commands::AttackMoveTo{selection.mine, {interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])}});
			return;
		}
		// GUICommandTranslator's doSetRallyPointCommand: the one selected structure's rally point at the ground clicked
		// (MSG_SET_RALLY_POINT); the wait ends either way.
		if (context.Read<GuiTargeting>().kind == GuiCommandKind::RallyPoint)
		{
			context.Write<GuiTargeting>() = {};
			const auto lookup = context.Lookup<Lookup>();
			const Selection selection = SelectedNow(context, lookup);
			const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
			if (!selection.current.empty() && at)
				context.Write<PlayerOrders>().pending.push_back(commands::SetRallyPoint{selection.current.front().entity,
					{interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])}});
			return;
		}
		if (!GuiTargetValid(context, pointer))
			return;
		GuiTargeting &targeting = context.Write<GuiTargeting>();
		auto &orders = context.Write<PlayerOrders>().pending;
		// MSG_DO_SPECIAL_POWER_AT_OBJECT for an object target, else MSG_DO_SPECIAL_POWER_AT_LOCATION.
		if ((targeting.options & content::button_option::NeedObjectTarget) != 0)
			orders.push_back(commands::UseSpecialPowerAtObject{targeting.source, targeting.power, targeting.validFor});
		else
		{
			const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
			orders.push_back(
				commands::UseSpecialPower{targeting.source, targeting.power, {interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])}, true});
		}
		targeting = {};
	}

	// GUICommandTranslator's doGuardCommand: with COMMAND_OPTION_NEED_OBJECT_TARGET, the selectable object under the pointer
	// whose relationship to the player the button takes (isValidObjectTarget: NEED_TARGET_ENEMY / ALLY / NEUTRAL_OBJECT;
	// one's own count as allies) is guarded (MSG_DO_GUARD_OBJECT); else the ground under the pointer (NEED_TARGET_POS) or,
	// without one, where the first selected stands (MSG_DO_GUARD_POSITION); by the player's selected units. The wait ends
	// either way (COMMAND_COMPLETE).
	static void GuardClick(ecs::SystemContext &context, const PointerInput &pointer)
	{
		GuiTargeting &targeting = context.Write<GuiTargeting>();
		const auto lookup = context.Lookup<Lookup>();
		const Selection selection = SelectedNow(context, lookup);
		auto &orders = context.Write<PlayerOrders>().pending;
		const GuiTargeting waiting = targeting;
		targeting = {};
		if (selection.current.empty())
			return;
		if ((waiting.options & content::button_option::NeedObjectTarget) != 0)
			if (const auto under = PickAt(context, lookup, pointer.x, pointer.y, false))
			{
				namespace bo = content::button_option;
				const std::uint32_t wanted = under->picked.side == PickSide::Enemy ? bo::NeedTargetEnemy
					: under->picked.side == PickSide::Civilian ? bo::NeedTargetNeutral : bo::NeedTargetAlly;
				if ((waiting.options & wanted) != 0)
				{
					orders.push_back(commands::GuardObject{selection.mine, under->entity, waiting.guardMode});
					return;
				}
			}
		Engine::Math::FixedVector2 spot;
		if ((waiting.options & content::button_option::NeedTargetPos) != 0)
		{
			const auto at = GroundUnder(context.Read<InteractionView>(), context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
			if (!at)
				return;
			spot = {interaction_detail::ToFixed((*at)[0]), interaction_detail::ToFixed((*at)[1])};
		}
		else
		{
			const auto *first = lookup.Get<engine::gameplay::Transform>(selection.current.front().entity);
			if (first == nullptr)
				return;
			spot = first->position.XY();
		}
		orders.push_back(commands::GuardPosition{selection.mine, spot, waiting.guardMode});
	}

	static void LeftClick(ecs::SystemContext &context, const PointerInput &pointer, bool isPoint)
	{
		using namespace interaction_detail;
		const InteractionView &view = context.Read<InteractionView>();
		const InteractionState &state = context.Read<InteractionState>();
		const SelectionCatalog &catalog = context.Read<SelectionCatalog>();
		const std::uint32_t local = context.Read<LocalPlayer>().player;
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		auto &selected = context.Side<SideTables, Selected>();
		const auto lookup = context.Lookup<Lookup>();
		const bool forceAttack = pointer.ctrl;
		const bool prefer = pointer.shift;

		// What is under the pointer or in the box.
		std::vector<Candidate> candidates;
		std::optional<Candidate> nearest;
		if (isPoint)
		{
			nearest = PickAt(context, lookup, pointer.x, pointer.y, forceAttack);
			if (nearest)
				candidates.push_back(*nearest);
		}
		else
		{
			const float x0 = std::min(state.leftAnchorX, pointer.x), x1 = std::max(state.leftAnchorX, pointer.x);
			const float y0 = std::min(state.leftAnchorY, pointer.y), y1 = std::max(state.leftAnchorY, pointer.y);
			context.Read<engine::gameplay::VisibleObjects>().ForEach([&](const engine::gameplay::VisibleObject &object) {
				const SelectionLook *look = catalog.Of(object.definition);
				if (look == nullptr || !Pickable(object.entity, *look, lookup, forceAttack))
					return;
				const float px = Engine::Math::ToFloat(object.transform.position.x), py = Engine::Math::ToFloat(object.transform.position.y),
							pz = Engine::Math::ToFloat(object.transform.position.z);
				const auto picked = Classify(object.entity, lookup, catalog, object.definition, object.player, relationships, local);
				float sx = 0, sy = 0;
				if (picked && view.Project(px, py, pz, sx, sy) && sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1)
					candidates.push_back({object.entity, 0.0f, *picked, px, py, object.definition});
			});
		}

		const Selection now = SelectedNow(context, lookup);
		const std::vector<ecs::Entity> &mine = now.mine;
		std::vector<PickedObject> picked;
		bool allSelected = true;
		for (const Candidate &candidate : candidates)
		{
			picked.push_back(candidate.picked);
			allSelected = allSelected && selected.Get(candidate.entity) != nullptr;
		}
		const auto underPointer = GroundUnder(view, context.Read<engine::gameplay::GroundHeight>(), pointer.x, pointer.y);
		const bool allRally = AllSetRallyPoints(context, lookup, now);
		const auto hasCommand = [&](std::size_t index) {
			// evaluateContextCommand (EVALUATE_ONLY): a command other than a move onto an object.
			const Hint hint = Evaluate(context, lookup, mine, &picked[index], underPointer, forceAttack, prefer);
			return hint != Hint::Invalid && hint != Hint::Move;
		};
		const ClickSelection selection = LeftClickSelection(now.current, picked, isPoint, prefer, forceAttack, false, allSelected, hasCommand);
		switch (selection.outcome)
		{
		case ClickOutcome::Replace:
			selected.Clear();
			[[fallthrough]];
		case ClickOutcome::Add:
			for (const ecs::Entity entity : selection.entities)
				if (selected.Get(entity) == nullptr)
					selected.Emplace(entity);
			return;
		case ClickOutcome::Deselect:
			for (const ecs::Entity entity : selection.entities)
				selected.Erase(entity);
			return;
		case ClickOutcome::Pass:
			break;
		}

		// CommandTranslator, MSG_MOUSE_LEFT_CLICK: a point, with a selection of the player's.
		if (!isPoint || mine.empty())
			return;
		auto &orders = context.Write<PlayerOrders>().pending;
		const Hint hint = Evaluate(context, lookup, mine, nearest ? &nearest->picked : nullptr, underPointer, forceAttack, prefer, allRally);
		switch (hint)
		{
		// handleSpecialPowerOverrideDestinationCommand: MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION at the spot, for the
		// whole selection (no specific source).
		case Hint::OverrideDestination:
			orders.push_back(commands::SpecialPowerDestination{mine, {ToFixed((*underPointer)[0]), ToFixed((*underPointer)[1])}});
			return;
		case Hint::GetRepaired: orders.push_back(commands::GetRepaired{mine, nearest->entity}); return;
		// handleConvertObjectToCarBombCommand: an enter order (MSG_ENTER) for the whole selection.
		case Hint::Hijack:
		case Hint::ConvertToCarBomb: orders.push_back(commands::Enter{mine, nearest->entity}); return;
		case Hint::AttackObject:
		case Hint::AttackAfterMoving:
		case Hint::ForceAttackObject: orders.push_back(commands::Attack{mine, nearest->entity}); return;
		// MSG_DO_FORCE_ATTACK_GROUND: fire at the spot.
		case Hint::ForceAttackGround:
			orders.push_back(commands::AttackPosition{mine, {ToFixed((*underPointer)[0]), ToFixed((*underPointer)[1])}});
			return;
		// handleDefaultMoveCommand: a move only onto the ground (never onto an object).
		case Hint::Move:
			if (!nearest && underPointer)
				orders.push_back(commands::MoveTo{mine, {ToFixed((*underPointer)[0]), ToFixed((*underPointer)[1])}});
			return;
		// handleSetRallyPointCommand: each selected one's rally point there.
		case Hint::SetRallyPoint:
			for (const ecs::Entity factory : now.current | std::views::transform([](const PickedObject &object) { return object.entity; }))
				orders.push_back(commands::SetRallyPoint{factory, {ToFixed((*underPointer)[0]), ToFixed((*underPointer)[1])}});
			return;
		case Hint::ImpossibleAttack:
		case Hint::Invalid: return;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::PointerInteractionSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.pointer_interaction";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
