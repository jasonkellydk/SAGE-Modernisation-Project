export module games.generalszh.gameplay.academy.algorithms.academy_records;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.academy.resources.academy_stats;
import engine.ecs.query.query;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.rts.containment.components.tunnel;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.economy.resources.player_energy;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.status.components.status_flags;
import engine.gameplay.common.status.components.script_status;
import games.generalszh.content.objects.object_status;
import engine.gameplay.rts.combat.resources.garrison_kills;
import engine.gameplay.rts.containment.systems.garrison_kill_damage_system;
import engine.gameplay.rts.lifecycle.resources.casualties;
import engine.gameplay.rts.stealth.resources.detections;

// AcademyStats (Core/GameEngine/Source/Common/RTS/AcademyStats.cpp) as the simulation keeps it, each where the original
// records it (the callers name their own sites):
//   InitAcademy (init, from Player::init): its next update 30 frames on, its first update to come; a side the advice
//     knows (base side USA, China or GLA, any case) or else none at all (m_unknownSide). The original looks for its
//     dozer's command set to find the supply center and its cost, but findDozerCommandSet only sets its own copy of the
//     pointer: none is ever found, so the cost is always the default 1000.
//   UpdateAcademy (update, from Player::update, every frame: its test `m_nextUpdateFrame >= now` always holds once
//     made, the next update set 30 on each time): nothing for an unknown side; on its first update every object of the
//     player is recorded as produced (recordProduction with no constructor); with no supply center built and not yet
//     out of money, money under the supply center's cost marks it out; a change in having sufficient power records,
//     on losing it, when, and on gaining it, how long it was out (now less when it went out: the first frame with power
//     counts from frame 0).
//   RecordAcademyProduction (recordProduction: a factory's unit, a dozer's finished structure, the first update):
//     supply centers, dozers, military units (INFANTRY or VEHICLE, not DOZER nor HARVESTER: the longest wait between
//     them), harvesters, heroes, a strategy center had, a tunnel network had (its contain is one), secondary income
//     (MONEY_HACKER, FS_BLACK_MARKET, FS_SUPPLY_DROPZONE), a barracks by 5 minutes, a war factory by 10, an advanced
//     tech structure by 15 (frames counted as 30 a second), disguisers.
//   RecordAcademyUpgrade (recordUpgrade: researched, or granted by GrantUpgradeCreate): ACT_UPGRADE_RADAR marks the
//     radar; a researched one counts as purchased.
//   RecordAcademyPowerUsed (recordSpecialPowerUsed, SpecialPowerModule::initiateIntentToDoSpecialPower): ACT_SUPERPOWER
//     ones count.
//   RecordAcademyIncomes (recordIncome, Money::deposit of more than nothing, any deposit, sounding or not): the gap since
//     the last income (last - now, as the original's unsigned subtraction: wrapping, so any income makes it huge) kept
//     at its largest, and now the last.
//   The counters (recordBuildingCapture, ...): one more each.
export namespace generalszh::gameplay
{
inline constexpr std::uint32_t AcademyFramesBetweenUpdates = 30; // FRAMES_BETWEEN_UPDATES

namespace academy_detail
{
inline bool SameText(std::string_view a, std::string_view b) noexcept
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}

inline std::uint32_t Frame(const GameWorld &game) noexcept { return static_cast<std::uint32_t>(game.tick); }

inline PlayerAcademy *Of(GameWorld &game, std::uint32_t player)
{
	auto *stats = game.world.FindResource<AcademyStats>();
	return stats != nullptr ? &stats->Of(player) : nullptr;
}
}

inline void InitAcademy(AcademyStats &stats, std::uint32_t player, std::string_view baseSide, std::uint64_t tick)
{
	using academy_detail::SameText;
	PlayerAcademy &record = stats.Of(player);
	record = PlayerAcademy{};
	record.nextUpdateFrame = static_cast<std::uint32_t>(tick) + AcademyFramesBetweenUpdates;
	record.firstUpdate = true;
	record.unknownSide = !(SameText(baseSide, "USA") || SameText(baseSide, "China") || SameText(baseSide, "GLA"));
	record.supplyCenterCost = 1000;
}

