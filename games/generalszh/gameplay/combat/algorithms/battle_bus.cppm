export module games.generalszh.gameplay.combat.algorithms.battle_bus;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.combat.systems.battle_bus_system;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.creation.algorithms.creation_list_runner;
import games.generalszh.content.combat.loadout_content;
import games.generalszh.content.combat.combat_catalog;
import games.generalszh.content.objects.model_conditions;
import engine.gameplay.common.health.systems.health_system;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.appearance.components.appearance;
import engine.gameplay.common.status.components.status_flags;
import engine.gameplay.common.status.algorithms.disable_now;
import engine.gameplay.common.physics.algorithms.forces;
import engine.gameplay.rts.death.algorithms.death_choice;
import engine.gameplay.rts.loadout.components.loadout;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.lifecycle.resources.kill_requests;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.common.identity.components.definition_ref;

// The battle bus's undeath, once the systems have run:
// - ApplySecondLives (UndeadBody::startSecondLife, after its new maximum): ARMORSET_SECOND_LIFE, then one of its slow
//   deaths that applies to the hit is picked by probability (GameLogicRandomValue) and begun; its
//   BattleBusSlowDeathBehavior, not yet really dead, begins its first death: FXStartUndeath and OCLStartUndeath, its AI
//   idle, its physics' acceleration cleared and sideways speed scrubbed, thrown up by ThrowForce (less its shock
//   resistance) and spun at random, and its riders hurt by PercentDamageToPassengers of their maximum (unresistable).
// - ApplyBattleBusEvents: landed: FXHitGround, OCLHitGround, SECOND_LIFE shown, its AI idle, its physics stilled, HELD.
export namespace generalszh::gameplay
{
struct BattleBusCue
{
	std::string effect; // an FX list, played on the bus (doFXObj)
	ecs::Entity bus;
	Engine::Math::FixedVector3 at;
};

struct BattleBusCues
{
	std::vector<BattleBusCue> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::BattleBusCues>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.battle_bus_cues";
};
}

export namespace generalszh::gameplay
{

namespace battle_bus_detail
{
namespace gp = engine::gameplay;

inline void Effects(GameWorld &game, ecs::Entity bus, const std::string &fx, const std::string &ocl)
{
	const auto &at = *game.world.Get<gp::Transform>(bus);
	if (!fx.empty())
		if (auto *cues = game.world.FindResource<BattleBusCues>())
			cues->list.push_back({fx, bus, at.position});
	if (!ocl.empty())
	{
		const auto *member = game.world.Get<gp::TeamMember>(bus);
		const auto *experience = game.world.Get<gp::Experience>(bus);
		RunCreationList(game, ocl, {at.position, at.facing, member != nullptr ? member->team : 0xFFFFFFFFu, bus, experience != nullptr ? experience->level : 0u});
	}
}

// clearAcceleration, scrubVelocity2D(0).
inline void Still(gp::PhysicsBody &body)
{
	body.acceleration = {};
	body.velocity.x = {};
	body.velocity.y = {};
}
}

inline void BeginFirstDeath(GameWorld &game, ecs::Entity bus, const BattleBusConfig &config)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	using namespace battle_bus_detail;
	auto &world = game.world;
	if (!world.Has<BattleBus>(bus))
		world.Add<BattleBus>(bus);
	auto &state = *world.Get<BattleBus>(bus);
	state.phase = BusPhase::Thrown;
	state.groundCheckTick = game.tick + BusGroundCheckDelay;
	Effects(game, bus, config.fxStart, config.oclStart);
	AiIdle(game, bus);
	if (auto *body = world.Get<gp::PhysicsBody>(bus))
	{
		Still(*body);
		const Fixed resisted = Fixed::One() - std::clamp(body->shockResistance, Fixed{}, Fixed::One());
		gp::ApplyForce(*body, {Fixed{}, Fixed{}, config.throwForce * resisted});
		// applyRandomRotation.
		if (!body->Has(gp::physics_flag::StickToGround))
		{
			body->Set(gp::physics_flag::AllowBouncing, true);
			const auto spin = [&](std::int32_t most) {
				const Fixed share = Engine::Math::UniformFixed(game.random, Fixed{} - Fixed::One(), Fixed::One());
				return static_cast<std::int32_t>((static_cast<std::int64_t>(most) * share.Raw()) >> Fixed::FractionBits);
			};
			body->yawRate += spin(body->shockMaxYaw);
			body->pitchRate += spin(body->shockMaxPitch);
			body->rollRate += spin(body->shockMaxRoll);
		}
		if (body->Has(gp::physics_flag::Locomotive))
			body->Set(gp::physics_flag::Pushed, true);
	}
	// processDamageToContained(PercentDamageToPassengers).
	if (config.passengerDamage > Fixed{})
	{
		const std::vector<ecs::Entity> riders(game.manifest.Aboard(bus).begin(), game.manifest.Aboard(bus).end());
		for (const ecs::Entity rider : riders)
			if (const auto *health = world.IsAlive(rider) ? world.Get<gp::Health>(rider) : nullptr)
				DamageNow(game, rider, health->maximum * config.passengerDamage);
	}
}

