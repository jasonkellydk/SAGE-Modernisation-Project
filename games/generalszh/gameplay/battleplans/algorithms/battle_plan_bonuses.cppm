export module games.generalszh.gameplay.battleplans.algorithms.battle_plan_bonuses;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.battleplans.components.battle_plan;
export import games.generalszh.gameplay.battleplans.resources.battle_plan_players;
import games.generalszh.gameplay.ai.components.repulsion;
import games.generalszh.content.combat.weapon_bonus_content;
import games.generalszh.content.combat.loadout_content;
import games.generalszh.content.control_bar.command_catalog;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.producer;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.health.components.damage_scalar;
import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.rts.combat.components.turret;
import engine.gameplay.rts.vision.components.vision;
import engine.gameplay.rts.loadout.components.loadout;
import engine.ecs.query.query;

// The army's side of the Strategy Center's battle plans:
// - ApplyPlanBonusesTo (localApplyBattlePlanBonusesToObject): a troop of the kinds the bonuses reach (a projectile judged
//   by what fired it) has its body's damage scalar times the armor scalar and its vision and shroud clearing range times
//   the sight scalar (not a projectile's), and each plan's weapon bonus condition set while it counts any, else cleared.
// - ChangeBattlePlan (Player::changeBattlePlan): a player's count of a plan up or down; the first of a plan gives its
//   bonuses to the player's army, the last one gone takes them back (the scalars inverted, the plan's bonus -1).
// - Each change is folded into the player's own bonuses (applyBattlePlanBonusesForPlayerObjects) and every object of
//   the player given the player's plans with the change's scalars (the fork's fix: all the player's plans, not only the
//   latest, keep their weapon bonuses).
// - MoveBattlePlan (onCapture, the fork's fix): a captured center's plan leaves the old player for the new one.
// - BattlePlanObjectCreated (Object::initObject, Weapon's projectiles) and BattlePlanOwnerChanged
//   (Player::becomingTeamMember): a new object gets its player's bonuses; one changing sides loses the old player's
//   (every plan's weapon bonus cleared) and gets the new one's.
export namespace generalszh::gameplay
{
namespace battle_plan_detail
{
inline bool AnyKind(const content::KindOfMask &kinds, const content::KindOfMask &mask) noexcept
{
	for (std::size_t word = 0; word < kinds.size(); ++word)
		if ((kinds[word] & mask[word]) != 0)
			return true;
	return false;
}

inline const content::ObjectDefinition *KindOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<engine::gameplay::DefinitionRef>(entity) : nullptr;
	return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
}

inline bool Qualifies(const content::KindOfMask &kinds, const content::KindOfMask &valid, const content::KindOfMask &invalid) noexcept
{
	return AnyKind(kinds, valid) && !AnyKind(kinds, invalid);
}

// Object::setVisionRange and setShroudClearingRange, each times `scalar` (its idle scan reaching its weapon's range).
inline void ScaleSight(GameWorld &game, ecs::Entity entity, Engine::Math::Fixed scalar)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (auto *aggression = world.Get<gp::Aggression>(entity))
	{
		aggression->vision = aggression->vision * scalar;
		Engine::Math::Fixed reach;
		if (const auto *armament = world.Get<gp::Armament>(entity); armament != nullptr && armament->weapon != gp::WeaponCatalog::None)
			reach = game.templates.weapons.At(armament->weapon).attackRange;
		aggression->scanRange = std::max(aggression->vision, reach);
	}
	if (auto *runner = world.Get<Repulsable>(entity))
		runner->vision = runner->vision * scalar;
	if (auto *vision = world.Get<gp::Vision>(entity))
		vision->clearingRange = vision->clearingRange * scalar;
}

// The inverse of a scalar (1 / MAX(scalar, 0.01)).
inline Engine::Math::Fixed Inverse(Engine::Math::Fixed scalar)
{
	return Engine::Math::Fixed::One() / std::max(scalar, Engine::Math::Fixed::FromRatio(1, 100));
}
}

