export module engine.gameplay.rts.lifecycle.algorithms.retirement;
import std;

export import engine.gameplay.rts.lifecycle.resources.casualties;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.resources.name_registry;

// Taking entities out of the game consistently: their team, script name and
// seat in (or cargo of) a transport go with them, and passengers go down
// with their transport. One routine for both paths: the removal system
// (during the step, through commands) and scripted deletes between ticks.
export namespace engine::gameplay
{
struct RetirementBooks
{
	TeamRoster &roster;
	NameRegistry &names;
	CargoManifest &manifest;
};

struct Retiree
{
	ecs::Entity entity;
	ecs::Entity killer;
	Departure departure{Departure::Removed};
	// Killed, then deleted there and then: its die modules run, its body does not stay (a dead bike's rider).
	bool deleteAfter{false};
};

// What becomes of a rider when its container leaves (OpenContain::onDie: processDamageToContained, then
// killRidersWhoAreNotFreeToExit, then removeAllContained): gone with it unseen (deleted with it, a tunnel's cave-in),
// killed (it dies its own death), deleted outright, or out alive (the caller lets it out).
enum class RiderFate : std::uint8_t
{
	GoesDown,
	Killed,
	Deleted,
	Survives,
	KilledAndDeleted,
};

// What Retire needs to know about an entity.
struct RetireeState
{
	bool alive{false};
	const TeamMember *team{nullptr};
	const DefinitionRef *definition{nullptr};
	const Transform *transform{nullptr};
};

// `read(entity)` returns its RetireeState; `destroy(retiree)` removes it (or
// starts it dying: the death system decides by how it left); `fate(container,
// rider)` says what becomes of each rider of a departing container (the
// removal system's riders go down with it).
// Adds the riders of departing transports, updates the books and records
// the casualties, in the retirees' order.
template<typename Read, typename Destroy, typename Fate>
void Retire(std::vector<Retiree> retirees, RetirementBooks books, Read &&read, Destroy &&destroy, Casualties &casualties, Fate &&fate)
{
	for (std::size_t index = 0; index < retirees.size(); ++index)
	{
		// A networked container's riders move to the rest of its network, or cave in with the last of it
		// (TunnelTracker::onTunnelDestroyed: destroyed, unseen).
		const bool networked = books.manifest.NetworkOf(retirees[index].entity).has_value();
		for (const ecs::Entity rider : books.manifest.TakeAll(retirees[index].entity))
			switch (networked ? RiderFate::GoesDown : fate(retirees[index], rider))
			{
			case RiderFate::GoesDown: retirees.push_back({rider, retirees[index].killer, Departure::WentDown}); break;
			case RiderFate::Killed: retirees.push_back({rider, retirees[index].entity, Departure::Killed}); break;
			case RiderFate::Deleted: retirees.push_back({rider, retirees[index].killer, Departure::Removed}); break;
			case RiderFate::KilledAndDeleted: retirees.push_back({rider, retirees[index].entity, Departure::Killed, true}); break;
			case RiderFate::Survives: break;
			}
	}
	std::vector<ecs::Entity> gone;
	for (const Retiree &retiree : retirees)
	{
		const RetireeState state = read(retiree.entity);
		if (!state.alive || std::find(gone.begin(), gone.end(), retiree.entity) != gone.end())
			continue;
		gone.push_back(retiree.entity);
		if (state.team != nullptr)
			books.roster.Leave(state.team->team, retiree.entity);
		books.names.Forget(retiree.entity);
		books.manifest.Forget(retiree.entity);
		casualties.list.push_back({retiree.entity, state.definition != nullptr ? state.definition->index : 0u,
			state.transform != nullptr ? *state.transform : Transform{}, retiree.killer, retiree.departure, state.team != nullptr ? state.team->team : 0xFFFFFFFFu});
		destroy(retiree);
	}
}

template<typename Read, typename Destroy>
void Retire(std::vector<Retiree> retirees, RetirementBooks books, Read &&read, Destroy &&destroy, Casualties &casualties)
{
	Retire(std::move(retirees), books, std::forward<Read>(read), std::forward<Destroy>(destroy), casualties,
		[](const Retiree &, ecs::Entity) { return RiderFate::GoesDown; });
}
}