inline void ApplySecondLives(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	auto &world = game.world;
	auto *resource = world.FindResource<gp::SecondLives>();
	if (resource == nullptr)
		return;
	std::vector<gp::SecondLifeStart> starts;
	resource->AppendTo(starts);
	resource->Reset(0);
	for (const gp::SecondLifeStart &start : starts)
	{
		if (!world.IsAlive(start.entity))
			continue;
		if (auto *loadout = world.Get<gp::Loadout>(start.entity))
			loadout->armorFlags |= content::SetFlag(content::ArmorSetFlagNames, "SECOND_LIFE");
		const auto *ref = world.Get<gp::DefinitionRef>(start.entity);
		const auto *mortality = world.Get<gp::Mortality>(start.entity);
		if (ref == nullptr || mortality == nullptr)
			continue;
		const gp::DeathDefinition &death = world.Resource<gp::DeathCatalog>().At(mortality->death);
		const auto *experience = world.Get<gp::Experience>(start.entity);
		const auto *flags = world.Get<gp::StatusFlags>(start.entity);
		const auto chosen = gp::ChooseSlowDeath(death, start.deathType, experience != nullptr ? experience->level : 0u, Fixed{}, game.random,
			flags != nullptr ? flags->bits : 0u);
		const BattleBusConfig *config = game.templates.BattleBusOf(ref->index);
		if (chosen && config != nullptr && *chosen == config->slowIndex)
			BeginFirstDeath(game, start.entity, *config);
	}
}

inline void ApplyBattleBusEvents(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using namespace battle_bus_detail;
	auto &world = game.world;
	auto *resource = world.FindResource<BattleBusEvents>();
	if (resource == nullptr)
		return;
	const std::vector<BattleBusEvent> events = std::move(resource->list);
	resource->list.clear();
	for (const BattleBusEvent &event : events)
	{
		const auto *ref = world.IsAlive(event.bus) ? world.Get<gp::DefinitionRef>(event.bus) : nullptr;
		const BattleBusConfig *config = ref != nullptr ? game.templates.BattleBusOf(ref->index) : nullptr;
		if (config == nullptr)
			continue;
		Effects(game, event.bus, config->fxHitGround, config->oclHitGround);
		if (auto *look = world.Get<gp::Appearance>(event.bus))
			look->Set(content::ModelConditionBit("SECOND_LIFE"), true);
		AiIdle(game, event.bus);
		if (auto *body = world.Get<gp::PhysicsBody>(event.bus))
			Still(*body);
		gp::DisableNow(world, event.bus, gp::disabled_type::Held, gp::DisabledForever);
	}
}
}

