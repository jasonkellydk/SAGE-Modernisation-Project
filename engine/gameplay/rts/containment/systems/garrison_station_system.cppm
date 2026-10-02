export module engine.gameplay.rts.containment.systems.garrison_station_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.containment.algorithms.garrison_stations;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.common.appearance.components.appearance;

// Before the step, chunk-parallel: each container that does not enclose its occupants (GarrisonStations) settles who
// stands at which station (CurrentStations: a newcomer or a changed model condition redeploys everyone in order of
// entry; else the gone give theirs up), and remembers the model condition it did so under. The riders are put there
// after the step (PassengerRideSystem).
export namespace engine::gameplay
{
struct GarrisonStationSystem
{
	using Query = ecs::Query<ecs::Write<GarrisonStations>, ecs::Optional<Appearance>>;
	using Resources = ecs::Resources<ecs::Read<CargoManifest>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const CargoManifest &manifest = context.Read<CargoManifest>();
		auto stations = chunk.Get<GarrisonStations>();
		const auto looks = chunk.Get<Appearance>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < stations.size(); ++row)
		{
			GarrisonStations &held = stations[row];
			const bool changed = !looks.empty() && looks[row].flags != held.condition;
			held.occupants = CurrentStations(held, manifest.Aboard(entities[row]), changed);
			if (!looks.empty())
				held.condition = looks[row].flags;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::GarrisonStationSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.garrison_stations";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
