export module games.generalszh.gameplay.containment.algorithms.garrisons;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.ecs.query.query;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.rts.containment.components.garrison_points;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.components.mount;
import engine.gameplay.rts.veterancy.components.experience;
export import games.generalszh.gameplay.containment.components.held_aboard;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.appearance.components.appearance;
import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.spatial.components.targetable;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.stealth.components.stealth;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.content.objects.model_conditions;
import games.generalszh.content.combat.weapon_bonus_content;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.resources.relationships;

// Zero Hour's garrisons (GarrisonContain), between ticks:
//   isValidContainerFor / onBodyDamageStateChange: a building with no health,
//   or really damaged (unless GARRISONABLE_UNTIL_DESTROYED), takes nobody in
//   and sends everyone inside out;
//   recalcApparentControllingPlayer: occupied, it is its first occupant's
//   player's (their default team); empty, its original team's again;
//   MODELCONDITION_GARRISONED while occupied; those inside have
//   WEAPONBONUSCONDITION_GARRISONED (their range and damage grow) and lose it
//   once out;
//   healObjects (HealObjects): each inside heals its most over TimeForFullHeal
//   a frame. A subdued garrison's occupants hold their fire; InitialRoster
//   occupants are made on its team and put inside as it is first tended.
export namespace generalszh::gameplay
{
// HelixContain / OverlordContain::addToContain of a portable structure: `rider` mounts on `carrier` (its one), riding
// where it is, armed (the Helix always lets it fire: isPassengerAllowedToFire; the Overlord as its
// PassengersAllowedToFire says).
inline void MountOn(GameWorld &game, ecs::Entity carrier, ecs::Entity rider)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const gp::Transport *transport = world.Get<gp::Transport>(carrier);
	const bool armed = transport != nullptr && (transport->definition.passengersFire || transport->definition.garrisonsRiders);
	world.Add<gp::Mounted>(rider);
	world.Get<gp::Mounted>(rider)->carrier = carrier;
	if (!world.Has<gp::Mount>(carrier))
		world.Add<gp::Mount>(carrier);
	world.Get<gp::Mount>(carrier)->rider = rider;
	if (!world.Has<gp::OffMap>(rider))
		world.Add<gp::OffMap>(rider);
	*world.Get<gp::OffMap>(rider) = gp::OffMap{2, armed, {}, carrier};
	if (const auto *at = world.Get<gp::Transform>(carrier))
		*world.Get<gp::Transform>(rider) = *at;
	// OverlordContain::activateRedirectedContain: carrying a structure that holds passengers (a Battle Bunker), it takes
	// them in as that structure would (its room, who fires, what they suffer as it dies: the bunker's TransportContain),
	// its own marks (what it mounts, its experience sink, its redirect) kept.
	if (auto *own = world.Get<gp::Transport>(carrier); own != nullptr && own->definition.redirectsToMount)
		if (const auto *held = world.Get<gp::Transport>(rider))
		{
			gp::TransportDefinition redirected = held->definition;
			redirected.mountsPortable = own->definition.mountsPortable;
			redirected.experienceSink = own->definition.experienceSink;
			redirected.redirectsToMount = true;
			redirected.unloadInAir = own->definition.unloadInAir;
			own->definition = redirected;
		}
	// OverlordContain::onContaining: ExperienceSinkForRider, its kills make the carrier a veteran.
	if (auto *experience = world.Get<gp::Experience>(rider); experience != nullptr && transport != nullptr && transport->definition.experienceSink)
		experience->sink = carrier;
}

// OpenContain::addToContain: `passenger` straight inside `container` (off the map, armed as it lets it be), taking
// `slots` of its room (a TransportContain's getTransportSlotCount; one elsewhere); `quiet`: without its load sound.
inline void PutInside(GameWorld &game, ecs::Entity container, ecs::Entity passenger, std::uint32_t slots = 1, bool quiet = false)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	gp::Transport *transport = world.Get<gp::Transport>(container);
	if (transport == nullptr || !world.IsAlive(passenger))
		return;
	const gp::Targetable *kind = world.Get<gp::Targetable>(passenger);
	const bool infantry = kind != nullptr && (kind->classes & gp::target_class::Infantry) != 0;
	const bool armed = transport->definition.passengersFire && (!transport->definition.infantryOnly || infantry);
	world.Add<gp::Passenger>(passenger);
	*world.Get<gp::Passenger>(passenger) = {container, slots, 0, game.tick};
	world.Add<gp::OffMap>(passenger);
	*world.Get<gp::OffMap>(passenger) = gp::OffMap{1, armed, {}, container};
	world.Resource<gp::CargoManifest>().Board(container, passenger, quiet);
	transport->occupied += slots;
}

