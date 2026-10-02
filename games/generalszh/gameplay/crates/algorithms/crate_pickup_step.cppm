export module games.generalszh.gameplay.crates.algorithms.crate_pickup_step;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.crates.algorithms.car_bombs;
import games.generalszh.gameplay.crates.algorithms.crate_rules;
import games.generalszh.gameplay.crates.algorithms.hijacking;
import games.generalszh.gameplay.crates.algorithms.sabotage;
import games.generalszh.gameplay.crates.resources.crates;

// CrateCollide after the tick's systems: crates run into go to the first that may take them; car bombs, hijacks and
// sabotage (their crate collides) take effect.
export namespace generalszh::gameplay
{
namespace gp = engine::gameplay;

// Crates run into this tick go to the first that may take them.
inline void CompleteCratePickups(GameWorld &game)
{
	auto &pickups = game.world.Resource<CratePickups>();
	pickups.list.clear();
	std::vector<CrateTouch> touches;
	game.world.Resource<CrateTouches>().AppendTo(touches);
	if (!touches.empty())
		PickUpCrates(game, touches, pickups);
	// Terrorists at the vehicles they make car bombs (their ConvertToCarBombCrateCollide).
	ApplyCarBombs(game);
	// Hijackers at the vehicles they take (ConvertToHijackedVehicleCrateCollide).
	ApplyHijacks(game);
	// Saboteurs at the buildings they sabotage (their Sabotage*CrateCollides).
	ApplySabotages(game);
}
}