inline void ApplyPlanBonusesTo(GameWorld &game, ecs::Entity entity, const PlanBonuses &bonus)
{
	namespace gp = engine::gameplay;
	using namespace battle_plan_detail;
	auto &world = game.world;
	const content::ObjectDefinition *kind = KindOf(game, entity);
	if (kind == nullptr)
		return;
	static const std::size_t projectileBit = content::KindOfBit("PROJECTILE");
	const bool projectile = content::HasKindOf(kind->kinds, projectileBit);
	const content::ObjectDefinition *judged = kind;
	if (projectile)
	{
		const auto *producer = world.Get<gp::Producer>(entity);
		judged = producer != nullptr ? KindOf(game, producer->entity) : nullptr;
	}
	if (judged == nullptr || !Qualifies(judged->kinds, bonus.valid, bonus.invalid))
		return;
	if (!projectile)
	{
		// Really important to not apply certain bonuses like health augmentation to projectiles!
		if (bonus.armorScalar != Engine::Math::Fixed::One() && world.Has<gp::Health>(entity))
		{
			if (!world.Has<gp::DamageScalar>(entity))
				world.Add<gp::DamageScalar>(entity);
			auto &body = *world.Get<gp::DamageScalar>(entity);
			body.scalar = body.scalar * bonus.armorScalar;
		}
		if (bonus.sightScalar != Engine::Math::Fixed::One())
			ScaleSight(game, entity, bonus.sightScalar);
	}
	if (!world.Has<gp::WeaponBonusConditions>(entity))
		world.Add<gp::WeaponBonusConditions>(entity);
	auto &conditions = *world.Get<gp::WeaponBonusConditions>(entity);
	namespace wb = content::weapon_bonus;
	gp::SetWeaponBonus(conditions, wb::BattleplanBombardment, bonus.bombardment > 0, game.tick);
	gp::SetWeaponBonus(conditions, wb::BattleplanHoldTheLine, bonus.holdTheLine > 0, game.tick);
	gp::SetWeaponBonus(conditions, wb::BattleplanSearchAndDestroy, bonus.searchAndDestroy > 0, game.tick);
}

// Player::applyBattlePlanBonusesForPlayerObjects.
inline void ApplyPlanBonusesForPlayer(GameWorld &game, std::uint32_t player, const PlanBonuses &bonus)
{
	PlayerBattlePlans *plans = game.world.Resource<BattlePlanPlayers>().Of(player);
	if (plans == nullptr)
		return;
	if (!plans->made)
	{
		plans->made = true;
		plans->bonuses = bonus;
	}
	else
	{
		PlanBonuses &own = plans->bonuses;
		own.armorScalar = own.armorScalar * bonus.armorScalar;
		own.sightScalar = own.sightScalar * bonus.sightScalar;
		own.bombardment = std::max(0, own.bombardment + bonus.bombardment);
		own.holdTheLine = std::max(0, own.holdTheLine + bonus.holdTheLine);
		own.searchAndDestroy = std::max(0, own.searchAndDestroy + bonus.searchAndDestroy);
	}
	PlanBonuses applied = plans->bonuses;
	applied.armorScalar = bonus.armorScalar;
	applied.sightScalar = bonus.sightScalar;
	// iterateObjects: every object of the player.
	std::vector<ecs::Entity> army;
	ecs::Query<ecs::Read<engine::gameplay::Owner>> owned(game.world);
	owned.ForEachChunk([&](auto chunk) {
		const auto owners = chunk.template Get<engine::gameplay::Owner>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < owners.size(); ++row)
			if (owners[row].player == player)
				army.push_back(entities[row]);
	});
	for (const ecs::Entity entity : army)
		ApplyPlanBonusesTo(game, entity, applied);
}

inline void ChangeBattlePlan(GameWorld &game, std::uint32_t player, PlanStatus plan, std::int32_t delta, const PlanBonuses &bonus)
{
	PlayerBattlePlans *plans = game.world.Resource<BattlePlanPlayers>().Of(player);
	if (plans == nullptr || plan == PlanStatus::None)
		return;
	std::int32_t &count = plans->counts[static_cast<std::size_t>(plan) - 1];
	count += delta;
	if (count == 1 && delta == 1)
		ApplyPlanBonusesForPlayer(game, player, bonus);
	else if (count == 0 && delta == -1)
	{
		PlanBonuses inverted = bonus;
		inverted.armorScalar = battle_plan_detail::Inverse(bonus.armorScalar);
		inverted.sightScalar = battle_plan_detail::Inverse(bonus.sightScalar);
		if (inverted.bombardment > 0)
			inverted.bombardment = -1;
		if (inverted.holdTheLine > 0)
			inverted.holdTheLine = -1;
		if (inverted.searchAndDestroy > 0)
			inverted.searchAndDestroy = -1;
		ApplyPlanBonusesForPlayer(game, player, inverted);
	}
}

// A Strategy Center's m_bonuses while its player's army has `plan` from it.
inline PlanBonuses BonusesOf(const BattlePlanConfig &config, PlanStatus plan)
{
	PlanBonuses bonus;
	bonus.valid = config.valid;
	bonus.invalid = config.invalid;
	switch (plan)
	{
	case PlanStatus::Bombardment: bonus.bombardment = 1; break;
	case PlanStatus::HoldTheLine:
		bonus.armorScalar = config.holdTheLineArmorScalar;
		bonus.holdTheLine = 1;
		break;
	case PlanStatus::SearchAndDestroy:
		bonus.searchAndDestroy = 1;
		bonus.sightScalar = config.searchAndDestroySightScalar;
		break;
	case PlanStatus::None: break;
	}
	return bonus;
}

