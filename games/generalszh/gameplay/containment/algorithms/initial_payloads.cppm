export module games.generalszh.gameplay.containment.algorithms.initial_payloads;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.containment.components.initial_payload;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.containment.algorithms.garrisons;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.components.cargo_size;
import engine.gameplay.rts.death.components.dying;
import engine.ecs.query.query;

// TransportContain::update's first pass (createPayload), as the tick starts: a transport not yet carrying its
// InitialPayload makes that many of it on its player's default team and puts each inside as it fits (isValidContainerFor
// with room: addToContain, without its load sounds); one that does not fit stays outside where it was made. An
// OverlordContain's PayloadTemplateName (OverlordContain::onObjectCreated -> createPayload: an Avenger's laser turret) is
// made the same way on the carrier's own team, before anything else acts on the carrier. A transport already gone or dying makes none (the TheSuperHackers fix: not for a destroyed object).
export namespace generalszh::gameplay
{
inline void CreateInitialPayloads(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	std::vector<std::pair<ecs::Entity, InitialPayload>> waiting;
	ecs::Query<ecs::Read<InitialPayload>> query(world);
	query.ForEachChunk([&](auto chunk) {
		const auto payloads = chunk.template Get<InitialPayload>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < payloads.size(); ++row)
			waiting.emplace_back(entities[row], payloads[row]);
	});
	for (const auto &[transport, payload] : waiting)
	{
		world.Remove<InitialPayload>(transport);
		if (!world.IsAlive(transport) || world.Has<gp::Dying>(transport) || world.Get<gp::Transport>(transport) == nullptr)
			continue;
		const auto *owner = world.Get<gp::Owner>(transport);
		const auto *member = world.Get<gp::TeamMember>(transport);
		const auto team = payload.ownTeam != 0 ? (member != nullptr ? std::optional<std::uint32_t>(member->team) : std::nullopt)
			: owner != nullptr ? game.roster.DefaultTeam(owner->player) : std::nullopt;
		if (!team)
			continue;
		const gp::Transform at = *world.Get<gp::Transform>(transport);
		const std::string name = game.templates.DefinitionAt(payload.definition).name;
		for (std::uint32_t index = 0; index < payload.count; ++index)
		{
			const ecs::Entity rider = SpawnObject(game, name, at.position.XY(), at.facing, *team, {});
			if (!world.IsAlive(rider))
				continue;
			// OverlordContain::createPayload: isValidContainerFor then addToContain (a portable structure mounts on top);
			// refused, it stays where it was made.
			if (payload.ownTeam != 0)
			{
				ContainInSource(game, transport, rider);
				continue;
			}
			const auto *size = world.Get<gp::CargoSize>(rider);
			const std::uint32_t slots = size != nullptr ? size->slots : 0u;
			const gp::Transport *room = world.Get<gp::Transport>(transport);
			if (slots == 0 || !MayContain(game, transport, rider) || room->occupied + slots > room->definition.slots)
				continue;
			PutInside(game, transport, rider, slots, true);
		}
	}
}
}
