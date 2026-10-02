export module games.generalszh.gameplay.construction.algorithms.builder_boredom;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.construction.components.builder_boredom;
import games.generalszh.gameplay.construction.algorithms.building;
import games.generalszh.gameplay.mines.algorithms.mine_clearing;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import engine.gameplay.common.spatial.resources.spatial_index;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;

// DozerPrimaryIdleState::update, bored: findObjectToRepair (the closest structure of its own player within BoredRange,
// centre to centre, it may repair: canRepairObject) and aiRepair it (CMD_FROM_AI); else findMine (the closest within
// BoredRange it may attack as a dozer, CMD_FROM_DOZER: an enemy's, not hidden from it) and aiAttackObject it for one shot.
// DozerAIUpdate::getBoredRange: a computer player's dozer (not a WorkerAIUpdate worker) looks AIDozerBoredRadiusModifier
// times as far, by its player as it is now.
export namespace generalszh::gameplay
{
inline void ApplyBoredBuilders(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	auto *resource = world.FindResource<BoredBuilders>();
	const auto *spatial = world.FindResource<gp::SpatialIndex>();
	if (resource == nullptr || spatial == nullptr)
		return;
	std::vector<ecs::Entity> bored;
	resource->AppendTo(bored);
	resource->Reset(0);
	for (const ecs::Entity dozer : bored)
	{
		const auto *boredom = world.IsAlive(dozer) ? world.Get<BuilderBoredom>(dozer) : nullptr;
		const auto *transform = world.Get<gp::Transform>(dozer);
		const auto *owner = world.Get<gp::Owner>(dozer);
		if (boredom == nullptr || transform == nullptr || owner == nullptr)
			continue;
		const Engine::Math::FixedVector2 at = transform->position.XY();
		Engine::Math::Fixed range = boredom->boredRange;
		if (owner->player < game.roster.PlayerCount() && !game.roster.PlayerAt(owner->player).human)
			if (const auto *ref = world.Get<gp::DefinitionRef>(dozer))
			{
				const auto &modules = game.templates.DefinitionAt(ref->index).modules;
				if (std::any_of(modules.begin(), modules.end(), [](const content::ModuleEntry &module) { return module.type == "DozerAIUpdate"; }))
					range = range * game.templates.Content().aiData.aiDozerBoredRadiusModifier;
			}
		const gp::SpatialEntry *repair = nullptr;
		Engine::Math::Fixed nearest;
		spatial->ForEachWithin(at, range, [&](const gp::SpatialEntry &entry) {
			if (entry.player != owner->player || (entry.classes & gp::target_class::Structure) == 0)
				return;
			const Engine::Math::Fixed distance = Engine::Math::DistanceSquared(entry.position.XY(), at);
			if (distance > range * range || (repair != nullptr && distance >= nearest) || !MayRepair(game, dozer, entry.entity))
				return;
			repair = &entry;
			nearest = distance;
		});
		if (repair != nullptr)
		{
			if (OrderWork(game, dozer, repair->entity))
				AiCommanded(game, dozer);
			continue;
		}
		const ecs::Entity mine = mine_clearing_detail::Closest(game, dozer, at, range, gp::CommandSource::Dozer, false, std::nullopt, {});
		if (mine != ecs::Entity{})
			OrderAttack(game, dozer, mine, 1, gp::CommandSource::Dozer);
	}
}
}
