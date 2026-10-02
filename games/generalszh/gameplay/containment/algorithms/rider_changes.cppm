export module games.generalszh.gameplay.containment.algorithms.rider_changes;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.containment.components.rider_change;
import engine.gameplay.rts.stealth.systems.stealth_system;
import games.generalszh.gameplay.containment.algorithms.garrisons;
import games.generalszh.gameplay.veterancy.algorithms.veterancy_placement;
import games.generalszh.content.objects.object_status;
import games.generalszh.content.objects.model_conditions;
import games.generalszh.content.combat.combat_catalog;
import engine.gameplay.common.appearance.components.appearance;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.status.components.status_flags;
import engine.gameplay.common.lifetime.components.lifetime;
import engine.gameplay.rts.loadout.components.loadout;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import games.generalszh.gameplay.upgrades.components.command_set_override;
import games.generalszh.gameplay.orders.algorithms.wander_orders;

// RiderChangeContain, after the tick, for the riders its bikes took in and let out this tick, in order:
//   onContaining: the rider's own model condition, weapon set flag and object status go on the bike (those of the
//   rider it showed before off), its command set becomes the bike's (setCommandSetStringOverride) and the bike moves on
//   its locomotor set (chooseLocomotorSet: the Terrorist's SET_SLUGGISH), and the rider's veterancy passes to the bike
//   (setVeterancyLevel), the rider starting over (setExperienceAndLevel 0);
//   onRemoving, the bike alive: those come off again, the bike's veterancy goes back to the rider and the bike starts
//   over; then the bike is scuttled so nobody else can use it: UNSELECTABLE, its ScuttleStatus model condition, IMMOBILE
//   unless it moves, and ScuttleDelay on it dies TOPPLED (update: kill(DAMAGE_UNRESISTABLE, DEATH_TOPPLED), here its
//   lifetime).
//   a new rider getting on a bike that has one throws the old one off first (aiEvacuateInstantly: its onRemoving, the
//   bike not scuttled while it takes the new one, m_containing); a stealthed bike taking a rider is marked detected.
// (A dead bike's rider is deleted by the death system.)
export namespace generalszh::gameplay
{
namespace rider_change_detail
{
inline void Show(GameWorld &game, ecs::Entity bike, const RiderChange &change, std::uint32_t index, bool on)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (index >= change.count)
		return;
	if (auto *look = world.Get<gp::Appearance>(bike); look != nullptr && change.conditions[index] != RiderChange::None)
		look->Set(change.conditions[index], on);
	if (auto *loadout = world.Get<gp::Loadout>(bike))
		loadout->weaponFlags = on ? loadout->weaponFlags | change.weaponFlags[index] : loadout->weaponFlags & ~change.weaponFlags[index];
	if (change.statuses[index] < 64)
	{
		if (!world.Has<gp::StatusFlags>(bike))
			world.Add<gp::StatusFlags>(bike);
		auto &bits = world.Get<gp::StatusFlags>(bike)->bits;
		bits = on ? bits | (std::uint64_t{1} << change.statuses[index]) : bits & ~(std::uint64_t{1} << change.statuses[index]);
	}
}

inline std::int64_t LevelOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *experience = game.world.Get<engine::gameplay::Experience>(entity);
	return experience != nullptr ? experience->level : 0;
}
}

inline void ApplyRiderChanges(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using namespace rider_change_detail;
	auto &world = game.world;
	const std::vector<gp::CargoChange> changes(world.Resource<gp::CargoManifest>().Changes().begin(), world.Resource<gp::CargoManifest>().Changes().end());
	for (const gp::CargoChange &move : changes)
	{
		RiderChange *change = world.IsAlive(move.container) ? world.Get<RiderChange>(move.container) : nullptr;
		if (change == nullptr || !world.IsAlive(move.rider))
			continue;
		const auto *ref = world.Get<gp::DefinitionRef>(move.rider);
		std::uint32_t index = RiderChange::None;
		for (std::uint32_t slot = 0; ref != nullptr && slot < change->count; ++slot)
			if (change->definitions[slot] == ref->index)
				index = slot;
		const ecs::Entity bike = move.container;
		if (move.entered)
		{
			if (index == RiderChange::None)
				continue;
			// RiderChangeContain::onContaining: its rider before this one is thrown off (onRemoving with m_containing: the
			// bike keeps going, its veterancy back to that rider).
			const auto aboard = game.manifest.Aboard(bike);
			const std::vector<ecs::Entity> others(aboard.begin(), aboard.end());
			for (const ecs::Entity old : others)
			{
				if (old == move.rider)
					continue;
				TakeOutNow(game, bike, old);
				change = world.Get<RiderChange>(bike);
				if (const auto *oldRef = world.Get<gp::DefinitionRef>(old))
					for (std::uint32_t slot = 0; slot < change->count; ++slot)
						if (change->definitions[slot] == oldRef->index)
							Show(game, bike, *change, slot, false);
				PlaceAtVeterancy(game, old, LevelOf(game, bike), true);
				PlaceAtVeterancy(game, bike, 0, true);
				change = world.Get<RiderChange>(bike);
				change->current = RiderChange::None;
			}
			// A stealthed bike is found out as it takes a rider (StealthUpdate::markAsDetected).
			if (auto *stealth = world.Get<gp::Stealth>(bike); stealth != nullptr && stealth->Has(gp::stealth_flag::Stealthed))
				gp::MarkAsDetected(*stealth, gp::stealth_detail::RulesOf(*stealth, world.Get<gp::StealthRider>(bike)), game.tick, 0);
			if (change->current != RiderChange::None && change->current != index)
				Show(game, bike, *change, change->current, false);
			Show(game, bike, *change, index, true);
			change->current = index;
			if (const std::uint32_t set = change->commandSets[index]; set != RiderChange::None)
			{
				if (auto *swapped = world.Get<CommandSetOverride>(bike))
					swapped->id = set;
				else
				{
					world.Add<CommandSetOverride>(bike);
					world.Get<CommandSetOverride>(bike)->id = set;
				}
			}
			ChooseLocomotorSet(game, bike, change->locomotorSets[index]);
			change = world.Get<RiderChange>(bike); // adding a component may have moved it
			PlaceAtVeterancy(game, bike, LevelOf(game, move.rider), true);
			PlaceAtVeterancy(game, move.rider, 0, true);
			continue;
		}
		// Let out of a live bike.
		if (world.Has<gp::Dying>(bike))
			continue;
		if (index != RiderChange::None)
			Show(game, bike, *change, index, false);
		if (change->current == index)
			change->current = RiderChange::None;
		PlaceAtVeterancy(game, move.rider, LevelOf(game, bike), true);
		PlaceAtVeterancy(game, bike, 0, true);
		if (change->scuttledTick != 0)
			continue;
		change->scuttledTick = std::max<std::uint64_t>(game.tick, 1);
		if (!world.Has<gp::StatusFlags>(bike))
			world.Add<gp::StatusFlags>(bike);
		auto &bits = world.Get<gp::StatusFlags>(bike)->bits;
		bits |= std::uint64_t{1} << content::ObjectStatusBit("UNSELECTABLE");
		const auto *order = world.Get<gp::MoveOrder>(bike);
		if (order == nullptr || order->mode == gp::MoveMode::Idle)
			bits |= std::uint64_t{1} << content::ObjectStatusBit("IMMOBILE");
		if (!world.Has<gp::Lifetime>(bike))
			world.Add<gp::Lifetime>(bike);
		*world.Get<gp::Lifetime>(bike) = {game.tick + change->scuttleTicks, 0u, content::DeathTypeIndex("TOPPLED").value_or(0)};
	}
}
}