// aiEvacuateInstantly for one rider: out of `container` at once, where it stands (removeFromContain), its room freed.
inline void TakeOutNow(GameWorld &game, ecs::Entity container, ecs::Entity passenger)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	gp::Transport *transport = world.Get<gp::Transport>(container);
	const gp::Passenger *seat = world.Get<gp::Passenger>(passenger);
	if (transport == nullptr || seat == nullptr || seat->transport != container)
		return;
	transport->occupied -= std::min(transport->occupied, seat->slots);
	world.Resource<gp::CargoManifest>().Take(container, passenger);
	world.Remove<gp::Passenger>(passenger);
	world.Remove<gp::OffMap>(passenger);
	if (const auto *at = world.Get<gp::Transform>(container))
		if (auto *place = world.Get<gp::Transform>(passenger))
			place->position = at->position;
}

// ObjectCreationList ContainInsideSourceObject: `made` goes inside `container` if it may (isValidContainerFor with
// capacity): a portable structure mounts on a carrier of them with none yet; anything else takes a free slot. False
// when it may not (the caller stillborns it).
inline bool ContainInSource(GameWorld &game, ecs::Entity container, ecs::Entity made)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.IsAlive(container) || !world.IsAlive(made))
		return false;
	const gp::Transport *transport = world.Get<gp::Transport>(container);
	if (transport == nullptr)
		return false;
	const auto *ref = world.Get<gp::DefinitionRef>(made);
	const bool portable = ref != nullptr && game.templates.DefinitionAt(ref->index).Is("PORTABLE_STRUCTURE");
	if (portable && transport->definition.mountsPortable)
	{
		if (world.Has<gp::Mount>(container))
			return false;
		MountOn(game, container, made);
		return true;
	}
	if (transport->closed || transport->occupied >= transport->definition.slots || !MayContain(game, container, made))
		return false;
	PutInside(game, container, made);
	return true;
}

