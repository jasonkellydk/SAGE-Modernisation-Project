export module games.generalszh.gameplay.sciences.algorithms.general_ranks;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import engine.gameplay.rts.sciences.algorithms.ranks;
import games.generalszh.gameplay.scripts.resources.script_records;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import games.generalszh.gameplay.eva.resources.eva_notices;

// A player's rank as a general and what it knows (Player: addScience, grantScience, resetSciences, addSkillPoints,
// setRankLevel, attemptToPurchaseScience), over the world's PlayerRanks, RankRules and PlayerSciences. A science
// learnt wakes the player's special powers that need it and is told to the scripts (notifyOfAcquiredScience).
export namespace generalszh::gameplay
{
// Player::addScience.
inline void AddScience(GameWorld &game, std::uint32_t player, std::uint32_t science)
{
	auto &known = game.world.Resource<engine::gameplay::PlayerSciences>();
	if (known.Has(player, science))
		return;
	known.Grant(player, science);
	// 'wake up' any special powers controlled by it: on, and instantly ready.
	WakePowersForScience(game, player, science);
	const auto &names = game.templates.Content().sciences;
	if (auto *records = game.world.FindResource<ScriptRecords>(); records != nullptr && science < names.size())
		records->AcquiredScience(player, names[science]);
}

// Player::grantScience: only a grantable one.
inline bool GrantScience(GameWorld &game, std::uint32_t player, std::uint32_t science)
{
	const engine::gameplay::ScienceRule *rule = game.world.Resource<engine::gameplay::RankRules>().Science(science);
	if (rule == nullptr || !rule->grantable)
		return false;
	const bool had = game.world.Resource<engine::gameplay::PlayerSciences>().Has(player, science);
	AddScience(game, player, science);
	return !had;
}

// Player::resetSciences: nothing known but its side's sciences and each rank's up to its own.
inline void ResetSciences(GameWorld &game, std::uint32_t player)
{
	game.world.Resource<engine::gameplay::PlayerSciences>().Clear(player);
	const engine::gameplay::PlayerRank &rank = game.world.Resource<engine::gameplay::PlayerRanks>().Of(player);
	for (const std::uint32_t science : rank.intrinsicSciences)
		AddScience(game, player, science);
	const auto &rules = game.world.Resource<engine::gameplay::RankRules>();
	for (std::int32_t level = 1; level <= rank.level; ++level)
		if (const engine::gameplay::RankLevel *info = rules.Rank(level))
			for (const std::uint32_t science : info->sciencesGranted)
				AddScience(game, player, science);
}

// Player::resetRank at a player's start: rank 1 with its purchase points, and its sciences.
inline void StartRank(GameWorld &game, std::uint32_t player)
{
	engine::gameplay::PlayerRank &rank = game.world.Resource<engine::gameplay::PlayerRanks>().Of(player);
	engine::gameplay::ResetRank(rank, game.world.Resource<engine::gameplay::RankRules>(), rank.intrinsicPurchasePoints);
	ResetSciences(game, player);
}

// Player::addSkillPoints: promotions grant their sciences.
inline bool AddSkillPoints(GameWorld &game, std::uint32_t player, std::int32_t points)
{
	if (player >= game.roster.PlayerCount())
		return false;
	std::vector<std::uint32_t> granted;
	engine::gameplay::PlayerRank &rank = game.world.Resource<engine::gameplay::PlayerRanks>().Of(player);
	const bool gained = engine::gameplay::AddSkillPoints(rank, game.world.Resource<engine::gameplay::RankRules>(),
		static_cast<std::int32_t>(game.roster.PlayerAt(player).rankLimit), rank.intrinsicPurchasePoints, points, granted);
	for (const std::uint32_t science : granted)
		AddScience(game, player, science);
	// setRankLevel: EVA tells the general.
	if (gained)
		if (auto *eva = game.world.FindResource<EvaNotices>())
			eva->list.push_back({EvaCue::GeneralLevelUp, EvaWeapon::None, player});
	return gained;
}

// Player::setRankLevel: a lower rank starts over (its sciences too).
inline bool SetRankLevel(GameWorld &game, std::uint32_t player, std::int32_t level)
{
	if (player >= game.roster.PlayerCount())
		return false;
	std::vector<std::uint32_t> granted;
	bool reset = false;
	engine::gameplay::PlayerRank &rank = game.world.Resource<engine::gameplay::PlayerRanks>().Of(player);
	const bool changed = engine::gameplay::SetRankLevel(rank, game.world.Resource<engine::gameplay::RankRules>(),
		static_cast<std::int32_t>(game.roster.PlayerAt(player).rankLimit), rank.intrinsicPurchasePoints, level, granted, reset);
	if (reset)
		ResetSciences(game, player);
	for (const std::uint32_t science : granted)
		AddScience(game, player, science);
	if (changed)
		if (auto *eva = game.world.FindResource<EvaNotices>())
			eva->list.push_back({EvaCue::GeneralLevelUp, EvaWeapon::None, player});
	return changed;
}

// Player::attemptToPurchaseScience.
inline bool PurchaseScience(GameWorld &game, std::uint32_t player, std::uint32_t science)
{
	auto &ranks = game.world.Resource<engine::gameplay::PlayerRanks>();
	if (!engine::gameplay::PurchaseScience(ranks.Of(player), game.world.Resource<engine::gameplay::PlayerSciences>(),
			game.world.Resource<engine::gameplay::RankRules>(), player, science))
		return false;
	AddScience(game, player, science);
	return true;
}
}
