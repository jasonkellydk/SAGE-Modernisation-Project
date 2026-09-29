export module games.generalszh.gameplay.crates.algorithms.crate_rules;
import engine.gameplay.common.identity.components.team_member;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.crates.resources.crates;
import games.generalszh.content.crates.crate_content;
import games.generalszh.content.combat.loadout_content;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.world.algorithms.level_setup;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.algorithms.find_position;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.lifetime.components.lifetime;
import engine.gameplay.common.health.components.health;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.sciences.resources.player_sciences;
import engine.gameplay.rts.loadout.components.loadout;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.rts.navigation.resources.navigation_grid;
import engine.gameplay.rts.navigation.algorithms.clearance;
import engine.gameplay.rts.harvesting.resources.harvest_catalog;
import engine.gameplay.rts.upgrades.resources.player_upgrades;

// Zero Hour's crates, between ticks with the session's random stream:
//   CreateCrateDie::onDie: a dead object's CrateData makes its crate when the
//   killer is no ally, the chance holds, and the dead one's veterancy, the
//   killer's kinds and science are right; the crate object the roll picks,
//   a random way round, on open ground within 5 of where it died (else 125);
//   the dead one's player's when OwnedByMaker, else nobody's. (The original's
//   AI never goes for them: checkForCrateToPickup forgets the crate first.)
//   CrateCollide::onCollide: the first to touch it that may pick it up
//   (isValidToExecute: owned by a player, with an AI or a building that may
//   pick it up, of its kinds, alive, not its owner when forbidden, human when
//   only humans may, with the science it needs, not a parachute) gets it and
//   it goes. A salvage crate (SalvageCrateCollide, salvagers only) gives the
//   next armor set when it can, else the next weapon set (WeaponChance), else a
//   level (LevelChance, trainable and below HEROIC), else money. A money crate
//   (MoneyCrateCollide) gives its money and the boost of the first of its
//   upgrades the player has (getUpgradedSupplyBoost).
export namespace generalszh::gameplay
{
namespace crate_rules_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;

inline std::uint32_t PlayerOf(const GameWorld &game, std::uint32_t team)
{
	return team < game.roster.TeamCount() ? game.roster.TeamAt(team).owner : 0u;
}

inline bool HasAll(const content::KindOfMask &kinds, const content::KindOfMask &mask)
{
	for (std::size_t word = 0; word < mask.size(); ++word)
		if ((kinds[word] & mask[word]) != mask[word])
			return false;
	return true;
}

inline bool HasAny(const content::KindOfMask &kinds, const content::KindOfMask &mask)
{
	for (std::size_t word = 0; word < mask.size(); ++word)
		if ((kinds[word] & mask[word]) != 0)
			return true;
	return false;
}

// UniformReal(0, 1) on the session's stream, as a share.
inline Fixed Roll(GameWorld &game) { return Engine::Math::UniformFixed(game.random, Fixed{}, Fixed::One() - Fixed::FromRaw(1)); }
}

