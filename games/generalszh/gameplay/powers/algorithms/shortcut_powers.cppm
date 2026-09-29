export module games.generalszh.gameplay.powers.algorithms.shortcut_powers;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.powers.algorithms.special_power_timing;
import games.generalszh.content.powers.special_powers;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.common.health.components.inactive_body;
import engine.gameplay.common.identity.components.definition_ref;
import games.generalszh.gameplay.scripts.algorithms.object_counting;

// The player's objects the general's powers shortcut bar fires from (Player::findMostReadyShortcutSpecialPowerOfType,
// hasAnyShortcutSpecialPower, countReadyShortcutSpecialPowersOfType): read only, for the control bar. The player's
// objects are walked as Player::iterateObjects does (team by team, each team's newest member first); none under
// construction, sold or effectively dead counts, nor a module only scripts fire.
export namespace generalszh::gameplay
{
namespace shortcut_detail
{
namespace gp = engine::gameplay;

template<typename Visit>
void ForPlayerObjects(const GameWorld &game, std::uint32_t player, Visit &&visit)
{
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		const auto &record = game.roster.TeamAt(team);
		if (record.owner != player)
			continue;
		for (auto it = record.members.rbegin(); it != record.members.rend(); ++it)
			if (game.world.IsAlive(*it) && !visit(*it))
				return;
	}
}

inline bool Counts(const GameWorld &game, ecs::Entity entity)
{
	const ecs::World &world = game.world;
	return world.Get<gp::UnderConstruction>(entity) == nullptr && world.Get<gp::Sale>(entity) == nullptr && world.Get<gp::Dying>(entity) == nullptr &&
		world.Get<gp::InactiveBody>(entity) == nullptr;
}

inline bool Disabled(const GameWorld &game, ecs::Entity entity)
{
	const auto *off = game.world.Get<gp::Disabled>(entity);
	return off != nullptr && off->mask != 0;
}

// Object::findSpecialPowerModuleInterface(type): its first module whose power is of the type.
inline const gp::SpecialPowerTimer *FirstOfType(const GameWorld &game, const gp::SpecialPowerTimers &timers, std::string_view type)
{
	const auto &templates = game.templates.Content().powers.templates;
	for (std::uint32_t index = 0; index < timers.count; ++index)
		if (timers.timers[index].power < templates.size() && templates[timers.timers[index].power].type == type)
			return &timers.timers[index];
	return nullptr;
}

// SpecialPowerModule::getReadyFrame, as the object's disabled state stands, without starting a shared timer.
inline std::uint64_t ReadyFrame(const GameWorld &game, const gp::SpecialPowerTimer &timer, std::uint32_t player, bool disabled)
{
	return gp::PeekReadyFrame(timer, disabled, game.world.Resource<gp::SpecialPowerRules>(), game.world.Resource<gp::SharedPowerTimers>(), player,
		game.tick);
}
}

// Player::findMostReadyShortcutSpecialPowerOfType: the first object whose first module of the power type is ready
// (its ready frame before now), else the one that will be soonest (the first found on a tie); a disabled one counts
// only as a last resort (ready at UINT_MAX - 10). None: no object has such a module.
inline std::optional<ecs::Entity> MostReadyShortcutPower(const GameWorld &game, std::uint32_t player, std::string_view type)
{
	using namespace shortcut_detail;
	std::optional<ecs::Entity> found;
	std::uint64_t lowest = 0xFFFFFFFFu;
	ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		if (!Counts(game, entity))
			return true;
		const auto *timers = game.world.Get<gp::SpecialPowerTimers>(entity);
		const gp::SpecialPowerTimer *timer = timers != nullptr ? FirstOfType(game, *timers, type) : nullptr;
		if (timer == nullptr || (timer->flags & gp::power_flag::ScriptOnly) != 0)
			return true;
		const bool disabled = Disabled(game, entity);
		const std::uint64_t ready = disabled ? 0xFFFFFFFFu - 10 : ReadyFrame(game, *timer, player, false);
		if (ready < game.tick)
		{
			found = entity;
			return false;
		}
		if (ready < lowest)
		{
			found = entity;
			lowest = ready;
		}
		return true;
	});
	return found;
}

