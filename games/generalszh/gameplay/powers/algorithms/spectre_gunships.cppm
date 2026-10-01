export module games.generalszh.gameplay.powers.algorithms.spectre_gunships;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.powers.components.spectre_gunship;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.powers.algorithms.power_trigger;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.orders.algorithms.wander_orders;
import games.generalszh.gameplay.containment.algorithms.garrisons;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.combat.resources.shots;
import engine.gameplay.common.status.components.disabled;

// The Spectre gunship's orders: SpectreGunshipDeploymentUpdate calling one in, SpectreGunshipUpdate starting its attack,
// and the tick's decisions carried out.
export namespace generalszh::gameplay
{
namespace spectre_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;
using Engine::Math::FixedVector3;

// Object::setDisabled / clearDisabled(DISABLED_PARALYZED).
inline void Paralyze(GameWorld &game, ecs::Entity entity, bool on)
{
	auto &world = game.world;
	if (!world.Has<gp::Disabled>(entity))
	{
		if (!on)
			return;
		world.Add<gp::Disabled>(entity);
	}
	auto &mask = world.Get<gp::Disabled>(entity)->mask;
	mask = on ? (mask | gp::disabled_type::Paralyzed) : (mask & ~gp::disabled_type::Paralyzed);
}
}

// SpectreGunshipUpdate::initiateIntentToDoSpecialPower: a player's order sets the area, the reticle and where its
// gattling's fire starts at the spot; a script's only the area (and readies the power). Either way it comes in on its
// panic set, afterburners on, its gattling made inside it (paralysed until it circles), and its power triggered.
inline void StartGunshipAttack(GameWorld &game, ecs::Entity gunship, const SpectreGunshipConfig &config, const Engine::Math::FixedVector3 &target, bool fromScript)
{
	using namespace spectre_detail;
	auto &world = game.world;
	auto *ship = world.Get<SpectreGunship>(gunship);
	if (ship == nullptr)
		return;
	if (!fromScript)
	{
		ship->initialTarget = ship->reticle = ship->gattlingTarget = target;
	}
	else
		ship->initialTarget = target;
	ship->status = GunshipStatus::Inserting;
	ChooseLocomotorSet(game, gunship, locomotor_set::Panic);
	if (!config.gattling.empty())
	{
		const auto *member = world.Get<gp::TeamMember>(gunship);
		const auto *at = world.Get<gp::Transform>(gunship);
		const ecs::Entity gattling = SpawnObject(game, config.gattling, at->position.XY(), at->facing, member != nullptr ? member->team : 0u, "");
		// addToContain: a portable structure mounts on its HelixContain.
		if (world.IsAlive(gattling) && ContainInSource(game, gunship, gattling))
		{
			spectre_detail::Paralyze(game, gattling, true);
			if (auto *again = world.Get<SpectreGunship>(gunship))
				again->gattling = gattling;
		}
	}
	TriggerSpecialPower(game, gunship, config.power, target);
}

