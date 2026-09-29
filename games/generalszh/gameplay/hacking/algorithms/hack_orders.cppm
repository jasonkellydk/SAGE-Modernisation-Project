export module games.generalszh.gameplay.hacking.algorithms.hack_orders;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.hacking.components.internet_hack;
export import games.generalszh.gameplay.hacking.resources.hack_cues;
import games.generalszh.content.objects.model_conditions;
import engine.gameplay.common.appearance.components.appearance;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.status.components.ai_activity;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.navigation.components.navigation;
import Engine.Core.Math.FixedRandom;

// HackInternetAIUpdate's ends outside its system: the order to hack (aiHackInternet: from a Hack Internet button, a script,
// or an Internet Center taking a hacker in), and any other order while it hacks or packs (aiDoCommand: it packs up
// first, the order waiting till then).
export namespace generalszh::gameplay
{
namespace internet_hack_detail
{
namespace gp = engine::gameplay;

inline void Look(GameWorld &game, ecs::Entity unit, std::string_view condition, bool on)
{
	if (auto *look = game.world.Get<gp::Appearance>(unit))
		look->Set(content::ModelConditionBit(condition), on);
}

inline const InternetHackConfig *ConfigOf(const GameWorld &game, ecs::Entity unit)
{
	const auto *ref = game.world.IsAlive(unit) ? game.world.Get<gp::DefinitionRef>(unit) : nullptr;
	return ref != nullptr ? game.templates.InternetHackOf(ref->index) : nullptr;
}

// ai->getPackUnpackVariationFactor: the time times a random factor in 1 +- the variation, its fraction dropped.
inline std::uint32_t Varied(GameWorld &game, const InternetHackConfig &config, std::uint32_t ticks)
{
	using Engine::Math::Fixed;
	const Fixed factor = Engine::Math::UniformFixed(game.random, Fixed::One() - config.variation, Fixed::One() + config.variation);
	const std::int64_t frames = (Fixed::FromInt(static_cast<std::int32_t>(ticks)) * factor).Floor();
	return static_cast<std::uint32_t>(std::max<std::int64_t>(frames, 0));
}

inline void Cue(GameWorld &game, ecs::Entity unit, HackCue::Kind kind, std::uint32_t amount = 0)
{
	if (auto *cues = game.world.FindResource<HackCues>())
		if (const auto *at = game.world.Get<gp::Transform>(unit))
			cues->list.push_back({kind, unit, game.world.Get<gp::DefinitionRef>(unit)->index, amount, at->position});
}

// UnpackingState::onEnter: UNPACKING (PACKING and FIRING_A off), UnitUnpack, its frames.
inline void Unpack(GameWorld &game, ecs::Entity unit, InternetHack &hack, const InternetHackConfig &config)
{
	Look(game, unit, "PACKING", false);
	Look(game, unit, "FIRING_A", false);
	Look(game, unit, "UNPACKING", true);
	hack.stage = HackStage::Unpacking;
	hack.pending = HackPending::None;
	hack.framesRemaining = Varied(game, config, config.unpackTicks);
	if (auto *activity = game.world.Get<gp::AiActivity>(unit))
		activity->busy = 1;
	Cue(game, unit, HackCue::Kind::Unpack);
}
}

// AIUpdateInterface::aiHackInternet (HackInternetAIUpdate::aiDoCommand): hacking or unpacking already, nothing (the
// original's retail build packed up and started over: a quirk fixed); packing, it hacks again once packed; else it stops
// where it is and unpacks. `commanded`: a player's or a script's (else its AI's).
inline void HackInternet(GameWorld &game, ecs::Entity unit, bool commanded)
{
	namespace gp = engine::gameplay;
	using namespace internet_hack_detail;
	const InternetHackConfig *config = ConfigOf(game, unit);
	auto *hack = config != nullptr ? game.world.Get<InternetHack>(unit) : nullptr;
	if (hack == nullptr)
		return;
	if (hack->stage == HackStage::Hacking || hack->stage == HackStage::Unpacking)
		return;
	if (hack->stage == HackStage::Packing)
	{
		hack->pending = HackPending::Hack;
		return;
	}
	if (auto *activity = game.world.Get<gp::AiActivity>(unit))
		activity->commanded = commanded ? 1 : 0;
	if (auto *order = game.world.Get<gp::MoveOrder>(unit))
		*order = gp::MoveOrder{};
	if (auto *route = game.world.Get<gp::Route>(unit))
		route->planned = false;
	Unpack(game, unit, *hack, *config);
}

// HackInternetAIUpdate::aiDoCommand for any other order: hacking, it packs up (PackingState: PACKING, UnitPack, no
// time inside a building) and the order waits till then; packing, the order waits; unpacking, it stops unpacking.
inline void InterruptHack(GameWorld &game, ecs::Entity unit)
{
	namespace gp = engine::gameplay;
	using namespace internet_hack_detail;
	auto *hack = game.world.IsAlive(unit) ? game.world.Get<InternetHack>(unit) : nullptr;
	const InternetHackConfig *config = hack != nullptr ? ConfigOf(game, unit) : nullptr;
	if (config == nullptr || hack->stage == HackStage::Idle)
		return;
	if (hack->stage == HackStage::Unpacking)
	{
		Look(game, unit, "UNPACKING", false);
		hack->stage = HackStage::Idle;
		if (auto *activity = game.world.Get<gp::AiActivity>(unit))
			activity->busy = 0;
		return;
	}
	if (hack->stage == HackStage::Hacking)
	{
		Look(game, unit, "FIRING_A", false);
		Look(game, unit, "PACKING", true);
		hack->stage = HackStage::Packing;
		hack->framesRemaining = Varied(game, *config, game.world.Get<gp::Passenger>(unit) != nullptr ? 0u : config->packTicks);
		Cue(game, unit, HackCue::Kind::Pack);
	}
	hack->pending = HackPending::Order;
}
}
