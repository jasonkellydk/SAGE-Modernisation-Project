export module engine.gameplay.rts.sciences.algorithms.ranks;
import std;

export import engine.gameplay.rts.sciences.definitions.rank_rules;
export import engine.gameplay.rts.sciences.resources.player_ranks;
export import engine.gameplay.rts.sciences.resources.player_sciences;

// A general's promotions and purchases (Player: resetRank, setRankLevel, addSkillPoints, isCapableOfPurchasingScience,
// attemptToPurchaseScience). The sciences a change grants are handed back in order: the caller grants them and tells
// whoever listens (scripts, special powers).
export namespace engine::gameplay
{
// resetRank: rank 1, no skill points, its side's intrinsic purchase points plus rank 1's.
inline void ResetRank(PlayerRank &rank, const RankRules &rules, std::int32_t intrinsicPurchasePoints)
{
	rank.level = 1;
	rank.skillPoints = 0;
	const RankLevel *next = rules.Rank(2);
	rank.levelUp = next != nullptr ? next->skillPointsNeeded : std::numeric_limits<std::int32_t>::max();
	rank.levelDown = 0;
	rank.purchasePoints = intrinsicPurchasePoints;
	if (const RankLevel *current = rules.Rank(1))
		rank.purchasePoints += current->purchasePointsGranted;
}

// setRankLevel: to `level` (kept within 1, the ranks there are, and `limit`); going down resets the rank first (`reset`
// is set: the caller drops the player's sciences and gives back its starting ones). Each rank passed gives its purchase
// points, raises the skill points to its need and grants its sciences. True when the rank changed.
inline bool SetRankLevel(PlayerRank &rank, const RankRules &rules, std::int32_t limit, std::int32_t intrinsicPurchasePoints, std::int32_t level,
	std::vector<std::uint32_t> &granted, bool &reset)
{
	if (level < 1)
		level = 1;
	else if (level > rules.LevelCount())
		level = rules.LevelCount();
	if (level > limit)
		level = limit;
	if (level == rank.level)
		return false;
	if (level < rank.level)
	{
		ResetRank(rank, rules, intrinsicPurchasePoints);
		reset = true;
	}
	for (std::int32_t passed = rank.level + 1; passed <= level; ++passed)
		if (const RankLevel *info = rules.Rank(passed))
		{
			rank.purchasePoints = std::max(rank.purchasePoints + info->purchasePointsGranted, 0);
			rank.skillPoints = std::max(rank.skillPoints, info->skillPointsNeeded);
			granted.insert(granted.end(), info->sciencesGranted.begin(), info->sciencesGranted.end());
			rank.levelDown = info->skillPointsNeeded;
		}
	const RankLevel *next = rules.Rank(level + 1);
	rank.levelUp = next != nullptr ? next->skillPointsNeeded : std::numeric_limits<std::int32_t>::max();
	rank.level = level;
	return true;
}

// addSkillPoints: the gain scaled by the modifier (rounded up), capped at the need of the capping rank (the lower of
// the limit and the last rank: its lowest point, not its highest), ranking up while the points reach the next rank.
// True when a rank was gained.
inline bool AddSkillPoints(PlayerRank &rank, const RankRules &rules, std::int32_t limit, std::int32_t intrinsicPurchasePoints, std::int32_t delta,
	std::vector<std::uint32_t> &granted)
{
	delta = static_cast<std::int32_t>((rank.skillModifier * Engine::Math::Fixed::FromInt(delta)).Ceil());
	if (delta == 0)
		return false;
	const RankLevel *capping = rules.Rank(std::min(limit, rules.LevelCount()));
	const std::int32_t pointCap = capping != nullptr ? capping->skillPointsNeeded : 0;
	rank.skillPoints = std::min(pointCap, rank.skillPoints + delta);
	bool gained = false;
	bool reset = false;
	while (rank.skillPoints >= rank.levelUp)
	{
		if (!SetRankLevel(rank, rules, limit, intrinsicPurchasePoints, rank.level + 1, granted, reset))
			break;
		gained = true;
	}
	return gained;
}

// isCapableOfPurchasingScience: not known yet, not disabled or hidden by a script, its prerequisites known, for sale and
// affordable.
inline bool CanPurchaseScience(const PlayerRank &rank, const PlayerSciences &known, const RankRules &rules, std::uint32_t player, std::uint32_t science)
{
	const ScienceRule *rule = rules.Science(science);
	if (rule == nullptr || known.Has(player, science))
		return false;
	if (known.Disabled(player, science) || known.Hidden(player, science))
		return false;
	for (const std::uint32_t needed : rule->prerequisites)
		if (!known.Has(player, needed))
			return false;
	return rule->purchaseCost != 0 && rule->purchaseCost <= rank.purchasePoints;
}

// attemptToPurchaseScience: paid for with purchase points; true when bought (the caller grants it).
inline bool PurchaseScience(PlayerRank &rank, const PlayerSciences &known, const RankRules &rules, std::uint32_t player, std::uint32_t science)
{
	if (!CanPurchaseScience(rank, known, rules, player, science))
		return false;
	rank.purchasePoints = std::max(rank.purchasePoints - rules.Science(science)->purchaseCost, 0);
	return true;
}
}
