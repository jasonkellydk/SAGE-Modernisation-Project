export module games.generalszh.shell.score.academy_advice;
import std;

// The skirmish score screen's war school advice (the original's AcademyStats::calculateAcademyAdvice,
// Core/GameEngine/Source/Common/RTS/AcademyStats.cpp, over the local player's record): MAX_ADVICE_TIPS (1) tips, each
// "\n\n" and its label's text. Tier 1's basic advice first, then (still short of tips) tier 2's, then tier 3's. Each
// tier counts the tips it has on offer (evaluateTierNAdvice with -1: the draws GameClientRandomValue(0, -2 and less)
// take nothing, lo >= hi giving hi), then, with any on offer, goes through them again choosing: each on offer draws
// GameClientRandomValue(0, left - 1) and is taken when that is under the tips still wanted (the last on offer always
// is). Its quirks are the original's: it updates the record's longest idle time, power outage and income gap to now
// as it looks (the income gap `last - now` unsigned, so any income but at frame 0 counts as a gap over 2 minutes);
// advice 31 is the local player's own mines (only the neutral player's are counted: never any) under the label
// "ACADEMY:NoIncome"; the guard ability is never recorded used, nor a sniped vehicle recovered.
export namespace generalszh::shell
{
inline constexpr std::uint32_t MaxAdviceTips = 1;         // MAX_ADVICE_TIPS
inline constexpr std::uint32_t AdviceFramesPerSecond = 30; // LOGICFRAMES_PER_SECOND

// What the advice reads of a player's AcademyStats (the simulation's, with the local machine's own two).
struct AcademyAdviceRecord
{
	bool unknownSide{true};
	bool spentCashBeforeBuildingSupplyCenter{false};
	std::uint32_t supplyCentersBuilt{0};
	bool researchedRadar{false};
	std::uint32_t peonsBuilt{0}, structuresCaptured{0}, generalsPointsSpent{0}, specialPowersUsed{0}, structuresGarrisoned{0};
	std::uint32_t idleBuildingUnitsMaxFrames{0}, lastUnitBuiltFrame{0};
	std::uint32_t dragSelectUnits{0}; // the local machine's
	std::uint32_t upgradesPurchased{0};
	std::uint32_t powerOutMaxFrames{0}, oldestPowerOutFrame{0};
	bool hadPowerLastCheck{false};
	std::uint32_t gatherersBuilt{0}, heroesBuilt{0};
	bool hadAStrategyCenter{false}, choseAStrategyForCenter{false};
	std::uint32_t unitsEnteredTunnelNetwork{0};
	bool hadATunnelNetwork{false};
	std::uint32_t controlGroupsUsed{0}, secondaryIncomeUnitsBuilt{0}, clearedGarrisonedBuildings{0}, salvageCollected{0}, guardAbilityUsedCount{0};
	std::uint32_t doubleClickAttackMoveOrdersGiven{0}; // the local machine's
	bool builtBarracksWithinFiveMinutes{false}, builtWarFactoryWithinTenMinutes{false}, builtTechStructureWithinFifteenMinutes{false};
	std::uint32_t lastIncomeFrame{0}, maxFramesBetweenIncome{0};
	std::uint32_t minesCleared{0}, vehiclesRecovered{0}, disguisableVehiclesBuilt{0}, vehiclesDisguised{0}, firestormsCreated{0};
};

struct AcademyAdviceContext
{
	std::uint32_t now{0};                 // TheGameLogic->getFrame() as the game ended
	std::string baseSide;                 // the player's PlayerTemplate's base side
	bool useAlternateMouse{false};        // TheGlobalData->m_useAlternateMouse (the options' alternate mouse)
	std::uint32_t localPlayerMines{0};    // ThePlayerList->getLocalPlayer()'s getMines()
	std::uint32_t neutralVehiclesSniped{0}; // ThePlayerList->getNeutralPlayer()'s getVehiclesSniped()
};

namespace academy_advice_detail
{
inline bool SameText(std::string_view a, std::string_view b) noexcept
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}

struct Tip
{
	bool offered;
	std::string_view label;
};

// GetGameClientRandomValue(lo, hi): hi when lo >= hi (no draw), else the next draw modulo the span, from lo.
inline std::int32_t ClientRandomValue(const std::function<std::uint32_t()> &draw, std::int32_t lo, std::int32_t hi)
{
	if (lo >= hi)
		return hi;
	const std::uint32_t delta = static_cast<std::uint32_t>(hi - lo + 1);
	return static_cast<std::int32_t>(draw() % delta) + lo;
}

// evaluateTierNAdvice(info, numAvailableTips): counting (-1), then choosing among those counted.
inline void Evaluate(std::span<const Tip> tips, std::vector<std::string> &advice, const std::function<std::uint32_t()> &draw, std::int32_t numAvailableTips = -1)
{
	const bool choosing = numAvailableTips != -1;
	std::int32_t availableTips = 0;
	for (const Tip &tip : tips)
	{
		if (!tip.offered)
			continue;
		++availableTips;
		const std::int32_t roll = ClientRandomValue(draw, 0, numAvailableTips - 1);
		const std::int32_t limit = static_cast<std::int32_t>(MaxAdviceTips) - static_cast<std::int32_t>(advice.size());
		if (choosing && roll < limit)
			advice.emplace_back(tip.label);
		--numAvailableTips;
	}
	if (!choosing && availableTips > 0)
		Evaluate(tips, advice, draw, availableTips);
}
}