inline void TendGarrisons(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const gp::CargoManifest &manifest = world.Resource<gp::CargoManifest>();
	constexpr std::uint32_t garrisoned = content::ModelConditionBit("GARRISONED");
	const Engine::Math::Fixed reallyDamaged = game.templates.Content().gameData.unitReallyDamaged;
	struct Held
	{
		ecs::Entity building;
		std::vector<ecs::Entity> inside;
	};
	std::vector<Held> garrisons;
	ecs::Query<ecs::Read<gp::Garrison>> query(world);
	query.ForEachChunk([&](auto chunk) {
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < entities.size(); ++row)
		{
			const auto aboard = manifest.Aboard(entities[row]);
			garrisons.push_back({entities[row], {aboard.begin(), aboard.end()}});
		}
	});
	std::vector<ecs::Entity> occupants;
	for (Held &held : garrisons)
	{
		gp::Garrison &garrison = *world.Get<gp::Garrison>(held.building);
		gp::Transport *transport = world.Get<gp::Transport>(held.building);
		const gp::Health *health = world.Get<gp::Health>(held.building);
		const gp::TeamMember *member = world.Get<gp::TeamMember>(held.building);
		if (transport == nullptr || health == nullptr || member == nullptr)
			continue;
		if (garrison.originalTeam == gp::Garrison::NoTeam)
		{
			garrison.originalTeam = member->team;
			garrison.originalPlayer = game.roster.TeamAt(member->team).owner;
		}
		// GarrisonContain::onObjectCreated: its InitialRoster, made on its team and put inside.
		for (; garrison.rosterLeft > 0 && transport->occupied < transport->definition.slots; --garrison.rosterLeft)
		{
			const auto *frame = world.Get<gp::Transform>(held.building);
			const ecs::Entity occupant = SpawnObject(game, std::string(game.templates.DefinitionAt(garrison.rosterDefinition).name),
				frame->position.XY(), frame->facing, member->team, "");
			PutInside(game, held.building, occupant);
			held.inside.push_back(occupant);
		}
		garrison.rosterLeft = 0;
		const bool wrecked = health->current <= Engine::Math::Fixed{} ||
			(!garrison.untilDestroyed && health->current <= health->maximum * reallyDamaged);
		transport->closed = wrecked;
		// GarrisonContain::findConditionIndex: the fire points of its damage state (rubble counts as really damaged).
		if (auto *points = world.Get<gp::GarrisonPoints>(held.building))
		{
			const Engine::Math::Fixed damaged = game.templates.Content().gameData.unitDamaged;
			points->condition = health->current <= health->maximum * reallyDamaged ? gp::garrison_condition::ReallyDamaged
				: health->current <= health->maximum * damaged                   ? gp::garrison_condition::Damaged
																				 : gp::garrison_condition::Pristine;
		}
		if (wrecked && !held.inside.empty())
			transport->state = gp::TransportState::Unloading;
		// Its side: its first occupant's player's, else its own again.
		std::uint32_t team = garrison.originalTeam;
		if (!held.inside.empty())
			if (const auto *owner = world.Get<gp::Owner>(held.inside.front()); owner != nullptr && owner->player < game.roster.PlayerCount())
				if (const auto own = game.roster.DefaultTeam(owner->player))
					team = *own;
		ChangeTeam(game, held.building, team);
		// recalcApparentControllingPlayer: all inside stealthy garrisoners (KINDOF_STEALTH_GARRISON) and the first
		// not detected: to players its side is not allied with it still looks empty and its original player's.
		garrison.hiddenFrom = 0;
		const auto stealthy = [&](ecs::Entity unit) {
			const auto *ref = world.Get<gp::DefinitionRef>(unit);
			return ref != nullptr && game.templates.DefinitionAt(ref->index).Is("STEALTH_GARRISON");
		};
		const auto *seen = held.inside.empty() ? nullptr : world.Get<gp::Stealth>(held.inside.front());
		const bool detected = seen != nullptr && seen->Has(gp::stealth_flag::Detected);
		if (!held.inside.empty() && !detected && std::all_of(held.inside.begin(), held.inside.end(), stealthy))
			if (const auto *owner = world.Get<gp::Owner>(held.building))
			{
				const auto &relationships = world.Resource<gp::Relationships>();
				for (std::uint32_t player = 0; player < std::min<std::uint32_t>(game.roster.PlayerCount(), 32); ++player)
					if (!relationships.Allies(owner->player, player))
						garrison.hiddenFrom |= 1u << player;
			}
		if (auto *look = world.Get<gp::Appearance>(held.building))
			look->Set(garrisoned, !held.inside.empty());
		// GarrisonContain::isPassengerAllowedToFire: always, unless the building is subdued.
		const auto *off = world.Get<gp::Disabled>(held.building);
		const bool subdued = off != nullptr && (off->mask & gp::disabled_type::Subdued) != 0;
		for (const ecs::Entity inside : held.inside)
		{
			occupants.push_back(inside);
			if (auto *away = world.Get<gp::OffMap>(inside); away != nullptr && transport->definition.passengersFire)
				away->armed = !subdued;
			if (!world.Has<gp::WeaponBonusConditions>(inside))
				world.Add<gp::WeaponBonusConditions>(inside);
			if (garrison.fullHealTicks > 0)
				if (auto *body = world.Get<gp::Health>(inside); body != nullptr && body->current < body->maximum)
				{
					const Engine::Math::Fixed heal = body->maximum / Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(garrison.fullHealTicks));
					gp::Heal(*body, heal, game.tick);
				}
		}
	}
	// HelixContain / OverlordContain::onCapture: a mounted portable structure goes over to its carrier's new player (its
	// default team) with it.
	{
		std::vector<std::pair<ecs::Entity, ecs::Entity>> mounted;
		ecs::Query<ecs::Read<gp::Mounted>> riders(world);
		riders.ForEachChunk([&](auto chunk) {
			const auto mounts = chunk.template Get<gp::Mounted>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < mounts.size(); ++row)
				mounted.emplace_back(entities[row], mounts[row].carrier);
		});
		for (const auto &[rider, carrier] : mounted)
		{
			const auto *riderOwner = world.Get<gp::Owner>(rider);
			const auto *carrierOwner = world.IsAlive(carrier) ? world.Get<gp::Owner>(carrier) : nullptr;
			if (riderOwner == nullptr || carrierOwner == nullptr || riderOwner->player == carrierOwner->player)
				continue;
			if (const auto team = game.roster.DefaultTeam(carrierOwner->player))
				ChangeTeam(game, rider, *team);
		}
	}
	// HelixContain::onContaining / onRemoving: its riders are held and garrisoned while aboard, and let go as they leave.
	{
		std::vector<ecs::Entity> aboard;
		ecs::Query<ecs::Read<gp::Transport>> holders(world);
		holders.ForEachChunk([&](auto chunk) {
			const auto transports = chunk.template Get<gp::Transport>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < transports.size(); ++row)
				if (transports[row].definition.garrisonsRiders)
					for (const ecs::Entity rider : manifest.Aboard(entities[row]))
						aboard.push_back(rider);
		});
		std::sort(aboard.begin(), aboard.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
		const auto isAboard = [&](ecs::Entity entity) {
			return std::binary_search(aboard.begin(), aboard.end(), entity, [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
		};
		std::vector<ecs::Entity> released;
		ecs::Query<ecs::Read<HeldAboard>> held(world);
		held.ForEachChunk([&](auto chunk) {
			for (const ecs::Entity entity : chunk.Entities())
				if (!isAboard(entity))
					released.push_back(entity);
		});
		for (const ecs::Entity rider : released)
		{
			if (auto *off = world.Get<gp::Disabled>(rider))
				off->mask &= ~gp::disabled_type::Held;
			world.Remove<HeldAboard>(rider);
		}
		for (const ecs::Entity rider : aboard)
		{
			occupants.push_back(rider);
			if (!world.Has<gp::WeaponBonusConditions>(rider))
				world.Add<gp::WeaponBonusConditions>(rider);
			if (world.Has<HeldAboard>(rider))
				continue;
			if (!world.Has<gp::Disabled>(rider))
				world.Add<gp::Disabled>(rider);
			world.Get<gp::Disabled>(rider)->mask |= gp::disabled_type::Held;
			world.Add<HeldAboard>(rider);
		}
	}
	// Garrisoned: the weapon bonus while inside one, not after.
	std::sort(occupants.begin(), occupants.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
	ecs::Query<ecs::Write<gp::WeaponBonusConditions>> bonuses(world);
	bonuses.ForEachChunk([&](auto chunk) {
		auto rows = chunk.template Get<gp::WeaponBonusConditions>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < rows.size(); ++row)
		{
			const bool inside = std::binary_search(occupants.begin(), occupants.end(), entities[row],
				[](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
			gp::SetWeaponBonus(rows[row], content::weapon_bonus::Garrisoned, inside, game.tick);
		}
	});
}

// PartitionFilterGarrisonableByPlayer: a garrison open to `player` (standing, taking people in, not full, and not held
// by `player`'s enemies), with its room left.
inline std::uint32_t GarrisonRoom(GameWorld &game, ecs::Entity building, std::uint32_t player)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	const gp::Transport *transport = world.IsAlive(building) ? world.Get<gp::Transport>(building) : nullptr;
	const gp::Health *health = transport != nullptr ? world.Get<gp::Health>(building) : nullptr;
	if (transport == nullptr || transport->closed || !world.Has<gp::Garrison>(building) || health == nullptr || gp::IsDead(*health))
		return 0;
	const auto count = static_cast<std::uint32_t>(world.Resource<gp::CargoManifest>().Count(building));
	if (count > 0)
		if (const auto *owner = world.Get<gp::Owner>(building); owner != nullptr && world.Resource<gp::Relationships>().Enemies(player, owner->player))
			return 0;
	return transport->definition.slots > count ? transport->definition.slots - count : 0u;
}

namespace garrison_detail
{
// Infantry that may garrison (KINDOF_INFANTRY, not KINDOF_NO_GARRISON).
inline bool MayGarrison(GameWorld &game, ecs::Entity unit)
{
	const auto *ref = game.world.IsAlive(unit) ? game.world.Get<engine::gameplay::DefinitionRef>(unit) : nullptr;
	if (ref == nullptr || game.world.Has<engine::gameplay::Passenger>(unit))
		return false;
	const auto &definition = game.templates.DefinitionAt(ref->index);
	return definition.Is("INFANTRY") && !definition.Is("NO_GARRISON");
}

// Every garrison open to `player`, nearest `from` first (3D), with its room.
inline std::vector<std::pair<ecs::Entity, std::uint32_t>> OpenGarrisons(GameWorld &game, Engine::Math::FixedVector3 from, std::uint32_t player)
{
	namespace gp = engine::gameplay;
	std::vector<std::tuple<Engine::Math::Fixed, ecs::Entity, std::uint32_t>> found;
	ecs::Query<ecs::Read<gp::Garrison>, ecs::Read<gp::Transform>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto transforms = chunk.template Get<gp::Transform>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < entities.size(); ++row)
			if (const std::uint32_t room = GarrisonRoom(game, entities[row], player); room > 0)
			{
				const auto delta = transforms[row].position - from;
				found.emplace_back(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z, entities[row], room);
			}
	});
	std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) {
		return std::get<0>(a) != std::get<0>(b) ? std::get<0>(a) < std::get<0>(b) : std::get<1>(a).index < std::get<1>(b).index;
	});
	std::vector<std::pair<ecs::Entity, std::uint32_t>> open;
	for (const auto &[distance, building, room] : found)
		open.emplace_back(building, room);
	return open;
}

