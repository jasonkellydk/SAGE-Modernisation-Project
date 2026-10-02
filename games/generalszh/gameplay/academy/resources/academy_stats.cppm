export module games.generalszh.gameplay.academy.resources.academy_stats;
import std;

export import engine.core.serialization.byte_stream;
import engine.ecs.system.system;

// Each player's academy record (the original's AcademyStats, one per Player: Core/GameEngine/Source/Common/RTS/
// AcademyStats.cpp), what the skirmish score screen's war school advice is drawn from. Frames are the logic's (the
// tick), kept as the original's UnsignedInt (its wrapping arithmetic included). What only the local machine records
// there (drag selections, double-click attack moves) is not simulation state and is kept by the presentation
// (AcademyClientRecords). Simulation state: checkpointed and hashed.
export namespace generalszh::gameplay
{
struct PlayerAcademy
{
	std::uint32_t nextUpdateFrame{0};
	bool firstUpdate{true};
	bool unknownSide{true}; // no USA, China or GLA base side: no advice, no polling
	// 2) money run out before a supply center.
	bool spentCashBeforeBuildingSupplyCenter{false};
	std::uint32_t supplyCentersBuilt{0};
	std::uint32_t supplyCenterCost{1000};
	// 3) radar.
	bool researchedRadar{false};
	// 4)..8)
	std::uint32_t peonsBuilt{0};
	std::uint32_t structuresCaptured{0};
	std::uint32_t generalsPointsSpent{0};
	std::uint32_t specialPowersUsed{0};
	std::uint32_t structuresGarrisoned{0};
	// 9) idle in building military units.
	std::uint32_t idleBuildingUnitsMaxFrames{0};
	std::uint32_t lastUnitBuiltFrame{0};
	// 11) upgrades.
	std::uint32_t upgradesPurchased{0};
	// 12) power out.
	std::uint32_t powerOutMaxFrames{0};
	std::uint32_t oldestPowerOutFrame{0};
	bool hadPowerLastCheck{false};
	// 13), 14)
	std::uint32_t gatherersBuilt{0};
	std::uint32_t heroesBuilt{0};
	// 15)..21)
	bool hadAStrategyCenter{false};
	bool choseAStrategyForCenter{false};
	std::uint32_t unitsEnteredTunnelNetwork{0};
	bool hadATunnelNetwork{false};
	std::uint32_t controlGroupsUsed{0};
	std::uint32_t secondaryIncomeUnitsBuilt{0};
	std::uint32_t clearedGarrisonedBuildings{0};
	std::uint32_t salvageCollected{0};
	std::uint32_t guardAbilityUsedCount{0};
	// 27)..29)
	bool builtBarracksWithinFiveMinutes{false};
	bool builtWarFactoryWithinTenMinutes{false};
	bool builtTechStructureWithinFifteenMinutes{false};
	// 30) income.
	std::uint32_t lastIncomeFrame{0};
	std::uint32_t maxFramesBetweenIncome{0};
	// 31)..35)
	std::uint32_t mines{0}; // the neutral player's
	std::uint32_t minesCleared{0};
	std::uint32_t vehiclesRecovered{0};
	std::uint32_t vehiclesSniped{0}; // the neutral player's
	std::uint32_t disguisableVehiclesBuilt{0};
	std::uint32_t vehiclesDisguised{0};
	std::uint32_t firestormsCreated{0};
};

struct AcademyStats
{
	std::vector<PlayerAcademy> players;

	PlayerAcademy &Of(std::uint32_t player)
	{
		if (player >= players.size())
			players.resize(player + 1);
		return players[player];
	}
	const PlayerAcademy *Find(std::uint32_t player) const noexcept { return player < players.size() ? &players[player] : nullptr; }

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(players.size()));
		for (const PlayerAcademy &record : players)
		{
			for (const bool flag : {record.firstUpdate, record.unknownSide, record.spentCashBeforeBuildingSupplyCenter, record.researchedRadar,
					 record.hadPowerLastCheck, record.hadAStrategyCenter, record.choseAStrategyForCenter, record.hadATunnelNetwork,
					 record.builtBarracksWithinFiveMinutes, record.builtWarFactoryWithinTenMinutes, record.builtTechStructureWithinFifteenMinutes})
				writer.Flag(flag);
			for (const std::uint32_t value : {record.nextUpdateFrame, record.supplyCentersBuilt, record.supplyCenterCost, record.peonsBuilt,
					 record.structuresCaptured, record.generalsPointsSpent, record.specialPowersUsed, record.structuresGarrisoned,
					 record.idleBuildingUnitsMaxFrames, record.lastUnitBuiltFrame, record.upgradesPurchased, record.powerOutMaxFrames,
					 record.oldestPowerOutFrame, record.gatherersBuilt, record.heroesBuilt, record.unitsEnteredTunnelNetwork, record.controlGroupsUsed,
					 record.secondaryIncomeUnitsBuilt, record.clearedGarrisonedBuildings, record.salvageCollected, record.guardAbilityUsedCount,
					 record.lastIncomeFrame, record.maxFramesBetweenIncome, record.mines, record.minesCleared, record.vehiclesRecovered,
					 record.vehiclesSniped, record.disguisableVehiclesBuilt, record.vehiclesDisguised, record.firestormsCreated})
				writer.U32(value);
		}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto count = reader.U32();
		if (!count || *count > 1024)
			return false;
		std::vector<PlayerAcademy> loaded(*count);
		for (PlayerAcademy &record : loaded)
		{
			for (bool *flag : {&record.firstUpdate, &record.unknownSide, &record.spentCashBeforeBuildingSupplyCenter, &record.researchedRadar,
					 &record.hadPowerLastCheck, &record.hadAStrategyCenter, &record.choseAStrategyForCenter, &record.hadATunnelNetwork,
					 &record.builtBarracksWithinFiveMinutes, &record.builtWarFactoryWithinTenMinutes, &record.builtTechStructureWithinFifteenMinutes})
				*flag = reader.Flag().value_or(false);
			for (std::uint32_t *value : {&record.nextUpdateFrame, &record.supplyCentersBuilt, &record.supplyCenterCost, &record.peonsBuilt,
					 &record.structuresCaptured, &record.generalsPointsSpent, &record.specialPowersUsed, &record.structuresGarrisoned,
					 &record.idleBuildingUnitsMaxFrames, &record.lastUnitBuiltFrame, &record.upgradesPurchased, &record.powerOutMaxFrames,
					 &record.oldestPowerOutFrame, &record.gatherersBuilt, &record.heroesBuilt, &record.unitsEnteredTunnelNetwork, &record.controlGroupsUsed,
					 &record.secondaryIncomeUnitsBuilt, &record.clearedGarrisonedBuildings, &record.salvageCollected, &record.guardAbilityUsedCount,
					 &record.lastIncomeFrame, &record.maxFramesBetweenIncome, &record.mines, &record.minesCleared, &record.vehiclesRecovered,
					 &record.vehiclesSniped, &record.disguisableVehiclesBuilt, &record.vehiclesDisguised, &record.firestormsCreated})
				*value = reader.U32().value_or(0);
		}
		if (reader.Failed())
			return false;
		players = std::move(loaded);
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::AcademyStats>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.academy_stats";
};
}
