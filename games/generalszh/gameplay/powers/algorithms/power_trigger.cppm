export module games.generalszh.gameplay.powers.algorithms.power_trigger;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.powers.algorithms.special_power_state;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import engine.gameplay.rts.vision.components.vision;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.lifetime.components.lifetime;
import engine.gameplay.common.identity.components.team_member;
import games.generalszh.gameplay.scripts.resources.script_records;
import games.generalszh.gameplay.eva.resources.eva_notices;
import games.generalszh.gameplay.abilities.resources.ability_notices;
import games.generalszh.gameplay.powers.algorithms.launcher_doors;

// A special power module's own firing (SpecialPowerModule::triggerSpecialPower, markSpecialPowerTriggered) and which
// of an object's modules a firing may use.
export namespace generalszh::gameplay
{
namespace gameplay = engine::gameplay;
using Engine::Math::Fixed;

// The player's default team ("team<Player>"), else the source's own.
inline std::uint32_t DefaultTeamOf(const GameWorld &game, ecs::Entity source, std::uint32_t player)
{
	return game.roster.DefaultTeam(player).value_or(game.world.Get<gameplay::TeamMember>(source)->team);
}

// SpecialPowerModule::createViewObject: GameData's SpecialPowerViewObject on the player's default team where the power
// lands, seeing ViewObjectRange, gone after ViewObjectDuration (DeletionUpdate::setLifetimeRange); none unless both.
inline ecs::Entity CreateViewObject(GameWorld &game, ecs::Entity source, std::uint32_t power, Engine::Math::FixedVector3 at)
{
	const content::GameContent &content = game.templates.Content();
	const content::SpecialPowerTemplate &kind = content.powers.templates[power];
	if (kind.viewObjectRange == Fixed{} || kind.viewObjectTicks == 0 || content.gameData.specialPowerViewObject.empty())
		return {};
	const std::uint32_t player = OwnerPlayer(game, source);
	const ecs::Entity look = SpawnObject(game, content.gameData.specialPowerViewObject, at.XY(), {}, DefaultTeamOf(game, source, player), {});
	if (!game.world.IsAlive(look))
		return {};
	game.world.Get<gameplay::Transform>(look)->position = at;
	// setShroudClearingRange (relooks it).
	if (auto *vision = game.world.Get<gameplay::Vision>(look))
		vision->clearingRange = kind.viewObjectRange;
	if (!game.world.Has<gameplay::Lifetime>(look))
		game.world.Add<gameplay::Lifetime>(look);
	*game.world.Get<gameplay::Lifetime>(look) = {game.tick + std::max<std::uint64_t>(kind.viewObjectTicks, 1), 1u, 0u};
	return look;
}

// SpecialPowerModule::triggerSpecialPower: aboutToDoSpecialPower (the scripts hear it), createViewObject where it lands
// (none: markSpecialPowerTriggered(nullptr)), and the recharge starts.
inline void TriggerSpecialPower(GameWorld &game, ecs::Entity source, std::uint32_t power, std::optional<Engine::Math::FixedVector3> at)
{
	const std::uint32_t player = OwnerPlayer(game, source);
	if (auto *records = game.world.FindResource<ScriptRecords>())
		records->TriggeredPower(player, game.templates.Content().powers.templates[power].name, source);
	// EVA announces a superweapon's launch to everyone (its own, an ally's, an enemy's).
	if (const EvaWeapon weapon = EvaWeaponOf(game.templates.Content().powers.templates[power].type); weapon != EvaWeapon::None)
		if (auto *eva = game.world.FindResource<EvaNotices>())
			eva->list.push_back({EvaCue::SuperweaponLaunched, weapon, player});
	// Its InitiateSound on the source, its InitiateAtLocationSound where it lands.
	if (auto *notices = game.world.FindResource<AbilityNotices>())
		notices->powers.push_back({source, power, player, at});
	if (at)
		CreateViewObject(game, source, power, *at);
	// MissileLauncherBuildingUpdate::initiateIntentToDoSpecialPower: its door's power fired, the door waits to close.
	if (auto *door = game.world.Get<LauncherDoor>(source))
		if (const auto *definition = game.world.Get<gameplay::DefinitionRef>(source))
			if (const LauncherDoorConfig *config = game.templates.LauncherDoorOf(definition->index); config != nullptr && config->power == power)
				SwitchLauncherDoor(*door, *config, LauncherDoorState::WaitingToClose, game.tick, 0, game.world.Get<gameplay::Transform>(source)->position,
					game.world.FindResource<LauncherDoorEffects>());
	gameplay::StartPowerRecharge(*game.world.Get<gameplay::SpecialPowerTimers>(source)->Find(power), ClockFor(game, player));
}

// The source's module for the power, if a player's order (not a script's) may fire it: the science it needs known
// (groupDoSpecialPowerAt...) and ready (getPercentReady() < 1: canDoSpecialPowerAt...).
inline gameplay::SpecialPowerTimer *PowerModuleFor(GameWorld &game, ecs::Entity source, const std::string &power, bool fromScript)
{
	auto &world = game.world;
	if (!world.IsAlive(source) || world.Get<gameplay::DefinitionRef>(source) == nullptr)
		return nullptr;
	const auto index = game.templates.Content().powers.Template(power);
	auto *timers = world.Get<gameplay::SpecialPowerTimers>(source);
	gameplay::SpecialPowerTimer *timer = index && timers != nullptr ? timers->Find(*index) : nullptr;
	if (timer == nullptr || fromScript)
		return timer;
	if (!CanUseSpecialPower(game, source, *index) || !gameplay::IsReady(*timer, ClockFor(game, OwnerPlayer(game, source))))
		return nullptr;
	return timer;
}
}
