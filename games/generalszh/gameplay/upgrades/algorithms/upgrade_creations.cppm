export module games.generalszh.gameplay.upgrades.algorithms.upgrade_creations;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import Engine.Core.Math.Matrix3;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.rts.death.definitions.death_definition;
import engine.gameplay.rts.veterancy.components.experience;
import games.generalszh.gameplay.ai.algorithms.ai_team_building;
import games.generalszh.gameplay.ai.resources.ai_players;
import games.generalszh.gameplay.combat.systems.cooldown_creation_system;
import games.generalszh.gameplay.creation.algorithms.creation_list_runner;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.mines.algorithms.minefields;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import games.generalszh.gameplay.sciences.algorithms.general_ranks;
import games.generalszh.gameplay.score.algorithms.scoring;
import games.generalszh.gameplay.upgrades.resources.upgrade_effects;

// Upgrades that make things, after the tick's systems: ObjectCreationUpgrade's creation lists, GenerateMinefieldBehavior's
// upgrade minefields, ReplaceObjectUpgrade's replacements (built as if just finished) and GrantScienceUpgrade's sciences.
export namespace generalszh::gameplay
{
namespace gp = engine::gameplay;

// ObjectCreationUpgrade::upgradeImplementation: each upgraded object's creation list, from where it stands.
inline void RunCooldownCreations(GameWorld &game)
{
	std::vector<CooldownCreationEvent> events;
	game.world.Resource<CooldownCreationEvents>().AppendTo(events);
	game.world.Resource<CooldownCreationEvents>().Reset(0);
	for (const CooldownCreationEvent &event : events)
	{
		const auto *at = game.world.IsAlive(event.entity) ? game.world.Get<gp::Transform>(event.entity) : nullptr;
		if (at == nullptr || event.creation == 0xFFFFFFFFu)
			continue;
		const auto *member = game.world.Get<gp::TeamMember>(event.entity);
		const auto *level = game.world.Get<gp::Experience>(event.entity);
		RunCreationList(game, game.templates.DeathEffectName(gp::DeathEffectKind::Objects, event.creation),
			{at->position, at->facing, member != nullptr ? member->team : 0xFFFFFFFFu, event.entity, level != nullptr ? level->level : 0u, event.lifetimeTicks});
	}
}

inline void RunUpgradeCreations(GameWorld &game)
{
	const std::vector<UpgradeCreation> creations = game.world.Resource<UpgradeCreations>().list;
	for (const UpgradeCreation &creation : creations)
	{
		// GenerateMinefieldBehavior::upgradeImplementation: it lays its minefield.
		if (creation.minefield)
		{
			const auto *ref = game.world.IsAlive(creation.entity) ? game.world.Get<gp::DefinitionRef>(creation.entity) : nullptr;
			const auto *at = ref != nullptr ? game.world.Get<gp::Transform>(creation.entity) : nullptr;
			if (at != nullptr)
			{
				const auto *member = game.world.Get<gp::TeamMember>(creation.entity);
				PlaceMines(game, {creation.entity, ref->index, *at, member != nullptr ? member->team : 0xFFFFFFFFu}, false);
			}
			continue;
		}
		// ReplaceObjectUpgrade::upgradeImplementation: it goes (destroyObject: no death) and its replacement is made where
		// it was, on its team, as if just built (onBuildComplete, onStructureConstructionComplete: not a rebuild).
		if (creation.replacement != 0xFFFFFFFFu)
		{
			const auto *at = game.world.IsAlive(creation.entity) ? game.world.Get<gp::Transform>(creation.entity) : nullptr;
			const auto *member = at != nullptr ? game.world.Get<gp::TeamMember>(creation.entity) : nullptr;
			if (member == nullptr)
				continue;
			const gp::Transform where = *at;
			const std::uint32_t team = member->team;
			RetireNow(game, {creation.entity});
			const ecs::Entity made = SpawnObject(game, std::string(game.templates.DeathEffectName(gp::DeathEffectKind::Spawn, creation.replacement)),
				where.position.XY(), where.facing, team, "");
			if (!game.world.IsAlive(made))
				continue;
			game.world.Get<gp::Transform>(made)->position = where.position;
			OnBuildComplete(game, made);
			CreateModulesBuildComplete(game, made);
			ScoreStructureComplete(game, made, false);
			NoticeSuperweaponDetected(game, made);
			if (const auto *owner = game.world.Get<gp::Owner>(made))
				if (AiPlayer *ai = game.world.Resource<AiPlayers>().Of(owner->player))
					OnAiStructureProduced(game, *ai, made);
			continue;
		}
		// GrantScienceUpgrade: its controlling player gets the science.
		if (creation.science != 0xFFFFFFFFu)
		{
			if (const auto *owner = game.world.IsAlive(creation.entity) ? game.world.Get<gp::Owner>(creation.entity) : nullptr)
				GrantScience(game, owner->player, creation.science);
			continue;
		}
		const auto *transform = game.world.IsAlive(creation.entity) ? game.world.Get<gp::Transform>(creation.entity) : nullptr;
		if (transform == nullptr)
			continue;
		const auto *member = game.world.Get<gp::TeamMember>(creation.entity);
		RunCreationList(game, game.templates.DeathEffectName(gp::DeathEffectKind::Objects, creation.creation),
			{transform->position, transform->facing, member != nullptr ? member->team : 0xFFFFFFFFu, creation.entity});
	}
}
}