// CrateCollide::isValidToExecute: whether `other` may collect the crate `crate` (its collide `collide`): not neutral
// (the map's neutral player, the one with no name), with an AI or a building that may pick it up, of its kinds, not
// dead, the crate on the ground (Thing::isAboveTerrain, within a tenth; a building's pickup: anywhere), not its owner
// when forbidden, human when only humans may, with the science it needs, not a parachute.
inline bool CrateCollideAllows(GameWorld &game, ecs::Entity crate, const content::CrateCollideContent &collide, ecs::Entity other)
{
	using namespace crate_rules_detail;
	auto &world = game.world;
	const auto *otherDefinition = world.IsAlive(other) ? world.Get<gp::DefinitionRef>(other) : nullptr;
	if (otherDefinition == nullptr || !world.IsAlive(crate))
		return false;
	const content::ObjectDefinition &definition = game.templates.DefinitionAt(otherDefinition->index);
	const auto *owner = world.Get<gp::Owner>(other);
	const std::uint32_t player = owner != nullptr ? owner->player : 0u;
	if (owner == nullptr || player >= game.roster.PlayerCount() || game.roster.PlayerAt(player).name.empty())
		return false;
	const bool building = collide.buildingPickup && definition.Is("STRUCTURE");
	if (!game.templates.HasAI(otherDefinition->index) && !building)
		return false;
	if (!HasAll(definition.kinds, collide.required) || HasAny(definition.kinds, collide.forbidden))
		return false;
	if (const auto *health = world.Get<gp::Health>(other); health != nullptr && gp::IsDead(*health))
		return false;
	if (const auto *at = world.Get<gp::Transform>(crate); at != nullptr && !building && at->position.z - game.ground.At(at->position.XY()) > Fixed::FromRatio(1, 10))
		return false;
	const auto *crateOwner = world.Get<gp::Owner>(crate);
	if (collide.forbidOwner && crateOwner != nullptr && crateOwner->player == player)
		return false;
	if (collide.humanOnly && world.Resource<gp::HarvestCatalog>().Computer(player))
		return false;
	if (!collide.pickupScience.empty())
	{
		const auto science = game.templates.Content().Science(collide.pickupScience);
		if (!science || !world.Resource<gp::PlayerSciences>().Has(player, *science))
			return false;
	}
	return !definition.Is("PARACHUTE");
}

// One CrateData of a dead object's CreateCrateDie.
void DropCrate(GameWorld &game, std::string_view crateData, ecs::Entity killer, std::uint32_t victimTeam, std::uint32_t victimLevel,
	Engine::Math::FixedVector3 at)
{
	using namespace crate_rules_detail;
	const auto &templates = game.templates.Content().crates;
	const auto found = templates.find(crateData);
	if (found == templates.end())
		return;
	const content::CrateTemplate &crate = found->second;
	auto &world = game.world;
	const std::uint32_t victimPlayer = PlayerOf(game, victimTeam);
	const bool killerAlive = world.IsAlive(killer);
	const auto *killerOwner = killerAlive ? world.Get<gp::Owner>(killer) : nullptr;
	if (killerOwner != nullptr && world.Resource<gp::Relationships>().Allies(killerOwner->player, victimPlayer))
		return; // no crate for killing an ally
	if (!(Roll(game) < crate.creationChance))
		return;
	if (crate.veterancyLevel && *crate.veterancyLevel != victimLevel)
		return;
	const auto *killerDefinition = killerAlive ? world.Get<gp::DefinitionRef>(killer) : nullptr;
	if (HasAny(crate.killedByKinds, crate.killedByKinds) &&
		(killerDefinition == nullptr || !HasAll(game.templates.DefinitionAt(killerDefinition->index).kinds, crate.killedByKinds)))
		return;
	if (!crate.killerScience.empty())
	{
		const auto science = game.templates.Content().Science(crate.killerScience);
		if (killerOwner == nullptr || !science || !world.Resource<gp::PlayerSciences>().Has(killerOwner->player, *science))
			return;
	}
	// The crate object: the first whose running chance passes the roll.
	const Fixed pick = Roll(game);
	Fixed total;
	std::string name;
	for (const auto &[object, chance] : crate.crates)
	{
		total += chance;
		if (total > pick)
		{
			name = object;
			break;
		}
	}
	if (name.empty())
		return;
	// Open ground near where it died (the dead and its hulk never block).
	const auto &grid = world.Resource<gp::NavigationGrid>();
	const gp::ClearancePlane *plane = grid.ClearanceFor(gp::locomotor_surface::Ground);
	const auto legal = [&](Engine::Math::FixedVector2 point) {
		const std::int32_t x = static_cast<std::int32_t>((point.x / Fixed::FromInt(gp::PathfindCellSize)).Floor());
		const std::int32_t y = static_cast<std::int32_t>((point.y / Fixed::FromInt(gp::PathfindCellSize)).Floor());
		return plane == nullptr || engine::gameplay::Passable(grid, *plane, x, y, 0);
	};
	const Engine::Math::TurnAngle start{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 0xFFFFFFFFll))};
	auto spot = gp::FindPositionAround(at.XY(), Fixed{}, Fixed::FromInt(5), start, legal);
	if (!spot)
		spot = gp::FindPositionAround(at.XY(), Fixed{}, Fixed::FromInt(125), start, legal);
	if (!spot)
		return;
	const Engine::Math::TurnAngle facing{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 0xFFFFFFFFll))};
	const std::uint32_t team = crate.ownedByMaker ? victimTeam : 0u;
	SpawnObject(game, name, *spot, facing, team == 0xFFFFFFFFu ? 0u : team, "");
}

