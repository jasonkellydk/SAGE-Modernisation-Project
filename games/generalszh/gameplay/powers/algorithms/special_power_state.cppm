export module games.generalszh.gameplay.powers.algorithms.special_power_state;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import engine.gameplay.rts.powers.algorithms.special_power_timing;
import games.generalszh.content.powers.special_powers;
import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.sciences.resources.player_sciences;
import engine.gameplay.rts.economy.resources.player_bounties;
import games.generalszh.gameplay.eva.resources.eva_notices;

// Objects' special power modules as the rules see them (SpecialPowerModule and its callers): their countdowns start as
// they are made (SpecialPowerModule's constructor), as they are built (SpecialPowerCreate::onBuildComplete) and as
// their science is learnt (Player::addScience); whether a player may use one (SpecialPowerStore::canUseSpecialPower)
// and whether it is ready.
export namespace generalszh::gameplay
{
inline engine::gameplay::PowerClock ClockFor(GameWorld &game, std::uint32_t player)
{
	return {game.world.Resource<engine::gameplay::SpecialPowerRules>(), game.world.Resource<engine::gameplay::SharedPowerTimers>(), player, game.tick};
}

inline std::uint32_t OwnerPlayer(const GameWorld &game, ecs::Entity entity)
{
	const auto *owner = game.world.Get<engine::gameplay::Owner>(entity);
	return owner != nullptr ? owner->player : 0u;
}

// Object::getRelationship: `from`'s team's view of `to`'s team (Team::getRelationship: the teams' overrides, then its
// player's). No relationships: neutral.
inline engine::gameplay::Relationship RelationOf(const GameWorld &game, ecs::Entity from, ecs::Entity to)
{
	namespace gp = engine::gameplay;
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	if (relationships == nullptr)
		return gp::Relationship::Neutral;
	const auto teamOf = [&](ecs::Entity entity) {
		const auto *member = game.world.IsAlive(entity) ? game.world.Get<gp::TeamMember>(entity) : nullptr;
		return member != nullptr ? member->team : gp::Relationships::NoTeam;
	};
	return relationships->Between(teamOf(from), OwnerPlayer(game, from), teamOf(to), OwnerPlayer(game, to));
}

// The scripts' countdown edits of a unit's special power module (NAMED_SET_ / NAMED_ADD_ / NAMED_STOP_ /
// NAMED_START_SPECIAL_POWER_COUNTDOWN): setReadyFrame(now + seconds), setReadyFrame(getReadyFrame() + seconds),
// pauseCountdown(true / false). No such unit, power or module: nothing.
enum class CountdownEdit : std::uint8_t
{
	Set,
	Add,
	Stop,
	Start,
};
inline void EditPowerCountdown(GameWorld &game, ecs::Entity unit, std::string_view powerName, CountdownEdit edit, std::int64_t seconds)
{
	namespace gp = engine::gameplay;
	const auto power = game.templates.Content().powers.Template(powerName);
	auto *timers = power && game.world.IsAlive(unit) ? game.world.Get<gp::SpecialPowerTimers>(unit) : nullptr;
	gp::SpecialPowerTimer *timer = timers != nullptr ? timers->Find(*power) : nullptr;
	if (timer == nullptr)
		return;
	const gp::PowerClock clock = ClockFor(game, OwnerPlayer(game, unit));
	const std::int64_t frames = static_cast<std::int64_t>(game.step.TicksPerSecond()) * seconds;
	const auto at = [](std::int64_t tick) { return static_cast<std::uint64_t>(std::max<std::int64_t>(0, tick)); };
	switch (edit)
	{
	case CountdownEdit::Set: gp::SetReadyFrame(*timer, at(static_cast<std::int64_t>(game.tick) + frames), clock); break;
	case CountdownEdit::Add:
	{
		const auto *off = game.world.Get<gp::Disabled>(unit);
		const std::uint64_t ready = gp::ReadyFrame(*timer, off != nullptr && off->mask != 0, clock);
		gp::SetReadyFrame(*timer, at(static_cast<std::int64_t>(ready) + frames), clock);
		break;
	}
	case CountdownEdit::Stop: gp::PauseCountdown(*timer, true, clock); break;
	case CountdownEdit::Start: gp::PauseCountdown(*timer, false, clock); break;
	}
}

// Object::isDisabled.
inline bool IsDisabled(const GameWorld &game, ecs::Entity entity)
{
	const auto *off = game.world.Get<engine::gameplay::Disabled>(entity);
	return off != nullptr && off->mask != 0;
}

// InGameUI::addSuperweapon for a structure's power with a public timer (hidden for good if its player lacks the power's
// science now).
inline void AddPublicTimer(GameWorld &game, ecs::Entity entity, engine::gameplay::SpecialPowerTimer &timer)
{
	namespace gp = engine::gameplay;
	const gp::SpecialPowerRule *rule = game.world.Resource<gp::SpecialPowerRules>().Of(timer.power);
	const auto *ref = game.world.Get<gp::DefinitionRef>(entity);
	if (rule == nullptr || !rule->publicTimer || ref == nullptr || !game.templates.DefinitionAt(ref->index).Is("STRUCTURE"))
		return;
	const bool knows = rule->requiredScience == gp::SpecialPowerRule::NoScience ||
		game.world.Resource<gp::PlayerSciences>().Has(OwnerPlayer(game, entity), rule->requiredScience);
	gp::AddPublicTimer(timer, game.world.Resource<gp::SharedPowerTimers>(), knows);
}

// CashBountyPower::onObjectCreated / onSpecialPowerCreation: its player's cash bounty rises to its Bounty. Made, only when
// the player knows its power's science (hasScience: a power needing none never); created (built with a
// SpecialPowerCreate, its science learnt), always. `science`: only the modules whose power needs that science.
inline void RaiseCashBounties(GameWorld &game, ecs::Entity entity, const content::ObjectDefinition &object, bool needScience,
	std::optional<std::uint32_t> science = std::nullopt)
{
	namespace gp = engine::gameplay;
	auto *bounties = game.world.FindResource<gp::PlayerBounties>();
	const auto *rules = game.world.FindResource<gp::SpecialPowerRules>();
	if (bounties == nullptr || rules == nullptr)
		return;
	const std::uint32_t player = OwnerPlayer(game, entity);
	for (const content::CashBountyModule &module : content::CashBountyModulesOf(object))
	{
		const auto power = game.templates.Content().powers.Template(module.power);
		const gp::SpecialPowerRule *rule = power ? rules->Of(*power) : nullptr;
		if (rule == nullptr || (science && rule->requiredScience != *science))
			continue;
		if (needScience && (rule->requiredScience == gp::SpecialPowerRule::NoScience ||
							   !game.world.Resource<gp::PlayerSciences>().Has(player, rule->requiredScience)))
			continue;
		bounties->Raise(player, module.share);
	}
}

// The modules' countdowns as the object is made.
inline void AttachSpecialPowers(GameWorld &game, ecs::Entity entity, const content::ObjectDefinition &object)
{
	namespace gp = engine::gameplay;
	if (game.world.FindResource<gp::SpecialPowerRules>() == nullptr)
		return;
	const content::SpecialPowerContent &powers = game.templates.Content().powers;
	gp::SpecialPowerTimers timers;
	for (const content::PowerModule &module : content::PowerModulesOf(object))
	{
		const auto power = powers.Template(module.power);
		if (!power || timers.count >= gp::SpecialPowerTimers::Capacity)
			continue;
		gp::SpecialPowerTimer &timer = timers.timers[timers.count++];
		timer.power = *power;
		timer.flags = (module.startsPaused ? gp::power_flag::StartsPaused : 0u) | (module.scriptOnly ? gp::power_flag::ScriptOnly : 0u) |
			(module.updateModuleStartsAttack ? gp::power_flag::UpdateModuleStartsAttack : 0u);
	}
	if (timers.count == 0)
		return;
	const bool building = game.world.Get<gp::UnderConstruction>(entity) != nullptr;
	const gp::PowerClock clock = ClockFor(game, OwnerPlayer(game, entity));
	for (std::uint32_t index = 0; index < timers.count; ++index)
		gp::StartSpecialPower(timers.timers[index], building, clock);
	game.world.Add<gp::SpecialPowerTimers>(entity);
	*game.world.Get<gp::SpecialPowerTimers>(entity) = timers;
	// The module's constructor: an unpaused shared power with a public timer on a structure shows its countdown.
	const gp::SpecialPowerRules &rules = game.world.Resource<gp::SpecialPowerRules>();
	auto &attached = *game.world.Get<gp::SpecialPowerTimers>(entity);
	for (std::uint32_t index = 0; index < attached.count; ++index)
		if (const gp::SpecialPowerRule *rule = rules.Of(attached.timers[index].power);
			rule != nullptr && rule->sharedSynced && attached.timers[index].pausedCount == 0)
			AddPublicTimer(game, entity, attached.timers[index]);
	RaiseCashBounties(game, entity, object, true);
}

// CreateModule::onBuildComplete for its SpecialPowerCreate: each power module's onSpecialPowerCreation.
inline void OnBuildComplete(GameWorld &game, ecs::Entity entity)
{
	namespace gp = engine::gameplay;
	auto *timers = game.world.IsAlive(entity) ? game.world.Get<gp::SpecialPowerTimers>(entity) : nullptr;
	const auto *ref = timers != nullptr ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	if (ref == nullptr || !content::HasSpecialPowerCreate(game.templates.DefinitionAt(ref->index)))
		return;
	const gp::PowerClock clock = ClockFor(game, OwnerPlayer(game, entity));
	for (std::uint32_t index = 0; index < timers->count; ++index)
	{
		gp::OnSpecialPowerCreation(timers->timers[index], clock);
		AddPublicTimer(game, entity, timers->timers[index]);
	}
	RaiseCashBounties(game, entity, game.templates.DefinitionAt(ref->index), false);
}

// Player::addScience: each of the player's power modules waiting for that science starts, ready now.
inline void WakePowersForScience(GameWorld &game, std::uint32_t player, std::uint32_t science)
{
	namespace gp = engine::gameplay;
	const auto *rules = game.world.FindResource<gp::SpecialPowerRules>();
	if (rules == nullptr)
		return;
	const gp::PowerClock clock = ClockFor(game, player);
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		if (game.roster.TeamAt(team).owner != player)
			continue;
		for (const ecs::Entity member : game.roster.TeamAt(team).members)
		{
			auto *timers = game.world.IsAlive(member) ? game.world.Get<gp::SpecialPowerTimers>(member) : nullptr;
			if (timers == nullptr)
				continue;
			for (std::uint32_t index = 0; index < timers->count; ++index)
			{
				const gp::SpecialPowerRule *rule = rules->Of(timers->timers[index].power);
				if (rule == nullptr || rule->requiredScience != science)
					continue;
				gp::OnSpecialPowerCreation(timers->timers[index], clock);
				AddPublicTimer(game, member, timers->timers[index]);
				gp::SetReadyFrame(timers->timers[index], game.tick, clock);
			}
			if (const auto *ref = game.world.Get<gp::DefinitionRef>(member))
				RaiseCashBounties(game, member, game.templates.DefinitionAt(ref->index), false, science);
		}
	}
}