// Player::hasAnyShortcutSpecialPower: an object whose first shortcut power module (ShortcutPower) is not script-only.
inline bool HasAnyShortcutPower(const GameWorld &game, std::uint32_t player)
{
	using namespace shortcut_detail;
	const auto &templates = game.templates.Content().powers.templates;
	bool any = false;
	ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		const auto *timers = game.world.Get<gp::SpecialPowerTimers>(entity);
		if (!Counts(game, entity) || timers == nullptr)
			return true;
		for (std::uint32_t index = 0; index < timers->count; ++index)
		{
			const gp::SpecialPowerTimer &timer = timers->timers[index];
			if (timer.power >= templates.size() || !templates[timer.power].shortcut)
				continue;
			any = (timer.flags & gp::power_flag::ScriptOnly) == 0;
			break;
		}
		return !any;
	});
	return any;
}

// ThingTemplate::isEquivalentTo between the object's type and `type`.
inline bool OfType(const GameWorld &game, ecs::Entity entity, const std::string &type)
{
	const auto *ref = game.world.Get<engine::gameplay::DefinitionRef>(entity);
	return ref != nullptr && EquivalentTypes(game, game.templates.DefinitionAt(ref->index).name, type);
}

// Player::findAnyExistingObjectWithThingTemplate: the first of the player's objects of the type not under construction,
// sold or effectively dead.
inline std::optional<ecs::Entity> AnyExistingObjectOfType(const GameWorld &game, std::uint32_t player, const std::string &type)
{
	using namespace shortcut_detail;
	std::optional<ecs::Entity> found;
	ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		if (OfType(game, entity, type) && Counts(game, entity))
			found = entity;
		return !found;
	});
	return found;
}

// GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE's walk (selectObjectOfType): every one of the player's objects of the type.
inline std::vector<ecs::Entity> PlayerObjectsOfType(const GameWorld &game, std::uint32_t player, const std::string &type)
{
	std::vector<ecs::Entity> out;
	shortcut_detail::ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		if (OfType(game, entity, type))
			out.push_back(entity);
		return true;
	});
	return out;
}

// Player::findMostReadyShortcutSpecialPowerForThing: of the player's objects of the type (not under construction, sold
// or effectively dead), the one with the module most ready (getPercentReady() * 100, whole; the first found on a tie,
// none at 0), the walk ending at 100.
inline std::optional<ecs::Entity> MostReadySpecialPowerForThing(const GameWorld &game, std::uint32_t player, const std::string &type)
{
	using namespace shortcut_detail;
	const auto &rules = game.world.Resource<gp::SpecialPowerRules>();
	const auto &shared = game.world.Resource<gp::SharedPowerTimers>();
	std::optional<ecs::Entity> found;
	std::int64_t highest = 0;
	ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		if (highest >= 100)
			return false;
		const auto *timers = game.world.Get<gp::SpecialPowerTimers>(entity);
		if (timers == nullptr || !OfType(game, entity, type) || !Counts(game, entity))
			return true;
		for (std::uint32_t index = 0; index < timers->count; ++index)
		{
			const std::int64_t percent = (gp::PeekPercentReady(timers->timers[index], rules, shared, player, game.tick) * Engine::Math::Fixed::FromInt(100)).Floor();
			if (percent > highest)
			{
				found = entity;
				highest = percent;
			}
		}
		return true;
	});
	return found;
}

// Player::countReadyShortcutSpecialPowersOfType: the objects whose first module of the type is ready (its ready frame
// before now; a disabled one never); once one is counted, a shared power (SharedSyncedTimer) counts no more.
inline std::int32_t CountReadyShortcutPowers(const GameWorld &game, std::uint32_t player, std::string_view type)
{
	using namespace shortcut_detail;
	const auto &templates = game.templates.Content().powers.templates;
	std::int32_t ready = 0;
	ForPlayerObjects(game, player, [&](ecs::Entity entity) {
		if (!Counts(game, entity))
			return true;
		const auto *timers = game.world.Get<gp::SpecialPowerTimers>(entity);
		const gp::SpecialPowerTimer *timer = timers != nullptr ? FirstOfType(game, *timers, type) : nullptr;
		if (timer == nullptr || (timer->flags & gp::power_flag::ScriptOnly) != 0)
			return true;
		if (templates[timer->power].sharedSynced && ready == 1)
			return true;
		const std::uint64_t frame = Disabled(game, entity) ? 0xFFFFFFFFu - 10 : ReadyFrame(game, *timer, player, false);
		if (frame < game.tick)
			++ready;
		return true;
	});
	return ready;
}
}