// Player::applyBattlePlanBonusesForObject, while the player has any plan.
inline void BattlePlanObjectCreated(GameWorld &game, ecs::Entity entity)
{
	const auto *owner = game.world.Get<engine::gameplay::Owner>(entity);
	const auto *players = game.world.FindResource<BattlePlanPlayers>();
	const PlayerBattlePlans *plans = owner != nullptr && players != nullptr ? players->Of(owner->player) : nullptr;
	if (plans != nullptr && plans->Active() > 0)
		ApplyPlanBonusesTo(game, entity, plans->bonuses);
}

// Player::becomingTeamMember (leaving `from`'s teams for `to`'s).
inline void BattlePlanOwnerChanged(GameWorld &game, ecs::Entity entity, std::uint32_t from, std::uint32_t to)
{
	auto *players = game.world.FindResource<BattlePlanPlayers>();
	if (players == nullptr || from == to)
		return;
	if (const PlayerBattlePlans *old = players->Of(from); old != nullptr && old->Active() > 0)
	{
		// removeBattlePlanBonusesForObject: the scalars inverted, every plan's weapon bonus cleared.
		PlanBonuses bonus;
		bonus.armorScalar = battle_plan_detail::Inverse(old->bonuses.armorScalar);
		bonus.sightScalar = battle_plan_detail::Inverse(old->bonuses.sightScalar);
		bonus.bombardment = bonus.holdTheLine = bonus.searchAndDestroy = -3; // -ALL_PLANS
		bonus.valid = old->bonuses.valid;
		bonus.invalid = old->bonuses.invalid;
		ApplyPlanBonusesTo(game, entity, bonus);
	}
	if (const PlayerBattlePlans *now = players->Of(to); now != nullptr && now->Active() > 0)
		ApplyPlanBonusesTo(game, entity, now->bonuses);
}

// BattlePlanUpdate::onCapture: the plan its army had moves from `from` to `to` (and Player::becomingTeamMember's).
inline void MoveBattlePlan(GameWorld &game, ecs::Entity entity, std::uint32_t from, std::uint32_t to)
{
	BattlePlanOwnerChanged(game, entity, from, to);
	const auto *state = game.world.Get<BattlePlan>(entity);
	const auto *ref = state != nullptr ? game.world.Get<engine::gameplay::DefinitionRef>(entity) : nullptr;
	const BattlePlanConfig *config = ref != nullptr ? game.templates.BattlePlanOf(ref->index) : nullptr;
	if (config == nullptr || from == to || state->affecting == PlanStatus::None)
		return;
	const PlanBonuses bonus = BonusesOf(*config, state->affecting);
	ChangeBattlePlan(game, from, state->affecting, -1, bonus);
	ChangeBattlePlan(game, to, state->affecting, 1, bonus);
}

// BattlePlanUpdate::onObjectCreated: the center's WEAPONSET_VETERAN set and its turret off till a bombardment.
inline void InitBattlePlan(GameWorld &game, ecs::Entity center)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.Has<BattlePlan>(center))
		world.Add<BattlePlan>(center);
	if (auto *loadout = world.Get<gp::Loadout>(center))
		loadout->weaponFlags |= content::SetFlag(content::WeaponSetFlagNames, "VETERAN");
	if (auto *turret = world.Get<gp::Turret>(center))
		turret->enabled = false;
}

// BattlePlanUpdate::initiateIntentToDoSpecialPower: the plan the player chose (OPTION_ONE bombardment, OPTION_TWO hold
// the line, OPTION_THREE search and destroy); true when `center`'s plans take `power` with one of them.
inline bool ChooseBattlePlan(GameWorld &game, ecs::Entity center, std::uint32_t power, std::uint32_t options)
{
	auto *plan = game.world.IsAlive(center) ? game.world.Get<BattlePlan>(center) : nullptr;
	const auto *ref = plan != nullptr ? game.world.Get<engine::gameplay::DefinitionRef>(center) : nullptr;
	const BattlePlanConfig *config = ref != nullptr ? game.templates.BattlePlanOf(ref->index) : nullptr;
	if (config == nullptr || config->power != power)
		return false;
	namespace option = content::button_option;
	if ((options & option::OptionOne) != 0)
		plan->desired = PlanStatus::Bombardment;
	else if ((options & option::OptionTwo) != 0)
		plan->desired = PlanStatus::HoldTheLine;
	else if ((options & option::OptionThree) != 0)
		plan->desired = PlanStatus::SearchAndDestroy;
	else
		return false;
	return true;
}
}