inline std::uint32_t PlayerOf(GameWorld &game, ecs::Entity unit)
{
	const auto *owner = game.world.Get<engine::gameplay::Owner>(unit);
	return owner != nullptr ? owner->player : 0u;
}
}

// NAMED_GARRISON_SPECIFIC_BUILDING / TEAM_GARRISON_SPECIFIC_BUILDING: in they go (aiEnter), as room allows.
inline void GarrisonSpecific(GameWorld &game, std::span<const ecs::Entity> units, ecs::Entity building)
{
	for (const ecs::Entity unit : units)
		if (garrison_detail::MayGarrison(game, unit) && GarrisonRoom(game, building, garrison_detail::PlayerOf(game, unit)) > 0)
			OrderBoard(game, unit, building);
}

// ScriptActions::doTeamGarrisonNearestBuilding: from the team's first member, garrisons nearest first, each filled with
// the team's infantry in team order (others passed over), until the team runs out.
inline void GarrisonNearest(GameWorld &game, std::span<const ecs::Entity> team)
{
	using namespace garrison_detail;
	if (team.empty() || !game.world.IsAlive(team.front()))
		return;
	const auto *leader = game.world.Get<engine::gameplay::Transform>(team.front());
	if (leader == nullptr)
		return;
	std::size_t next = 0;
	for (const auto &[building, room] : OpenGarrisons(game, leader->position, PlayerOf(game, team.front())))
		for (std::uint32_t filled = 0; filled < room;)
		{
			if (next >= team.size())
				return;
			const ecs::Entity unit = team[next++];
			if (MayGarrison(game, unit))
			{
				OrderBoard(game, unit, building);
				++filled;
			}
		}
}

