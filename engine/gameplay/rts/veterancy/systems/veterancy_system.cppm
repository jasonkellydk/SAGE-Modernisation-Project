export module engine.gameplay.rts.veterancy.systems.veterancy_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.veterancy.algorithms.experience;
export import engine.gameplay.rts.veterancy.resources.veterancy_catalog;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.rts.veterancy.algorithms.promotion;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.rts.upgrades.components.upgradable;
export import engine.gameplay.rts.upgrades.resources.object_upgrade_grants;

// Object::scoreTheKill and ExperienceTracker, over this tick's deaths in
// order: a killer still in the world scores a victim of a playable side
// (PlayerTemplate PlayableSide) that scores kills (not IGNORED_IN_GUI), its
// enemy and another player's: its player earns the victim's skill points
// (not for one under construction), and it gains experience when it can take experience (trainable, or it
// passes its gains to a sink): the victim's ExperienceValue at its level,
// scaled by the killer's scalar, into the killer or its sink. A new level
// (Object::onVeterancyLevelChanged; retail: even for a dead killer) swaps its
// levels' weapon bonus bits (each change re-times its weapons),
// scales its max health by the level's HealthBonus over the old one's,
// keeping the ratio (ActiveBody::onVeterancyLevelChanged), and gives it the
// level's veterancy upgrade (Object::giveUpgrade).
export import engine.gameplay.rts.veterancy.resources.promotions;
export import engine.gameplay.rts.veterancy.resources.experience_awards;
export import engine.gameplay.rts.veterancy.resources.skill_point_awards;
export import engine.gameplay.rts.construction.components.under_construction;

export namespace engine::gameplay
{
struct VeterancySystem
{
	using Query = ecs::Query<ecs::Read<Experience>>;
	using Lookup = ecs::Lookup<ecs::Read<Experience>, ecs::Read<DefinitionRef>, ecs::Read<Owner>, ecs::Read<Health>, ecs::Read<WeaponBonusConditions>,
		ecs::Read<Upgradable>, ecs::Read<UnderConstruction>>;
	using Resources = ecs::Resources<ecs::Read<Deaths>, ecs::Read<VeterancyCatalog>, ecs::Read<Relationships>, ecs::Read<TeamRoster>,
		ecs::Write<ObjectUpgradeGrants>, ecs::Write<Promotions>, ecs::Write<ExperienceAwards>, ecs::Write<SkillPointAwards>>;

