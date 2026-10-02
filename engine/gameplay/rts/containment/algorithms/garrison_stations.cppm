export module engine.gameplay.rts.containment.algorithms.garrison_stations;
import std;

export import engine.gameplay.rts.containment.components.garrison_points;
export import engine.gameplay.common.spatial.components.transform;

// Who stands at which station of a container that does not enclose its occupants (a fire base's STATION bones).
export namespace engine::gameplay
{
// GarrisonContain's stations this tick, from `aboard` (its contain list, in order of entry):
// - someone aboard at no station (addToContain's redeployOccupants: it has just got in), or `redeploy` (its model
//   condition changed: OpenContain::update): loadStationGarrisonPoints forgets every occupant, and
//   positionObjectsAtStationGarrisonPoints gives each, in order of entry, the first free station (pickAStationForMe);
// - else those no longer aboard give theirs up (onRemoving: removeObjectFromStationPoint) and the rest keep theirs.
// More occupants than stations: the rest have none.
inline std::array<ecs::Entity, GarrisonStations::Max> CurrentStations(const GarrisonStations &stations, std::span<const ecs::Entity> aboard,
	bool redeploy) noexcept
{
	const std::size_t count = std::min<std::size_t>(stations.count, GarrisonStations::Max);
	const auto stationOf = [&](ecs::Entity rider) {
		for (std::size_t index = 0; index < count; ++index)
			if (stations.occupants[index] == rider)
				return index;
		return GarrisonStations::Max;
	};
	bool newcomer = false;
	for (const ecs::Entity rider : aboard)
		newcomer = newcomer || stationOf(rider) == GarrisonStations::Max;
	std::array<ecs::Entity, GarrisonStations::Max> occupants{};
	if (!newcomer && !redeploy)
		for (std::size_t index = 0; index < count; ++index)
			if (std::find(aboard.begin(), aboard.end(), stations.occupants[index]) != aboard.end())
				occupants[index] = stations.occupants[index];
	for (const ecs::Entity rider : aboard)
	{
		if (std::find(occupants.begin(), occupants.begin() + static_cast<std::ptrdiff_t>(count), rider) != occupants.begin() + static_cast<std::ptrdiff_t>(count))
			continue;
		for (std::size_t index = 0; index < count; ++index)
			if (occupants[index] == ecs::Entity{})
			{
				occupants[index] = rider;
				break;
			}
	}
	return occupants;
}

// Where station `index` is in the world: the container's frame applied to the bone (loadStationGarrisonPoints'
// getMultiLogicalBonePosition with convertToWorld).
inline Engine::Math::FixedVector3 StationPosition(const GarrisonStations &stations, std::size_t index, const Transform &container) noexcept
{
	const Engine::Math::FixedVector3 local = stations.points[index];
	const Engine::Math::Fixed c = Engine::Math::Cos(container.facing), s = Engine::Math::Sin(container.facing);
	return {container.position.x + local.x * c - local.y * s, container.position.y + local.x * s + local.y * c, container.position.z + local.z};
}
}