// Player::garrisonAllUnits: each of its infantry out in the world into the nearest garrison with room left.
inline void GarrisonAll(GameWorld &game, std::uint32_t player)
{
	using namespace garrison_detail;
	namespace gp = engine::gameplay;
	std::vector<ecs::Entity> units;
	ecs::Query<ecs::Read<gp::Owner>, ecs::Read<gp::Transform>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto owners = chunk.template Get<gp::Owner>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < entities.size(); ++row)
			if (owners[row].player == player && !game.world.Has<gp::Boarding>(entities[row]) && MayGarrison(game, entities[row]))
				units.push_back(entities[row]);
	});
	std::sort(units.begin(), units.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
	std::vector<std::pair<ecs::Entity, std::uint32_t>> promised; // places promised by this call
	const auto promisedTo = [&](ecs::Entity building) -> std::uint32_t & {
		for (auto &[to, count] : promised)
			if (to == building)
				return count;
		return promised.emplace_back(building, 0u).second;
	};
	for (const ecs::Entity unit : units)
	{
		const auto at = game.world.Get<gp::Transform>(unit)->position;
		for (const auto &[building, room] : OpenGarrisons(game, at, player))
		{
			std::uint32_t &count = promisedTo(building);
			if (count >= room)
				continue;
			OrderBoard(game, unit, building);
			++count;
			break;
		}
	}
}
}