inline void RecordAcademyProduction(GameWorld &game, std::uint32_t player, ecs::Entity entity)
{
	PlayerAcademy *record = academy_detail::Of(game, player);
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<engine::gameplay::DefinitionRef>(entity) : nullptr;
	if (record == nullptr || ref == nullptr)
		return;
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
	const std::uint32_t now = academy_detail::Frame(game);
	if (kind.Is("FS_SUPPLY_CENTER"))
		++record->supplyCentersBuilt;
	if (kind.Is("DOZER"))
		++record->peonsBuilt;
	if ((kind.Is("INFANTRY") || kind.Is("VEHICLE")) && !kind.Is("DOZER") && !kind.Is("HARVESTER"))
	{
		const std::uint32_t idleFrames = now - record->lastUnitBuiltFrame;
		if (idleFrames > record->idleBuildingUnitsMaxFrames)
			record->idleBuildingUnitsMaxFrames = idleFrames;
		record->lastUnitBuiltFrame = now;
	}
	if (kind.Is("HARVESTER"))
		++record->gatherersBuilt;
	if (kind.Is("HERO"))
		++record->heroesBuilt;
	if (kind.Is("FS_STRATEGY_CENTER"))
		record->hadAStrategyCenter = true;
	if (game.world.Has<engine::gameplay::Tunnel>(entity))
		record->hadATunnelNetwork = true;
	if (kind.Is("MONEY_HACKER") || kind.Is("FS_BLACK_MARKET") || kind.Is("FS_SUPPLY_DROPZONE"))
		++record->secondaryIncomeUnitsBuilt;
	if (kind.Is("FS_BARRACKS") && now <= 300u * 30u)
		record->builtBarracksWithinFiveMinutes = true;
	if (kind.Is("FS_WARFACTORY") && now <= 600u * 30u)
		record->builtWarFactoryWithinTenMinutes = true;
	if (kind.Is("FS_ADVANCED_TECH") && now <= 900u * 30u)
		record->builtTechStructureWithinFifteenMinutes = true;
	if (kind.Is("DISGUISER"))
		++record->disguisableVehiclesBuilt;
}

inline void UpdateAcademy(GameWorld &game, std::uint32_t player)
{
	PlayerAcademy *record = academy_detail::Of(game, player);
	if (record == nullptr || record->unknownSide)
		return;
	const std::uint32_t now = academy_detail::Frame(game);
	if (record->nextUpdateFrame < now)
		return;
	record->nextUpdateFrame = now + AcademyFramesBetweenUpdates;
	if (record->firstUpdate)
	{
		std::vector<ecs::Entity> owned;
		ecs::Query<ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::DefinitionRef>> query(game.world);
		query.ForEachChunk([&](auto chunk) {
			const auto owners = chunk.template Get<engine::gameplay::Owner>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < owners.size(); ++row)
				if (owners[row].player == player)
					owned.push_back(entities[row]);
		});
		for (const ecs::Entity entity : owned)
			RecordAcademyProduction(game, player, entity);
		record = academy_detail::Of(game, player);
	}
	if (record->supplyCentersBuilt == 0 && !record->spentCashBeforeBuildingSupplyCenter)
		if (const auto *money = game.world.FindResource<engine::gameplay::PlayerMoney>())
		{
			const std::uint32_t amount = static_cast<std::uint32_t>(std::max<std::int64_t>(money->Balance(player), 0));
			if (amount < record->supplyCenterCost)
				record->spentCashBeforeBuildingSupplyCenter = true;
		}
	if (const auto *energy = game.world.FindResource<engine::gameplay::PlayerEnergy>())
	{
		const bool hasPower = energy->Sufficient(player);
		if (hasPower != record->hadPowerLastCheck)
		{
			if (!hasPower)
				record->oldestPowerOutFrame = now;
			else
			{
				const std::uint32_t frames = now - record->oldestPowerOutFrame;
				if (frames > record->powerOutMaxFrames)
					record->powerOutMaxFrames = frames;
			}
			record->hadPowerLastCheck = hasPower;
		}
	}
	record->firstUpdate = false;
}

inline void RecordAcademyUpgrade(GameWorld &game, std::uint32_t player, std::uint32_t upgrade, bool granted)
{
	PlayerAcademy *record = academy_detail::Of(game, player);
	const auto &upgrades = game.templates.Content().upgrades.upgrades;
	if (record == nullptr || upgrade >= upgrades.size())
		return;
	if (upgrades[upgrade].academyClassification == 1u) // ACT_UPGRADE_RADAR
		record->researchedRadar = true;
	if (!granted)
		++record->upgradesPurchased;
}