// calculateAcademyAdvice: the labels of the tips given (none: no advice; an unknown side gives none). `draw` is the
// client random stream's next value (randomValue(theGameClientSeed)).
inline std::vector<std::string> CalculateAcademyAdvice(AcademyAdviceRecord record, const AcademyAdviceContext &context,
	const std::function<std::uint32_t()> &draw)
{
	using academy_advice_detail::Tip;
	std::vector<std::string> advice;
	if (record.unknownSide)
		return advice;
	const std::uint32_t now = context.now;
	// Tier 1. 9) and 12) bring the longest idle time and outage up to now.
	const std::uint32_t idleFrames = now - record.lastUnitBuiltFrame;
	if (idleFrames > record.idleBuildingUnitsMaxFrames)
		record.idleBuildingUnitsMaxFrames = idleFrames;
	if (!record.hadPowerLastCheck)
	{
		const std::uint32_t frames = now - record.oldestPowerOutFrame;
		if (frames > record.powerOutMaxFrames)
			record.powerOutMaxFrames = frames;
	}
	const std::array tier1{
		Tip{record.spentCashBeforeBuildingSupplyCenter, "ACADEMY:BuildSupplyCenterEarlier"},
		Tip{!record.researchedRadar, "ACADEMY:TryBuildingRadar"},
		Tip{record.peonsBuilt < 2, "ACADEMY:BuildMorePeons"},
		Tip{record.structuresCaptured == 0, "ACADEMY:TryCapturingStructures"},
		Tip{record.generalsPointsSpent == 0, "ACADEMY:SpendGeneralsPoints"},
		Tip{record.specialPowersUsed == 0, "ACADEMY:TryUsingSuperweapons"},
		Tip{record.structuresGarrisoned == 0, "ACADEMY:TryGarrisoningAStructure"},
		Tip{record.idleBuildingUnitsMaxFrames > 300 * AdviceFramesPerSecond || record.lastUnitBuiltFrame == 0, "ACADEMY:IdleBuildingUnits"},
		Tip{record.dragSelectUnits == 0, "ACADEMY:TryDragSelectingUnits"},
		Tip{record.upgradesPurchased == 0, "ACADEMY:ResearchUpgrades"},
		Tip{record.powerOutMaxFrames > 600 * AdviceFramesPerSecond, "ACADEMY:RanOutOfPower"},
		Tip{record.gatherersBuilt == 0, "ACADEMY:BuildMoreGatherers"},
		Tip{record.heroesBuilt == 0, "ACADEMY:BuildAHero"},
	};
	academy_advice_detail::Evaluate(tier1, advice, draw);
	if (advice.size() >= MaxAdviceTips)
		return advice;
	const bool gla = academy_advice_detail::SameText(context.baseSide, "GLA");
	const std::array tier2{
		Tip{record.hadAStrategyCenter && !record.choseAStrategyForCenter, "ACADEMY:PickStrategyCenterPlan"},
		Tip{record.hadATunnelNetwork && record.unitsEnteredTunnelNetwork == 0, "ACADEMY:UseTunnelNetwork"},
		Tip{record.controlGroupsUsed == 0, "ACADEMY:UseControlGroups"},
		Tip{record.secondaryIncomeUnitsBuilt == 0, "ACADEMY:UseSecondaryIncomeMethods"},
		Tip{record.clearedGarrisonedBuildings == 0, "ACADEMY:ClearBuildings"},
		Tip{gla && record.salvageCollected == 0, "ACADEMY:PickUpSalvage"},
		Tip{record.guardAbilityUsedCount == 0, "ACADEMY:UseGuardAbility"},
		Tip{record.supplyCentersBuilt < 2, "ACADEMY:MultipleSupplyCenters"},
	};
	academy_advice_detail::Evaluate(tier2, advice, draw);
	if (advice.size() >= MaxAdviceTips)
		return advice;
	// Tier 3. 30) brings the income gap up to now (unsigned last - now).
	const std::uint32_t delta = record.lastIncomeFrame - now;
	if (delta > record.maxFramesBetweenIncome)
		record.maxFramesBetweenIncome = delta;
	const bool china = academy_advice_detail::SameText(context.baseSide, "China");
	const std::array tier3{
		Tip{!context.useAlternateMouse, "ACADEMY:AlternateMouseInterface"},
		Tip{record.doubleClickAttackMoveOrdersGiven == 0, "ACADEMY:DoubleClickAttackMoveGuard"},
		Tip{!record.builtBarracksWithinFiveMinutes, "ACADEMY:BuildBarracksSooner"},
		Tip{!record.builtWarFactoryWithinTenMinutes, "ACADEMY:BuildWarFactorySooner"},
		Tip{!record.builtTechStructureWithinFifteenMinutes, "ACADEMY:BuildTechStructureSooner"},
		Tip{record.maxFramesBetweenIncome > AdviceFramesPerSecond * 120 || record.lastIncomeFrame == 0, "ACADEMY:NoIncome"},
		Tip{context.localPlayerMines > 0 && record.minesCleared == 0, "ACADEMY:NoIncome"},
		Tip{context.neutralVehiclesSniped > 0 && record.vehiclesRecovered == 0, "ACADEMY:UnmannedVehicles"},
		Tip{record.disguisableVehiclesBuilt != 0 && record.vehiclesDisguised == 0, "ACADEMY:DisguisedUnits"},
		Tip{china && record.firestormsCreated == 0, "ACADEMY:Firestorm"},
	};
	academy_advice_detail::Evaluate(tier3, advice, draw);
	return advice;
}
}