// SpectreGunshipDeploymentUpdate::initiateIntentToDoSpecialPower: a new gunship on the building's team, at the map's
// edge its CreateLocation names, beyond that point from the target by its orbit radius (none: never read), at its
// locomotor's preferred height, facing the target; made by the building (setProducer); its own power fired at the
// spot; then the building's power triggered.
inline bool DeploySpectreGunship(GameWorld &game, ecs::Entity source, const SpectreDeploymentConfig &config, Engine::Math::FixedVector2 target, bool fromScript)
{
	using namespace spectre_detail;
	auto &world = game.world;
	const auto *from = world.Get<gp::Transform>(source);
	const auto *member = world.Get<gp::TeamMember>(source);
	if (from == nullptr || member == nullptr || config.gunship.empty())
		return false;
	const FixedVector3 spot{target.x, target.y, game.ground.At(target)};
	FixedVector3 edge;
	switch (config.entry)
	{
	case GunshipEntry::EdgeNearSource: edge = game.ground.ClosestEdgePoint(from->position.XY()); break;
	case GunshipEntry::EdgeNearTarget: edge = game.ground.ClosestEdgePoint(target); break;
	case GunshipEntry::EdgeFarthestFromSource:
	case GunshipEntry::EdgeFarthestFromTarget:
	{
		// findFarthestEdgePoint: the far corner (by the map's halves).
		const FixedVector2 about = config.entry == GunshipEntry::EdgeFarthestFromSource ? from->position.XY() : target;
		const auto [low, high] = game.ground.Extent();
		const FixedVector2 corner{about.x < (high.x - low.x) / Fixed::FromInt(2) ? high.x : low.x, about.y < (high.y - low.y) / Fixed::FromInt(2) ? high.y : low.y};
		edge = {corner.x, corner.y, game.ground.At(corner)};
		break;
	}
	}
	const ecs::Entity gunship = SpawnObject(game, config.gunship, edge.XY(), Engine::Math::Heading(target - edge.XY()), member->team, "");
	if (!world.IsAlive(gunship))
		return false;
	SetProducer(game, gunship, source);
	Fixed height;
	if (const auto *motion = world.Get<gp::Locomotion>(gunship))
		height = motion->locomotor.preferredHeight;
	world.Get<gp::Transform>(gunship)->position = {edge.x, edge.y, height};
	const auto *shipRef = world.Get<gp::DefinitionRef>(gunship);
	if (const SpectreGunshipConfig *ship = shipRef != nullptr ? game.templates.SpectreGunshipOf(shipRef->index) : nullptr)
		StartGunshipAttack(game, gunship, *ship, spot, fromScript);
	TriggerSpecialPower(game, source, config.power, spot);
	return true;
}

// The tick's gunship orders.
inline void ApplyGunshipEvents(GameWorld &game)
{
	using namespace spectre_detail;
	auto *events = game.world.FindResource<GunshipEvents>();
	if (events == nullptr)
		return;
	const std::vector<GunshipEvent> list = std::move(events->list);
	events->list.clear();
	for (const GunshipEvent &event : list)
	{
		auto &world = game.world;
		if (!world.IsAlive(event.gunship))
			continue;
		const auto *ship = world.Get<SpectreGunship>(event.gunship);
		const ecs::Entity gattling = ship != nullptr && world.IsAlive(ship->gattling) ? ship->gattling : ecs::Entity{};
		switch (event.order)
		{
		case GunshipOrder::ShipMove:
			OrderMove(game, event.gunship, event.at.XY(), false, false);
			break;
		case GunshipOrder::ShipSet:
			ChooseLocomotorSet(game, event.gunship, event.set);
			break;
		case GunshipOrder::GattlingAttack:
			if (gattling != ecs::Entity{})
			{
				OrderAttack(game, gattling, event.target, 0, engine::gameplay::CommandSource::Ai);
				AiCommanded(game, gattling);
			}
			break;
		case GunshipOrder::GattlingAttackAt:
			if (gattling != ecs::Entity{})
				OrderAttackPosition(game, gattling, event.at, 9999, false);
			break;
		case GunshipOrder::GattlingParalyzed:
			if (gattling != ecs::Entity{})
				spectre_detail::Paralyze(game, gattling, event.set != 0);
			break;
		case GunshipOrder::Howitzer:
			if (const SpectreGunshipConfig *config = game.templates.SpectreGunshipOf(world.Get<gp::DefinitionRef>(event.gunship)->index))
				if (auto *fires = world.FindResource<gp::TemporaryWeaponFires>())
					fires->Add({event.gunship, config->howitzer, world.Get<gp::Owner>(event.gunship)->player, world.Get<gp::Transform>(event.gunship)->position, event.at});
			break;
		case GunshipOrder::DestroyGattling:
			if (gattling != ecs::Entity{})
				RetireNow(game, {gattling});
			break;
		case GunshipOrder::DestroyShip:
			RetireNow(game, {event.gunship});
			break;
		}
	}
}
}
