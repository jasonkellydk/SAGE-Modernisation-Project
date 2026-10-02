export module games.generalszh.gameplay.containment.algorithms.transport_riders;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.content.objects.object_definition;
import games.generalszh.content.combat.loadout_content;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.common.weapons.components.weapon_slots;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.rts.combat.algorithms.weapon_fitness;
import engine.gameplay.rts.combat.algorithms.target_pitch;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.loadout.components.loadout;

// TransportContain's onContaining / onRemoving side effects on the tick's cargo changes, after the step:
//   letRidersUpgradeWeaponSet (ArmedRidersUpgradeMyWeaponSet, the Combat Chinook): after anyone gets in or out, the
//     transport takes WEAPONSET_PLAYER_UPGRADE while any INFANTRY rider has a weapon in any slot that is no contact
//     weapon and does damage (Weapon::isDamageWeapon), and drops it otherwise;
//   ResetMoodCheckTimeOnExit (default Yes): a rider that got out and is idle looks for a target now
//     (wakeUpAndAttemptToTarget: its next mood check is this tick's).
export namespace generalszh::gameplay
{
namespace transport_rider_detail
{
namespace gp = engine::gameplay;

inline bool ArmedRider(GameWorld &game, ecs::Entity rider, const gp::WeaponCatalog &weapons)
{
	const auto *ref = game.world.Get<gp::DefinitionRef>(rider);
	if (ref == nullptr || !game.templates.DefinitionAt(ref->index).Is("INFANTRY"))
		return false;
	const auto viable = [&](std::uint32_t weapon) {
		if (weapon == gp::WeaponCatalog::None)
			return false;
		const gp::WeaponDefinition &definition = weapons.At(weapon);
		return !gp::IsContactWeapon(definition) && gp::IsDamageWeapon(definition, weapons);
	};
	if (const auto *slots = game.world.Get<gp::WeaponSlots>(rider))
		return std::ranges::any_of(slots->slots, [&](const gp::WeaponSlot &slot) { return viable(slot.weapon); });
	const auto *armament = game.world.Get<gp::Armament>(rider);
	return armament != nullptr && viable(armament->weapon);
}
}

inline void ApplyTransportRiders(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using namespace transport_rider_detail;
	auto &world = game.world;
	const auto *weapons = world.FindResource<gp::WeaponCatalog>();
	if (weapons == nullptr)
		return;
	const std::uint32_t upgraded = content::SetFlag(content::WeaponSetFlagNames, "PLAYER_UPGRADE");
	const std::vector<gp::CargoChange> changes(game.manifest.Changes().begin(), game.manifest.Changes().end());
	std::vector<ecs::Entity> recounted;
	for (const gp::CargoChange &change : changes)
	{
		const gp::Transport *carrier = world.IsAlive(change.container) ? world.Get<gp::Transport>(change.container) : nullptr;
		if (carrier == nullptr)
			continue;
		if (!change.entered && carrier->definition.resetMoodOnExit && world.IsAlive(change.rider) && HasAi(game, change.rider) && IsIdle(game, change.rider))
			if (auto *aggression = world.Get<gp::Aggression>(change.rider))
				gp::WakeToTarget(*aggression, game.tick);
		if (!carrier->definition.armedRidersUpgrade || std::ranges::find(recounted, change.container) != recounted.end())
			continue;
		recounted.push_back(change.container);
		bool armed = false;
		for (const ecs::Entity rider : game.manifest.Aboard(change.container))
			armed = armed || (world.IsAlive(rider) && ArmedRider(game, rider, *weapons));
		if (auto *loadout = world.Get<gp::Loadout>(change.container))
			loadout->weaponFlags = armed ? loadout->weaponFlags | upgraded : loadout->weaponFlags & ~upgraded;
	}
}
}
