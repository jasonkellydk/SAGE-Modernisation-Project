export module engine.gameplay.rts.sciences.definitions.rank_rules;
import std;

import engine.ecs.system.system;

// What ranks and sciences are worth (RankInfoStore, ScienceStore), by science bit and rank (rank 1 first): the skill
// points a rank needs, the sciences and purchase points it grants; each science's purchase cost (0: not for sale),
// whether it may be granted, and the sciences it needs first. Level data: not checkpointed.
export namespace engine::gameplay
{
struct RankLevel
{
	std::int32_t skillPointsNeeded{0};
	std::int32_t purchasePointsGranted{0};
	std::vector<std::uint32_t> sciencesGranted;
};

struct ScienceRule
{
	std::int32_t purchaseCost{0};
	bool grantable{true};
	std::vector<std::uint32_t> prerequisites;
	// ScienceInfo::m_rootSciences: the sciences without prerequisites it comes down to (itself if it has none).
	std::vector<std::uint32_t> roots;
};

struct RankRules
{
	std::vector<RankLevel> ranks;
	std::vector<ScienceRule> sciences;

	std::int32_t LevelCount() const noexcept { return static_cast<std::int32_t>(ranks.size()); }
	// getRankInfo: 1-based; none outside 1..count.
	const RankLevel *Rank(std::int32_t level) const noexcept
	{
		return level >= 1 && level <= LevelCount() ? &ranks[static_cast<std::size_t>(level - 1)] : nullptr;
	}
	const ScienceRule *Science(std::uint32_t science) const noexcept { return science < sciences.size() ? &sciences[science] : nullptr; }
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::RankRules>
{
	static constexpr std::string_view StableName = "engine.gameplay.rank_rules";
};
}
