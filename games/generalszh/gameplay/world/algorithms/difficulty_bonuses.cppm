export module games.generalszh.gameplay.world.algorithms.difficulty_bonuses;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.world.resources.solo_play;
export import games.generalszh.gameplay.world.components.difficulty_bonus;
import games.generalszh.gameplay.ai.resources.ai_players;
import games.generalszh.content.combat.weapon_bonus_content;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.health.algorithms.max_health;
import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
import engine.ecs.query.query;

// A single-player game's difficulty bonus on objects:
// - SetReceivingDifficultyBonus (Object::setReceivingDifficultyBonus): a change applies or takes back its player's bonus
//   (Player::friend_applyDifficultyBonusesForObject): in a single-player game only, its maximum health times (or divided
//   by) GameData's solo health bonus for the player's type and difficulty (PRESERVE_RATIO; 100% changes nothing), and
//   that player's SOLO_* weapon bonus condition set (or cleared). A player's difficulty is its AI's, else the game's.
// - ObjectCreated (Object::initObject): an object made while bonuses are allowed gets it.
// - AllowDifficultyBonuses (ScriptActions::doEnableOrDisableObjectDifficultyBonuses, OBJECT_ALLOW_BONUSES): every object,
//   and the rule for objects made from now on.
export namespace generalszh::gameplay
{
namespace difficulty_detail
{
inline std::optional<std::pair<std::size_t, std::size_t>> TypeAndDifficulty(GameWorld &game, ecs::Entity entity)
{
	const auto *owner = game.world.Get<engine::gameplay::Owner>(entity);
	if (owner == nullptr || owner->player >= game.roster.PlayerCount())
		return std::nullopt;
	const std::size_t type = game.roster.PlayerAt(owner->player).human ? 0 : 1; // PLAYER_HUMAN, PLAYER_COMPUTER
	std::size_t difficulty = 1;
	if (const auto *solo = game.world.FindResource<SoloPlay>())
		difficulty = solo->difficulty;
	if (const auto *ais = game.world.FindResource<AiPlayers>())
		if (const AiPlayer *ai = ais->Of(owner->player))
			difficulty = ai->difficulty;
	return std::pair{type, std::min<std::size_t>(difficulty, 2)};
}
}

inline void SetReceivingDifficultyBonus(GameWorld &game, ecs::Entity entity, bool receive)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.IsAlive(entity))
		return;
	const bool now = world.Has<DifficultyBonus>(entity) && world.Get<DifficultyBonus>(entity)->receiving != 0;
	if (now == receive)
		return;
	if (!world.Has<DifficultyBonus>(entity))
		world.Add<DifficultyBonus>(entity);
	world.Get<DifficultyBonus>(entity)->receiving = receive ? 1 : 0;
	const auto *solo = world.FindResource<SoloPlay>();
	if (solo == nullptr || !solo->singlePlayer)
		return;
	const auto which = difficulty_detail::TypeAndDifficulty(game, entity);
	if (!which)
		return;
	const auto [type, difficulty] = *which;
	const Engine::Math::Fixed factor = game.templates.Content().gameData.soloHealthBonus[type][difficulty];
	if (factor != Engine::Math::Fixed::One() && factor > Engine::Math::Fixed{})
		if (auto *health = world.Get<gp::Health>(entity))
			gp::SetMaxHealth(*health, receive ? health->maximum * factor : health->maximum / factor, gp::MaxHealthChange::PreserveRatio);
	namespace bonus = content::weapon_bonus;
	constexpr std::uint32_t bits[2][3] = {{bonus::SoloHumanEasy, bonus::SoloHumanNormal, bonus::SoloHumanHard},
		{bonus::SoloAiEasy, bonus::SoloAiNormal, bonus::SoloAiHard}};
	if (!world.Has<gp::WeaponBonusConditions>(entity))
		world.Add<gp::WeaponBonusConditions>(entity);
	gp::SetWeaponBonus(*world.Get<gp::WeaponBonusConditions>(entity), bits[type][difficulty], receive, game.tick);
}

inline void ObjectCreated(GameWorld &game, ecs::Entity entity)
{
	const auto *solo = game.world.FindResource<SoloPlay>();
	if (solo != nullptr && solo->bonuses)
		SetReceivingDifficultyBonus(game, entity, true);
}

inline void AllowDifficultyBonuses(GameWorld &game, bool allow)
{
	std::vector<ecs::Entity> objects;
	ecs::Query<ecs::Read<engine::gameplay::Owner>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		for (const ecs::Entity entity : chunk.Entities())
			objects.push_back(entity);
	});
	for (const ecs::Entity entity : objects)
		SetReceivingDifficultyBonus(game, entity, allow);
	if (auto *solo = game.world.FindResource<SoloPlay>())
		solo->bonuses = allow;
}
}
