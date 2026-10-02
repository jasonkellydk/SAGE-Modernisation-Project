export module games.generalszh.gameplay.teams.algorithms.defection;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.construction.algorithms.selling;
import games.generalszh.gameplay.eva.resources.eva_notices;
import games.generalszh.gameplay.abilities.resources.ability_notices;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.targetable;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.stealth.components.undetected_defector;
import engine.gameplay.rts.aircraft.components.jet;
import engine.gameplay.rts.mines.components.minefield;
import engine.ecs.query.query;

// Object::defect: something goes over to another team, as a capture or a defector does.
export namespace generalszh::gameplay
{
// Object::defect(newTeam, detectionTime): nothing while it is aboard something, when `team` is its own player's default
// team, or while it is being built or sold. Else whatever it was producing is refunded (cancelAndRefundAllProduction);
// its player hears of an infiltration there (Radar::tryInfiltrationEvent, when both players are playable sides); it is an
// undetected defector for `detectionTicks` (none when 0); it joins the team (setTeam) and its AI idles; a container
// that kicks out on capture (all but tunnels and caves) puts everyone out; the jets parked at it (on the ground, or
// taking off or landing) defect with it, while those in the air lose their space if the new side is another player's
// (ParkingPlaceBehavior::defectAllParkedUnits); and the mines it made join the team (setTeam).
inline void Defect(GameWorld &game, ecs::Entity unit, std::uint32_t team, std::uint64_t detectionTicks)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	if (!world.IsAlive(unit) || world.Has<gp::Passenger>(unit) || team >= game.roster.TeamCount())
		return;
	const auto *owner = world.Get<gp::Owner>(unit);
	if (owner == nullptr || owner->player >= game.roster.PlayerCount())
		return;
	const std::uint32_t from = owner->player;
	if (game.roster.DefaultTeam(from) == team)
		return;
	if (world.Has<gp::UnderConstruction>(unit) || world.Has<gp::Sale>(unit))
		return;
	selling_detail::RefundProduction(game, unit);
	const std::uint32_t to = game.roster.TeamAt(team).owner;
	if (to < game.roster.PlayerCount() && game.roster.PlayerAt(to).playable && game.roster.PlayerAt(from).playable)
		if (auto *notices = world.FindResource<InfiltrationNotices>())
			if (const auto *at = world.Get<gp::Transform>(unit))
				notices->list.push_back({from, 0u, at->position});
	// friend_setUndetectedDefector / ObjectDefectionHelper::startDefectionTimer.
	if (detectionTicks > 0)
	{
		if (!world.Has<gp::UndetectedDefector>(unit))
			world.Add<gp::UndetectedDefector>(unit);
		world.Get<gp::UndetectedDefector>(unit)->until = game.tick + detectionTicks;
		world.Get<gp::UndetectedDefector>(unit)->fx = 1;
		if (auto *targetable = world.Get<gp::Targetable>(unit))
			targetable->classes |= gp::target_class::Undetected;
	}
	ChangeTeam(game, unit, team);
	if (HasAi(game, unit))
		AiIdle(game, unit);
	// Its defect voice, the flash and the timer's tick (the presentation's).
	if (auto *notices = world.FindResource<AbilityNotices>())
		notices->defected.push_back(unit);
	// isKickOutOnCapture: TunnelContain and CaveContain keep theirs.
	if (world.Has<gp::Transport>(unit) && !game.manifest.NetworkOf(unit).has_value())
		selling_detail::PutOut(game, unit, game.manifest.TakeAll(unit));
	std::vector<ecs::Entity> parked;
	std::vector<ecs::Entity> mines;
	ecs::Query<ecs::Write<gp::Jet>> jets(world);
	jets.ForEachChunk([&](auto chunk) {
		auto rows = chunk.template Get<gp::Jet>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < rows.size(); ++row)
		{
			gp::Jet &jet = rows[row];
			// (defectAllParkedUnits walks its spaces: a helicopter holds none.)
			if (jet.airfield != unit || jet.helicopter != 0)
				continue;
			const bool airborne = jet.state == gp::JetState::Flying || jet.state == gp::JetState::Returning || jet.state == gp::JetState::AwaitLanding ||
				jet.state == gp::JetState::ReturnToDeadAirfield || jet.state == gp::JetState::CirclingDeadAirfield;
			if (!airborne)
				parked.push_back(entities[row]);
			else if (const auto *jetOwner = world.Get<gp::Owner>(entities[row]); jetOwner != nullptr && jetOwner->player != to)
				jet.airfield = {}; // releaseSpace, setProducer(nullptr)
		}
	});
	ecs::Query<ecs::Read<gp::Minefield>> fields(world);
	fields.ForEachChunk([&](auto chunk) {
		const auto rows = chunk.template Get<gp::Minefield>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < rows.size(); ++row)
			if (rows[row].producer == unit)
				mines.push_back(entities[row]);
	});
	for (const ecs::Entity jet : parked)
		Defect(game, jet, team, detectionTicks);
	for (const ecs::Entity mine : mines)
		ChangeTeam(game, mine, team);
}
}