inline void RecordAcademyPowerUsed(GameWorld &game, std::uint32_t player, std::uint32_t power)
{
	PlayerAcademy *record = academy_detail::Of(game, player);
	const auto &templates = game.templates.Content().powers.templates;
	if (record != nullptr && power < templates.size() && templates[power].academyClassification == 2u) // ACT_SUPERPOWER
		++record->specialPowersUsed;
}

inline void RecordAcademyIncomes(GameWorld &game)
{
	auto *money = game.world.FindResource<engine::gameplay::PlayerMoney>();
	if (money == nullptr)
		return;
	const std::uint32_t now = academy_detail::Frame(game);
	for (const std::uint32_t player : money->Incomes())
		if (PlayerAcademy *record = academy_detail::Of(game, player))
		{
			const std::uint32_t delta = record->lastIncomeFrame - now;
			if (delta > record->maxFramesBetweenIncome)
				record->maxFramesBetweenIncome = delta;
			record->lastIncomeFrame = now;
		}
	money->ClearIncomes();
}

// A disguiser taking its look this tick (StealthUpdate::changeVisualDisguise: recordVehicleDisguised) counts for its
// player.
inline void RecordAcademyDisguises(GameWorld &game)
{
	namespace gp = engine::gameplay;
	const auto *events = game.world.FindResource<gp::DisguiseEvents>();
	if (events == nullptr)
		return;
	std::vector<std::uint32_t> players;
	events->ForEach([&](const gp::DisguiseEvent &event) {
		if (event.disguised == 0 || !game.world.IsAlive(event.entity))
			return;
		if (const auto *owner = game.world.Get<gp::Owner>(event.entity))
			players.push_back(owner->player);
	});
	for (const std::uint32_t player : players)
		if (PlayerAcademy *record = academy_detail::Of(game, player))
			++record->vehiclesDisguised;
}

// The counters, each where the original records it.
enum class AcademyCount : std::uint8_t
{
	BuildingCapture,          // SpecialAbilityUpdate's capture done
	GeneralsPointsSpent,      // Player::attemptToPurchaseScience (by the cost)
	BuildingGarrisoned,       // GarrisonContain::onContaining
	BattlePlanSelected,       // BattlePlanUpdate::initiateIntentToDoSpecialPower
	UnitEnteredTunnelNetwork, // TunnelContain::onContaining
	ControlGroupsUsed,        // Player::processSelectTeamGameMessage, a squad with someone alive
	ClearedGarrisonedBuilding, // DumbProjectileBehavior's garrison hit; ActiveBody's DAMAGE_KILL_GARRISONED (each victim's player)
	VehicleDisguised,         // StealthUpdate's disguise taken
	FirestormCreated,         // FirestormDynamicGeometryInfoUpdate's effects fired
	SalvageCollected,         // SalvageCrateCollide::onCollide
	MineCleared,              // Weapon's disarm of a mine or trap
	VehicleSniped,            // ActiveBody's DAMAGE_KILLPILOT (the neutral player's)
	Mine,                     // Object's creation of a MINE, BOOBY_TRAP or DEMOTRAP (the neutral player's)
};

inline void RecordAcademy(GameWorld &game, std::uint32_t player, AcademyCount count, std::uint32_t amount = 1)
{
	PlayerAcademy *record = academy_detail::Of(game, player);
	if (record == nullptr)
		return;
	switch (count)
	{
	case AcademyCount::BuildingCapture: record->structuresCaptured += amount; break;
	case AcademyCount::GeneralsPointsSpent: record->generalsPointsSpent += amount; break;
	case AcademyCount::BuildingGarrisoned: record->structuresGarrisoned += amount; break;
	case AcademyCount::BattlePlanSelected: record->choseAStrategyForCenter = true; break;
	case AcademyCount::UnitEnteredTunnelNetwork: record->unitsEnteredTunnelNetwork += amount; break;
	case AcademyCount::ControlGroupsUsed: record->controlGroupsUsed += amount; break;
	case AcademyCount::ClearedGarrisonedBuilding: record->clearedGarrisonedBuildings += amount; break;
	case AcademyCount::VehicleDisguised: record->vehiclesDisguised += amount; break;
	case AcademyCount::FirestormCreated: record->firestormsCreated += amount; break;
	case AcademyCount::SalvageCollected: record->salvageCollected += amount; break;
	case AcademyCount::MineCleared: record->minesCleared += amount; break;
	case AcademyCount::VehicleSniped: record->vehiclesSniped += amount; break;
	case AcademyCount::Mine: record->mines += amount; break;
	}
}

