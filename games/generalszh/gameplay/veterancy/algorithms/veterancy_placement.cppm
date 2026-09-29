export module games.generalszh.gameplay.veterancy.algorithms.veterancy_placement;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.rts.veterancy.algorithms.experience;
import engine.gameplay.rts.veterancy.algorithms.promotion;
import engine.gameplay.rts.upgrades.components.upgradable;
import engine.gameplay.rts.veterancy.resources.promotions;

// Putting an object at a veterancy level from outside its own experience (map
// properties, create modules, crates, creation lists), and experience it earns outside of kills.
export namespace generalszh::gameplay
{
// Object::updateObjValuesFromMapProperties' objectVeterancy: a trainable object starts at that level
// (ExperienceTracker::setVeterancyLevel: the level's minimum points, then onVeterancyLevelChanged). `untrainable`: an
// object that cannot train too (setVeterancyLevel itself never asks).
void PlaceAtVeterancy(GameWorld &game, ecs::Entity entity, std::int64_t level, bool untrainable = false)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	gp::Experience *experience = world.Get<gp::Experience>(entity);
	if (experience == nullptr || (!experience->trainable && !untrainable) || level < 0 || level >= static_cast<std::int64_t>(gp::VeterancyLevelCount) ||
		experience->level == level)
		return;
	const gp::VeterancyCatalog &catalog = game.templates.veterancy;
	const std::uint8_t from = experience->level, to = static_cast<std::uint8_t>(level);
	gp::SetVeterancyLevel(*experience, catalog.Of(world.Get<gp::DefinitionRef>(entity)->index), to);
	if (gp::WeaponBonusConditions *conditions = world.Get<gp::WeaponBonusConditions>(entity))
		gp::SetLevelBonus(*conditions, catalog, to, game.tick);
	if (gp::Health *health = world.Get<gp::Health>(entity))
		gp::ScaleHealthForLevel(*health, catalog, from, to);
	if (const std::uint32_t upgrade = catalog.levelUpgrade[to]; upgrade != gp::VeterancyCatalog::NoUpgrade)
	{
		if (!world.Has<gp::Upgradable>(entity))
			world.Add<gp::Upgradable>(entity);
		gp::Upgradable &upgradable = *world.Get<gp::Upgradable>(entity);
		upgradable.completed.Set(upgrade);
		upgradable.stale = 1;
	}
}

// ExperienceTracker::addExperiencePoints(gain, canScaleForBonus) outside of kills (an ability's AwardXPForTriggering):
// into its experience sink while there is one (the gain times its scalar), else its own; a changed level as the
// veterancy pass has it (onVeterancyLevelChanged: its weapon bonus, health, level upgrade; promoted, it shows).
void AwardExperience(GameWorld &game, ecs::Entity entity, std::int32_t gain, bool canScale = true)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const gp::Experience *own = world.IsAlive(entity) ? world.Get<gp::Experience>(entity) : nullptr;
	if (own == nullptr)
		return;
	ecs::Entity into = entity;
	if (own->sink != ecs::Entity{} && world.IsAlive(own->sink) && world.Get<gp::Experience>(own->sink) != nullptr)
	{
		gain = gp::ScaledForSink(*own, gain);
		into = own->sink;
	}
	const auto *ref = world.Get<gp::DefinitionRef>(into);
	gp::Experience &experience = *world.Get<gp::Experience>(into);
	const gp::VeterancyCatalog &catalog = game.templates.veterancy;
	const std::uint8_t from = gp::AddExperiencePoints(experience, catalog.Of(ref != nullptr ? ref->index : 0xFFFFFFFFu), gain, canScale);
	const std::uint8_t to = experience.level;
	if (from == to)
		return;
	if (to > from)
		if (auto *promotions = world.FindResource<gp::Promotions>())
			promotions->list.push_back({into, from, to});
	if (gp::WeaponBonusConditions *conditions = world.Get<gp::WeaponBonusConditions>(into))
		gp::SetLevelBonus(*conditions, catalog, to, game.tick);
	if (gp::Health *health = world.Get<gp::Health>(into))
		gp::ScaleHealthForLevel(*health, catalog, from, to);
	if (const std::uint32_t upgrade = catalog.levelUpgrade[to]; upgrade != gp::VeterancyCatalog::NoUpgrade)
	{
		if (!world.Has<gp::Upgradable>(into))
			world.Add<gp::Upgradable>(into);
		gp::Upgradable &upgradable = *world.Get<gp::Upgradable>(into);
		upgradable.completed.Set(upgrade);
		upgradable.stale = 1;
	}
}

}
