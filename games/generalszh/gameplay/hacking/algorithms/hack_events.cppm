export module games.generalszh.gameplay.hacking.algorithms.hack_events;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.hacking.algorithms.hack_orders;
export import games.generalszh.gameplay.hacking.systems.internet_hack_system;
import games.generalszh.gameplay.veterancy.algorithms.veterancy_placement;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.status.components.ai_activity;
import engine.gameplay.rts.economy.resources.player_money;
import games.generalszh.gameplay.powers.resources.cash_notices;
import engine.gameplay.common.spatial.components.transform;

// HackInternetAIUpdate's reach beyond its hacker, once the systems have run: its pay, and the Internet Center taking it in.
export namespace generalszh::gameplay
{
// The tick's HackEvents: a hacker's pay (Money::deposit and the score's money earned, XpPerCashUpdate to it, the cash
// shown over it and UnitCashPing); one packed with a hack pending hacks again.
inline void ApplyHackEvents(GameWorld &game, std::span<const HackEvent> events)
{
	namespace gp = engine::gameplay;
	using namespace internet_hack_detail;
	for (const HackEvent &event : events)
	{
		if (!game.world.IsAlive(event.hacker))
			continue;
		if (event.kind == HackEvent::Kind::Again)
		{
			if (auto *hack = game.world.Get<InternetHack>(event.hacker))
				if (const InternetHackConfig *config = ConfigOf(game, event.hacker))
					Unpack(game, event.hacker, *hack, *config);
			continue;
		}
		const InternetHackConfig *config = ConfigOf(game, event.hacker);
		const auto *owner = game.world.Get<gp::Owner>(event.hacker);
		if (config == nullptr || owner == nullptr)
			continue;
		game.world.Resource<gp::PlayerMoney>().Earn(owner->player, event.amount);
		AwardExperience(game, event.hacker, static_cast<std::int32_t>(config->xpPerCash));
		Cue(game, event.hacker, HackCue::Kind::Cash, event.amount);
		if (auto *notices = game.world.FindResource<CashNotices>())
		{
			Engine::Math::FixedVector3 over = game.world.Get<gp::Transform>(event.hacker)->position;
			over.z = over.z + Engine::Math::Fixed::FromInt(20);
			notices->list.push_back({CashNotice::Kind::Hacked, static_cast<std::int64_t>(event.amount), over});
		}
	}
}

// InternetHackContain::onContaining: a hacker taken into an Internet Center hacks (from its AI); a hacker let out of
// anything is done (its exit an order: packed, with no time inside a building).
inline void ApplyInternetHackCargo(GameWorld &game)
{
	namespace gp = engine::gameplay;
	std::vector<std::pair<ecs::Entity, ecs::Entity>> entered, left;
	for (const gp::CargoChange &change : game.manifest.Changes())
		(change.entered ? entered : left).emplace_back(change.container, change.rider);
	for (const auto &[container, rider] : left)
		if (auto *hack = game.world.IsAlive(rider) ? game.world.Get<InternetHack>(rider) : nullptr; hack != nullptr && hack->stage != HackStage::Idle)
		{
			internet_hack_detail::Look(game, rider, "UNPACKING", false);
			internet_hack_detail::Look(game, rider, "PACKING", false);
			internet_hack_detail::Look(game, rider, "FIRING_A", false);
			*hack = InternetHack{};
			if (auto *activity = game.world.Get<gp::AiActivity>(rider))
				activity->busy = 0;
		}
	for (const auto &[container, rider] : entered)
	{
		const auto *ref = game.world.IsAlive(container) ? game.world.Get<gp::DefinitionRef>(container) : nullptr;
		if (ref != nullptr && game.templates.InternetHackContainer(ref->index) && game.world.IsAlive(rider))
			HackInternet(game, rider, false);
	}
}
}