// The tick's boardings (OpenContain's onContaining, in order): a rider into a garrisoned structure (GarrisonContain:
// recordBuildingGarrisoned) or a tunnel network (TunnelContain: recordUnitEnteredTunnelNetwork) counts for the rider's
// player.
inline void RecordAcademyCargo(GameWorld &game)
{
	namespace gp = engine::gameplay;
	for (const gp::CargoChange &change : game.manifest.Changes())
	{
		if (!change.entered || !game.world.IsAlive(change.container) || !game.world.IsAlive(change.rider))
			continue;
		const auto *owner = game.world.Get<gp::Owner>(change.rider);
		if (owner == nullptr)
			continue;
		if (game.world.Has<gp::Garrison>(change.container))
			RecordAcademy(game, owner->player, AcademyCount::BuildingGarrisoned);
		else if (game.world.Has<gp::Tunnel>(change.container))
			RecordAcademy(game, owner->player, AcademyCount::UnitEnteredTunnelNetwork);
	}
}

// The tick's garrison clearings (recordClearedGarrisonedBuilding): a projectile that killed anyone inside
// (DumbProjectileBehavior's GarrisonHitKillCount: once, for the projectile's player; a projectile's kills are the
// consecutive ones of one building and source) and each rider DAMAGE_KILL_GARRISONED killed (ActiveBody: once each, for
// the victim's own player, as the original records it).
inline void RecordAcademyGarrisonClears(GameWorld &game)
{
	namespace gp = engine::gameplay;
	if (const auto *clears = game.world.FindResource<gp::GarrisonClears>())
	{
		const gp::GarrisonKill *last = nullptr;
		for (const gp::GarrisonKill &kill : clears->kills)
		{
			if (last == nullptr || last->building != kill.building || last->source != kill.source)
				RecordAcademy(game, kill.sourcePlayer, AcademyCount::ClearedGarrisonedBuilding);
			last = &kill;
		}
	}
	if (const auto *victims = game.world.FindResource<gp::GarrisonKillVictims>())
	{
		for (const ecs::Entity victim : victims->list)
		{
			std::optional<std::uint32_t> player;
			if (const auto *owner = game.world.IsAlive(victim) ? game.world.Get<gp::Owner>(victim) : nullptr)
				player = owner->player;
			else
				for (const gp::Casualty &casualty : game.casualties.list)
					if (casualty.entity == victim && casualty.team < game.roster.TeamCount())
						player = game.roster.TeamAt(casualty.team).owner;
			if (player)
				RecordAcademy(game, *player, AcademyCount::ClearedGarrisonedBuilding);
		}
	}
}

// Squad::getLiveObjects is not empty: a member that still exists and is selectable (Object::isSelectable:
// ALWAYS_SELECTABLE, or SELECTABLE (a script's say in its place) without OBJECT_STATUS_UNSELECTABLE, not being sold and
// not effectively dead).
inline bool SquadHasLiveMember(const GameWorld &game, std::span<const ecs::Entity> members)
{
	namespace gp = engine::gameplay;
	static constexpr std::uint64_t unselectable = std::uint64_t{1} << content::ObjectStatusBit("UNSELECTABLE");
	for (const ecs::Entity member : members)
	{
		const auto *ref = game.world.IsAlive(member) ? game.world.Get<gp::DefinitionRef>(member) : nullptr;
		if (ref == nullptr)
			continue;
		const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
		if (kind.Is("ALWAYS_SELECTABLE"))
			return true;
		const auto *script = game.world.Get<gp::ScriptStatus>(member);
		const bool selectable = script != nullptr && script->Has(gp::script_status::SelectableSet) ? script->Has(gp::script_status::SelectableValue)
																								   : kind.Is("SELECTABLE");
		const auto *status = game.world.Get<gp::StatusFlags>(member);
		const auto *health = game.world.Get<gp::Health>(member);
		if (selectable && (status == nullptr || (status->bits & unselectable) == 0) && !game.world.Has<gp::Sale>(member) &&
			!game.world.Has<gp::Dying>(member) && (health == nullptr || !gp::IsDead(*health)))
			return true;
	}
	return false;
}

// ThePlayerList->getNeutralPlayer(): the player that owns no seat (the roster's neutral, "team"'s player).
inline std::optional<std::uint32_t> NeutralPlayer(const GameWorld &game)
{
	for (std::uint32_t player = 0; player < game.roster.PlayerCount(); ++player)
		if (game.roster.PlayerAt(player).name.empty())
			return player;
	return std::nullopt;
}
}
