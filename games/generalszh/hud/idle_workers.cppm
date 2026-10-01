export module games.generalszh.hud.idle_workers;
import std;

export import games.generalszh.session.session_view;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.death.components.dying;
import engine.ecs.query.query;
import games.generalszh.gameplay.construction.components.builder_boredom;

// The control bar's idle worker button (InGameUI's idle worker list, ButtonIdleWorker): the player's dozers and workers
// marked idle by DozerPrimaryIdleState, in the order they went on the list; the button is enabled while there is one
// (updateIdleWorker / showIdleWorkerLayout), and pressing it selects the next (selectNextIdleWorker) and centres the view
// on it.
export namespace generalszh::hud
{
// The player's idle workers, first marked first (two marked on the same tick: the older object first); one dying is off
// the list (Object's removeIdleWorker as it dies).
inline std::vector<ecs::Entity> IdleWorkers(session::SessionView &view, std::uint32_t player)
{
	namespace gp = engine::gameplay;
	auto &world = view.World();
	std::vector<std::pair<std::uint64_t, ecs::Entity>> marked;
	ecs::Query<ecs::Read<generalszh::gameplay::BuilderBoredom>, ecs::Read<gp::Owner>> query(world);
	query.ForEachChunk([&](auto chunk) {
		const auto boredoms = chunk.template Get<generalszh::gameplay::BuilderBoredom>();
		const auto owners = chunk.template Get<gp::Owner>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < boredoms.size(); ++row)
			if (boredoms[row].idleMarked != 0 && owners[row].player == player && !world.Has<gp::Dying>(entities[row]))
				marked.emplace_back(boredoms[row].idleMarked, entities[row]);
	});
	std::ranges::sort(marked, [](const auto &a, const auto &b) { return a.first != b.first ? a.first < b.first : a.second.index < b.second.index; });
	std::vector<ecs::Entity> out;
	out.reserve(marked.size());
	for (const auto &[tick, entity] : marked)
		out.push_back(entity);
	return out;
}

// What a worker is selected as: the outermost container it rides in, else itself.
inline ecs::Entity OutermostContainer(session::SessionView &view, ecs::Entity entity)
{
	namespace gp = engine::gameplay;
	auto &world = view.World();
	for (int depth = 0; depth < 8; ++depth)
	{
		const auto *passenger = world.Get<gp::Passenger>(entity);
		if (passenger == nullptr || !world.IsAlive(passenger->transport))
			break;
		entity = passenger->transport;
	}
	return entity;
}

// InGameUI::selectNextIdleWorker: none or several selected, the first on the list (its outermost container); one
// selected, the one after it among the list's distinct outermost containers (getUniqueIdleWorkers), wrapping round, or
// the first of them if what is selected is none of them. None: the list is empty.
inline std::optional<ecs::Entity> NextIdleWorker(session::SessionView &view, std::span<const ecs::Entity> idle, std::span<const ecs::Entity> selection)
{
	if (idle.empty())
		return std::nullopt;
	if (selection.size() != 1)
		return OutermostContainer(view, idle.front());
	std::vector<ecs::Entity> unique;
	for (const ecs::Entity worker : idle)
		if (const ecs::Entity top = OutermostContainer(view, worker); std::ranges::find(unique, top) == unique.end())
			unique.push_back(top);
	const auto at = std::ranges::find(unique, selection.front());
	if (at == unique.end())
		return unique.front();
	return std::next(at) == unique.end() ? unique.front() : *std::next(at);
}
}
