export module engine.gameplay.rts.harvesting.systems.harvest_roster_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.harvesting.resources.harvest_roster;
export import engine.gameplay.rts.harvesting.resources.harvest_catalog;
export import engine.gameplay.rts.harvesting.components.resource_store;
export import engine.gameplay.rts.docking.components.dock;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;

// Gathers the tick's harvest sites (stores and depots that are docks, and
// the homes harvesters regroup to), in parallel per chunk.
export namespace engine::gameplay
{
struct HarvestRosterSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Read<Owner>, ecs::Optional<Dock>, ecs::Optional<ResourceStore>, ecs::Optional<ResourceDepot>,
		ecs::Optional<DefinitionRef>, ecs::Optional<Targetable>>;
	using Resources = ecs::Resources<ecs::Read<HarvestCatalog>, ecs::Write<HarvestRoster>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<HarvestRoster>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const HarvestCatalog &catalog = context.Read<HarvestCatalog>();
		const auto docks = chunk.Get<Dock>();
		const auto definitions = chunk.Get<DefinitionRef>();
		if (docks.empty() && (definitions.empty() || catalog.homeRank.empty()))
			return;
		auto &out = context.Write<HarvestRoster>().Slot(context);
		const auto transforms = chunk.Get<Transform>();
		const auto owners = chunk.Get<Owner>();
		const auto stores = chunk.Get<ResourceStore>();
		const auto depots = chunk.Get<ResourceDepot>();
		const auto bodies = chunk.Get<Targetable>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < transforms.size(); ++row)
		{
			HarvestSite site;
			site.homeRank = definitions.empty() ? HarvestCatalog::NoHome : catalog.HomeRank(definitions[row].index);
			if (!docks.empty())
			{
				site.store = !stores.empty();
				site.depot = !depots.empty();
				site.dynamic = docks[row].dynamic;
				site.approaches = docks[row].approachCount;
				site.open = docks[row].open;
				if (site.store)
				{
					site.boxes = stores[row].boxes;
					site.startingBoxes = stores[row].startingBoxes;
					site.deleteWhenEmpty = stores[row].deleteWhenEmpty;
					site.listed = stores[row].listed;
				}
			}
			if (!site.store && !site.depot && site.homeRank == HarvestCatalog::NoHome)
				continue;
			site.entity = entities[row];
			site.position = transforms[row].position;
			site.radius = bodies.empty() ? Engine::Math::Fixed{} : bodies[row].radius;
			site.player = owners[row].player;
			out.push_back(site);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::HarvestRosterSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.harvest_roster";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