// This tick's touches: each crate goes to the first toucher that may take it.
void PickUpCrates(GameWorld &game, const std::vector<CrateTouch> &touches, CratePickups &pickups)
{
	using namespace crate_rules_detail;
	auto &world = game.world;
	std::vector<ecs::Entity> taken;
	for (const CrateTouch &touch : touches)
	{
		if (std::find(taken.begin(), taken.end(), touch.crate) != taken.end() || !world.IsAlive(touch.crate) || !world.IsAlive(touch.toucher))
			continue;
		const auto *crateDefinition = world.Get<gp::DefinitionRef>(touch.crate);
		const auto *otherDefinition = world.Get<gp::DefinitionRef>(touch.toucher);
		if (crateDefinition == nullptr || otherDefinition == nullptr || world.Has<Crate>(touch.toucher))
			continue;
		const auto collide = content::ReadCrateCollide(game.templates.DefinitionAt(crateDefinition->index));
		if (!collide)
			continue;
		const content::ObjectDefinition &other = game.templates.DefinitionAt(otherDefinition->index);
		if (!CrateCollideAllows(game, touch.crate, *collide, touch.toucher))
			continue;
		const std::uint32_t player = world.Get<gp::Owner>(touch.toucher)->player;
		const auto &at = world.Get<gp::Transform>(touch.crate)->position;
		CratePickup pickup{touch.toucher, CratePickup::Kind::Money, 0, player, at, collide->executeFX};
		pickup.animation = collide->executeAnimation;
		pickup.animationSeconds = collide->executeAnimationSeconds;
		pickup.animationRise = collide->executeAnimationRise;
		pickup.animationFades = collide->executeAnimationFades;
		bool executed = false;
		if (collide->kind == content::CrateKind::Salvage)
		{
			if (!other.Is("SALVAGER"))
				continue;
			executed = true;
			gp::Loadout *loadout = world.Get<gp::Loadout>(touch.toucher);
			gp::Experience *experience = world.Get<gp::Experience>(touch.toucher);
			const std::uint32_t armorOne = content::SetFlag(content::ArmorSetFlagNames, "CRATE_UPGRADE_ONE"),
								armorTwo = content::SetFlag(content::ArmorSetFlagNames, "CRATE_UPGRADE_TWO");
			const std::uint32_t weaponOne = content::SetFlag(content::WeaponSetFlagNames, "CRATEUPGRADE_ONE"),
								weaponTwo = content::SetFlag(content::WeaponSetFlagNames, "CRATEUPGRADE_TWO");
			const bool armor = other.Is("ARMOR_SALVAGER") && loadout != nullptr && (loadout->armorFlags & armorTwo) == 0;
			const bool weapons = !armor && other.Is("WEAPON_SALVAGER") && loadout != nullptr && (loadout->weaponFlags & weaponTwo) == 0 &&
				(collide->weaponChance == Fixed::One() || Roll(game) < collide->weaponChance);
			const bool level = !armor && !weapons && experience != nullptr && experience->trainable && experience->level < 3 &&
				(collide->levelChance == Fixed::One() || Roll(game) < collide->levelChance);
			if (armor)
			{
				loadout->armorFlags = (loadout->armorFlags & armorOne) != 0 ? (loadout->armorFlags & ~armorOne) | armorTwo : loadout->armorFlags | armorOne;
				pickup.kind = CratePickup::Kind::Salvage;
			}
			else if (weapons)
			{
				loadout->weaponFlags = (loadout->weaponFlags & weaponOne) != 0 ? (loadout->weaponFlags & ~weaponOne) | weaponTwo : loadout->weaponFlags | weaponOne;
				pickup.kind = CratePickup::Kind::Salvage;
			}
			else if (level)
			{
				PlaceAtVeterancy(game, touch.toucher, experience->level + 1);
				pickup.kind = CratePickup::Kind::Level;
			}
			else
			{
				pickup.amount = collide->minMoney != collide->maxMoney ? Engine::Math::UniformInt(game.random, collide->minMoney, collide->maxMoney) : collide->minMoney;
				pickup.floats = true;
				if (pickup.amount > 0)
					world.Resource<gp::PlayerMoney>().Earn(player, pickup.amount); // SalvageCrateCollide: addMoneyEarned
			}
		}
		else if (collide->kind == content::CrateKind::Money)
		{
			executed = true;
			pickup.amount = collide->moneyProvided;
			const auto &completed = world.Resource<gp::PlayerUpgrades>().Completed(player);
			for (const auto &[name, boost] : collide->upgradeBoosts)
				if (const auto upgrade = game.templates.Content().upgrades.Find(name); upgrade && completed.Has(*upgrade))
				{
					pickup.amount += boost;
					break;
				}
			if (pickup.amount > 0)
				world.Resource<gp::PlayerMoney>().Earn(player, pickup.amount); // MoneyCrateCollide: addMoneyEarned
		}
		else if (collide->kind == content::CrateKind::Unit)
		{
			// UnitCrateCollide::executeCrateBehavior: UnitCount of UnitName on the collector's player's default team, each
			// set down within 20 of it (findPositionAround), facing its way; an unknown unit: not taken.
			const content::ObjectDefinition *unit = game.templates.Content().objects.Find(collide->unitName);
			if (unit == nullptr)
				continue;
			executed = true;
			pickup.kind = CratePickup::Kind::Unit;
			const auto &from = *world.Get<gp::Transform>(touch.toucher);
			const std::uint32_t team = game.roster.DefaultTeam(player).value_or(world.Has<gp::TeamMember>(touch.toucher) ? world.Get<gp::TeamMember>(touch.toucher)->team : 0u);
			const auto &grid = world.Resource<gp::NavigationGrid>();
			const auto open = [&](Engine::Math::FixedVector2 point) {
				const std::int32_t x = static_cast<std::int32_t>((point.x / Fixed::FromInt(gp::PathfindCellSize)).Floor());
				const std::int32_t y = static_cast<std::int32_t>((point.y / Fixed::FromInt(gp::PathfindCellSize)).Floor());
				return grid.Width() == 0 || (grid.Contains(x, y) && grid.Type(x, y) == gp::PathfindCellType::Clear);
			};
			for (std::uint32_t count = 0; count < collide->unitCount; ++count)
			{
				const auto spot = gp::FindPositionAround(from.position.XY(), Fixed{}, Fixed::FromInt(20), from.facing, open).value_or(from.position.XY());
				SpawnObject(game, collide->unitName, spot, from.facing, team, "");
			}
		}
		if (!executed)
			continue;
		taken.push_back(touch.crate);
		pickups.list.push_back(std::move(pickup));
		// TheGameLogic->destroyObject: gone (not killed).
		if (!world.Has<gp::Lifetime>(touch.crate))
			world.Add<gp::Lifetime>(touch.crate);
		*world.Get<gp::Lifetime>(touch.crate) = gp::Lifetime{game.tick, 1, 0};
	}
}
}