// Player::onStructureConstructionComplete: a finished structure with a particle cannon, a nuke or a Scud Storm is
// announced (each kind it has, in that order).
inline void NoticeSuperweaponDetected(GameWorld &game, ecs::Entity structure)
{
	namespace gp = engine::gameplay;
	auto *eva = game.world.FindResource<EvaNotices>();
	const auto *timers = game.world.IsAlive(structure) ? game.world.Get<gp::SpecialPowerTimers>(structure) : nullptr;
	if (eva == nullptr || timers == nullptr)
		return;
	const auto &templates = game.templates.Content().powers.templates;
	const auto has = [&](EvaWeapon weapon) {
		for (std::uint32_t index = 0; index < timers->count; ++index)
			if (timers->timers[index].power < templates.size() && EvaWeaponOf(templates[timers->timers[index].power].type) == weapon)
				return true;
		return false;
	};
	for (const EvaWeapon weapon : {EvaWeapon::ParticleCannon, EvaWeapon::Nuke, EvaWeapon::ScudStorm})
		if (has(weapon))
			eva->list.push_back({EvaCue::SuperweaponDetected, weapon, OwnerPlayer(game, structure)});
}

// What evaluateSkirmishSpecialPowerIsReady finds: whether the power exists, whether one of the player's objects has it
// ready, and else the soonest tick one will be (never later than `latest`).
struct PowerReadiness
{
	bool known{false};
	bool ready{false};
	std::uint64_t next{0};
};