	void Execute(ecs::SystemContext &context) const
	{
		const Deaths &deaths = context.Read<Deaths>();
		context.Write<Promotions>().list.clear();
		auto &skillPoints = context.Write<SkillPointAwards>().list;
		skillPoints.clear();
		const VeterancyCatalog &catalog = context.Read<VeterancyCatalog>();
		const Relationships &relationships = context.Read<Relationships>();
		const TeamRoster &roster = context.Read<TeamRoster>();
		const auto lookup = context.Lookup<Lookup>();
		const std::uint64_t tick = context.Tick();
		// Experience as this tick's kills leave it, in order, with the level each had first.
		struct Tracked
		{
			ecs::Entity entity;
			Experience experience;
			std::uint8_t firstLevel{0};
		};
		std::vector<Tracked> tracked;
		const auto track = [&](ecs::Entity entity) -> Tracked * {
			for (Tracked &entry : tracked)
				if (entry.entity == entity)
					return &entry;
			const Experience *experience = lookup.Get<Experience>(entity);
			if (experience == nullptr)
				return nullptr;
			tracked.push_back({entity, *experience, experience->level});
			return &tracked.back();
		};
		const auto definitionOf = [&](ecs::Entity entity) -> const VeterancyDefinition & {
			const DefinitionRef *ref = lookup.Get<DefinitionRef>(entity);
			return catalog.Of(ref != nullptr ? ref->index : 0xFFFFFFFFu);
		};
		// ExperienceTracker::addExperiencePoints: into the sink while it is there, else its own.
		const auto addPoints = [&](Tracked &killer, std::int32_t gain, bool canScale) {
			if (killer.experience.sink != ecs::Entity{} && lookup.IsAlive(killer.experience.sink))
				if (Tracked *sink = track(killer.experience.sink))
				{
					AddExperiencePoints(sink->experience, definitionOf(sink->entity), ScaledForSink(killer.experience, gain), canScale);
					return;
				}
			AddExperiencePoints(killer.experience, definitionOf(killer.entity), gain, canScale);
		};
		deaths.ForEach([&](const Death &death) {
			if (death.killer == ecs::Entity{} || !lookup.IsAlive(death.killer) || !lookup.IsAlive(death.entity))
				return;
			const Owner *victimOwner = lookup.Get<Owner>(death.entity);
			const Owner *killerOwner = lookup.Get<Owner>(death.killer);
			if (victimOwner == nullptr || killerOwner == nullptr)
				return;
			if (victimOwner->player >= roster.PlayerCount() || !roster.PlayerAt(victimOwner->player).playable)
				return;
			const VeterancyDefinition &victim = definitionOf(death.entity);
			if (!victim.scoresKills)
				return;
			if (!relationships.Enemies(killerOwner->player, victimOwner->player) || killerOwner->player == victimOwner->player)
				return;
			// Player::addSkillPointsForKill: nothing for what was still being built.
			if (lookup.Get<UnderConstruction>(death.entity) == nullptr)
			{
				const Experience *victimLevel = lookup.Get<Experience>(death.entity);
				const DefinitionRef *victimKind = lookup.Get<DefinitionRef>(death.entity);
				skillPoints.push_back({killerOwner->player, victim.skillValue[victimLevel != nullptr ? victimLevel->level : 0u], death.killer,
					victimKind != nullptr ? victimKind->index : 0xFFFFFFFFu, victimOwner->player});
			}
			Tracked *killer = track(death.killer);
			if (killer == nullptr)
				return;
			// isAcceptingExperiencePoints: trainable, or it has a sink.
			if (!killer->experience.trainable && killer->experience.sink == ecs::Entity{})
				return;
			const Experience *victimExperience = lookup.Get<Experience>(death.entity);
			const std::uint8_t victimLevel = victimExperience != nullptr ? victimExperience->level : std::uint8_t{0};
			addPoints(*killer, victim.value[victimLevel], true);
		});
		// ExperienceTracker::gainExpForLevel: just enough for so many more levels (at most the last), as points.
		auto &awards = context.Write<ExperienceAwards>().list;
		for (const ExperienceAward &award : awards)
		{
			if (!lookup.IsAlive(award.entity))
				continue;
			Tracked *entry = track(award.entity);
			if (entry == nullptr)
				continue;
			const std::uint32_t level = std::min<std::uint32_t>(entry->experience.level + award.levels, VeterancyLevelCount - 1u);
			if (level <= entry->experience.level)
				continue;
			addPoints(*entry, definitionOf(award.entity).required[level] - entry->experience.points, award.canScale);
		}
		awards.clear();
		auto &commands = context.Commands();
		auto &grants = context.Write<ObjectUpgradeGrants>().list;
		auto &promotions = context.Write<Promotions>().list;
		for (const Tracked &entry : tracked)
		{
			commands.Set<Experience>(entry.entity, entry.experience);
			const std::uint8_t from = entry.firstLevel, to = entry.experience.level;
			if (from == to)
				continue;
			if (to > from)
				promotions.push_back({entry.entity, from, to});
			if (const WeaponBonusConditions *now = lookup.Get<WeaponBonusConditions>(entry.entity))
			{
				WeaponBonusConditions conditions = *now;
				SetLevelBonus(conditions, catalog, to, tick);
				commands.Set<WeaponBonusConditions>(entry.entity, conditions);
			}
			if (const Health *health = lookup.Get<Health>(entry.entity))
			{
				Health changed = *health;
				ScaleHealthForLevel(changed, catalog, from, to);
				commands.Set<Health>(entry.entity, changed);
			}
			if (const std::uint32_t upgrade = catalog.levelUpgrade[to]; upgrade != VeterancyCatalog::NoUpgrade)
			{
				if (lookup.Get<Upgradable>(entry.entity) != nullptr)
					grants.push_back({entry.entity, upgrade});
				else
				{
					Upgradable own;
					own.completed.Set(upgrade);
					commands.Add<Upgradable>(entry.entity, own);
				}
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::VeterancySystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.veterancy";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>; // the session orders it after the health pass
};
}