inline bool CanUseSpecialPower(const GameWorld &game, ecs::Entity entity, std::uint32_t power);

// evaluateSkirmishSpecialPowerIsReady's walk: the player's objects (team by team) not under construction or disabled
// that have a module for the power and may use it.
inline PowerReadiness SpecialPowerReadiness(GameWorld &game, std::uint32_t player, std::string_view name, std::uint64_t latest)
{
	namespace gp = engine::gameplay;
	PowerReadiness out;
	const auto power = game.templates.Content().powers.Template(name);
	if (!power)
		return out;
	out.known = true;
	out.next = latest;
	const gp::PowerClock clock = ClockFor(game, player);
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		if (game.roster.TeamAt(team).owner != player)
			continue;
		for (auto it = game.roster.TeamAt(team).members.rbegin(); it != game.roster.TeamAt(team).members.rend(); ++it)
		{
			const ecs::Entity member = *it;
			if (!game.world.IsAlive(member) || game.world.Get<gp::UnderConstruction>(member) != nullptr || IsDisabled(game, member))
				continue;
			const auto *timers = game.world.Get<gp::SpecialPowerTimers>(member);
			const gp::SpecialPowerTimer *timer = timers != nullptr ? timers->Find(*power) : nullptr;
			if (timer == nullptr || !CanUseSpecialPower(game, member, *power))
				continue;
			if (gp::IsReady(*timer, clock))
			{
				out.ready = true;
				return out;
			}
			out.next = std::min(out.next, gp::ReadyFrame(*timer, false, clock));
		}
	}
	return out;
}

// SpecialPowerStore::canUseSpecialPower: it has a module for the power and its player knows the science it needs.
inline bool CanUseSpecialPower(const GameWorld &game, ecs::Entity entity, std::uint32_t power)
{
	namespace gp = engine::gameplay;
	const auto *timers = game.world.IsAlive(entity) ? game.world.Get<gp::SpecialPowerTimers>(entity) : nullptr;
	if (timers == nullptr || timers->Find(power) == nullptr)
		return false;
	const gp::SpecialPowerRule *rule = game.world.Resource<gp::SpecialPowerRules>().Of(power);
	if (rule == nullptr || rule->requiredScience == gp::SpecialPowerRule::NoScience)
		return true;
	return game.world.Resource<gp::PlayerSciences>().Has(OwnerPlayer(game, entity), rule->requiredScience);
}
}
