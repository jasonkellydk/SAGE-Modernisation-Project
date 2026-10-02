export module games.generalszh.gameplay.railroad.algorithms.railroad_motion;
import std;

export import games.generalszh.gameplay.railroad.components.railcar;
export import games.generalszh.gameplay.railroad.resources.rail_network;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.ground_height;
export import Engine.Core.Math.FixedAngle;

// RailroadBehavior's track and motion (RailroadGuideAIUpdate.cpp), as free functions over a train's cars.
export namespace generalszh::gameplay
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;
using Engine::Math::FixedVector3;

// One car as the railroad walks it this tick.
struct RailCarRow
{
	ecs::Entity entity;
	Railcar *car{nullptr};
	engine::gameplay::Transform *transform{nullptr};
	const RailroadConfig *config{nullptr};
	std::uint32_t player{0};
};

// The tick's cars (in entity order) and what they run on.
struct RailScene
{
	std::span<RailCarRow> cars;
	const RailWaypoints &waypoints;
	RailTracks &tracks;
	const engine::gameplay::GroundHeight &ground;
	std::vector<ecs::Entity> &destroyed; // took themselves away this tick (destroyObject)
	RailroadRequests *requests{nullptr}; // walls and passengers to see to after the tick
	RailroadCues *cues{nullptr};         // the tick's sounds

	RailCarRow *Find(ecs::Entity entity) noexcept
	{
		const auto found = std::lower_bound(cars.begin(), cars.end(), entity, [](const RailCarRow &row, ecs::Entity e) {
			return row.entity.index != e.index ? row.entity.index < e.index : row.entity.generation < e.generation;
		});
		return found != cars.end() && found->entity == entity ? &*found : nullptr;
	}
};

// TerrainLogic's waypoints as loadTrackData walks them (see RailWaypoints): `markers` in map order with their ids,
// names, positions and bidirectional flags, `links` every (from, to) link in map order.
struct RailMarker
{
	std::uint32_t id{0};
	std::string_view name;
	FixedVector2 position;
	bool biDirectional{false};
};

inline RailWaypoints BuildRailWaypoints(std::span<const RailMarker> markers, std::span<const std::array<std::uint32_t, 2>> links,
	const engine::gameplay::GroundHeight &ground)
{
	RailWaypoints out;
	const std::size_t count = markers.size();
	std::vector<std::vector<std::uint32_t>> linked(count); // list indices, addLink order
	// addWaypoint puts each at the head of the list: the map's order reversed.
	for (std::size_t k = 0; k < count; ++k)
	{
		const RailMarker &marker = markers[count - 1 - k];
		out.ids.push_back(marker.id);
		out.positions.push_back({marker.position.x, marker.position.y, ground.At(marker.position)});
		std::uint8_t kinds = 0;
		if (marker.name.ends_with("Tunnel"))
			kinds |= rail_point::Tunnel;
		if (marker.name.ends_with("Station"))
			kinds |= rail_point::Station;
		if (marker.name.ends_with("Disembark"))
			kinds |= rail_point::Disembark;
		if (marker.name.ends_with("PingPong"))
			kinds |= rail_point::PingPong;
		out.kinds.push_back(kinds);
	}
	// addWaypointLink: found by id walking the list (the last found wins); not twice; a bidirectional one also back.
	const auto find = [&](std::uint32_t id) {
		std::uint32_t found = RailWaypoints::None;
		for (std::uint32_t index = 0; index < count; ++index)
			if (out.ids[index] == id)
				found = index;
		return found;
	};
	for (const auto &[from, to] : links)
	{
		const std::uint32_t a = find(from), b = find(to);
		if (a == RailWaypoints::None || b == RailWaypoints::None || a == b)
			continue;
		if (std::ranges::find(linked[a], b) != linked[a].end())
			continue;
		linked[a].push_back(b);
		if (markers[count - 1 - a].biDirectional && std::ranges::find(linked[b], a) == linked[b].end())
			linked[b].push_back(a);
	}
	for (std::size_t index = 0; index < count; ++index)
		out.next.push_back(linked[index].empty() ? RailWaypoints::None : linked[index].front());
	return out;
}

// loadTrackData's anchor: the waypoint nearest (3D) the locomotive, the first in list order on a tie.
inline std::uint32_t RailAnchor(const RailWaypoints &waypoints, const FixedVector3 &at) noexcept
{
	std::uint32_t best = RailWaypoints::None;
	Fixed closest = Fixed::FromInt(99999);
	for (std::uint32_t index = 0; index < waypoints.ids.size(); ++index)
	{
		const Fixed distance = Engine::Math::Distance(waypoints.positions[index], at);
		if (closest > distance)
		{
			closest = distance;
			best = index;
		}
	}
	return best;
}

// loadTrackData from the anchor (a list index): point 0 on it, then along the first links, each point's distance along
// the track, its handle the waypoint before it (the first two: the anchor's), Tunnel and Station from its own waypoint,
// Disembark and PingPong from the one before; back at the anchor it loops. (The original hangs on a chain that loops
// back to another waypoint: it stops there instead.)
inline const RailTrack &LayTrack(RailTracks &tracks, const RailWaypoints &waypoints, std::uint32_t anchor)
{
	RailTrack track;
	track.anchor = waypoints.ids[anchor];
	track.first = static_cast<std::uint32_t>(tracks.positions.size());
	const auto push = [&](const FixedVector3 &at, Fixed along, std::uint32_t handle, std::uint8_t kinds) {
		tracks.positions.push_back(at);
		tracks.along.push_back(along);
		tracks.handles.push_back(handle);
		tracks.kinds.push_back(kinds);
		++track.count;
	};
	constexpr std::uint8_t own = rail_point::Tunnel | rail_point::Station;
	push(waypoints.positions[anchor], Fixed{}, waypoints.ids[anchor], static_cast<std::uint8_t>(waypoints.kinds[anchor] & (own | rail_point::Disembark)));
	std::uint32_t scanner = anchor;
	for (std::size_t steps = 0; steps <= waypoints.ids.size(); ++steps)
	{
		const std::uint32_t next = waypoints.next[scanner];
		if (next == RailWaypoints::None)
			break;
		track.length += Engine::Math::Distance(waypoints.positions[scanner], waypoints.positions[next]);
		push(waypoints.positions[next], track.length, waypoints.ids[scanner],
			static_cast<std::uint8_t>((waypoints.kinds[next] & own) | (waypoints.kinds[scanner] & (rail_point::Disembark | rail_point::PingPong))));
		scanner = next;
		if (scanner == anchor)
		{
			track.looping = true;
			break;
		}
	}
	tracks.tracks.push_back(track);
	return tracks.tracks.back();
}

// The track of an anchor (its id), laid again when the tracks were made anew.
inline const RailTrack *TrackOf(RailScene &scene, std::uint32_t anchor)
{
	if (anchor == Railcar::NoTrack)
		return nullptr;
	if (const RailTrack *track = scene.tracks.Of(anchor))
		return track;
	const std::uint32_t index = scene.waypoints.IndexOf(anchor);
	return index == RailWaypoints::None ? nullptr : &LayTrack(scene.tracks, scene.waypoints, index);
}

// FindPosByPathDistance: where `distance` along the track is (a looping track wraps it; one that does not puts the car
// in the wings before its start, at the end of the line past it, for good). Within a segment (strictly between two
// points) it is along the straight line from the first; exactly on a point it is the one before (on 0 the origin).
// With `setState`: the car is in a tunnel as the segment is; a locomotive coming into a new segment (its handle) brakes
// for a station, brakes to unload at a Disembark one, and at a PingPong one (not the one it last turned at) brakes and
// turns back.
inline FixedVector3 FindPosition(RailCarRow &row, RailTracks &tracks, const RailTrack &track, Fixed distance, bool setState, RailScene *scene = nullptr)
{
	Railcar &car = *row.car;
	car.wings = 0;
	Fixed a = distance;
	if (track.looping)
	{
		if (track.length > Fixed{})
		{
			while (a < Fixed{})
				a += track.length;
			while (a > track.length)
				a -= track.length;
		}
	}
	else
	{
		if (distance < Fixed{})
			car.wings = 1;
		else if (distance >= track.length)
			car.endOfLine = 1;
		a = std::clamp(distance, Fixed{}, track.length);
	}
	FixedVector3 position;
	for (std::uint32_t k = 0; k < track.count; ++k)
	{
		const std::uint32_t point = track.first + k;
		if (!(tracks.along[point] < a))
			continue;
		if (k + 1 < track.count && tracks.along[point + 1] > a)
		{
			const std::uint32_t handle = tracks.handles[point];
			const bool edge = car.handle != handle;
			if (setState)
			{
				const std::uint8_t kinds = tracks.kinds[point];
				car.tunnel = (kinds & rail_point::Tunnel) != 0 ? 1 : 0;
				if (car.locomotive != 0 && edge)
				{
					car.handle = handle;
					if ((kinds & rail_point::Station) != 0)
					{
						car.state = ConductorState::ApplyBrakes;
						car.disembark = 0;
					}
					else if ((kinds & rail_point::Disembark) != 0)
					{
						car.state = ConductorState::ApplyBrakes;
						car.disembark = 1;
					}
					else if ((kinds & rail_point::PingPong) != 0 && car.conductor.lastPingPong != handle)
					{
						car.conductor.lastPingPong = handle;
						car.state = ConductorState::ApplyBrakes;
						car.disembark = 0;
						car.conductor.direction = -car.conductor.direction;
					}
				}
				// Its clickety-clack coming into it, out of a tunnel, where it is and at its speed / 10 (a car never
				// notes the segment it is in: every frame).
				if (edge && car.tunnel == 0 && scene != nullptr && scene->cues != nullptr && !row.config->clicketyClackSound.empty())
					scene->cues->list.push_back({RailroadCue::Kind::ClicketyClack, row.entity, row.transform->position,
						car.conductor.speed / Fixed::FromInt(10), 0xFFFFFFFFu, row.config->clicketyClackSound});
			}
			const FixedVector3 from = tracks.positions[point];
			return from + Engine::Math::Normalize(tracks.positions[point + 1] - from) * (a - tracks.along[point]);
		}
		position = tracks.positions[point];
	}
	return position;
}

// updatePositionTrackDistance: the car's rear goes 2R behind its puller's point, at its speed and way; turned to face
// from where its rear was toward its puller's point, its centre R ahead of its rear point; on the ground where its rear
// was (not in a tunnel, where its height holds).
inline void UpdatePosition(RailScene &scene, RailCarRow &row, const RailTrack &track, const RailPull &puller, RailPull &me)
{
	const Fixed radius = row.config->radius;
	me.distance = puller.distance - radius * Fixed::FromInt(2);
	me.speed = puller.speed;
	me.direction = puller.direction;
	me.hitch = FindPosition(row, scene.tracks, track, me.distance, false);
	const FixedVector3 rear = FindPosition(row, scene.tracks, track, me.distance, true, &scene);
	engine::gameplay::Transform &transform = *row.transform;
	const FixedVector2 turn = transform.position.XY() - Engine::Math::Direction(transform.facing) * radius;
	const Engine::Math::TurnAngle desired = Engine::Math::Atan2(puller.hitch.y - turn.y, puller.hitch.x - turn.x);
	const FixedVector2 centre = rear.XY() + Engine::Math::Direction(desired) * radius;
	transform.position.x = centre.x;
	transform.position.y = centre.y;
	transform.facing = desired;
	if (row.car->tunnel == 0)
		transform.position.z = scene.ground.At(turn);
}

// Its trailer's getPulled down the chain from `puller`: each takes the pull before it (counting itself pulled, keeping a
// copy of it as its conductor) and places itself; a car with nothing (left) behind it lets go of it, and at the end of
// the line takes itself away.
inline void PullChain(RailScene &scene, RailCarRow &first, const RailTrack &track)
{
	RailCarRow *puller = &first;
	for (std::size_t guard = 0; guard <= scene.cars.size(); ++guard)
	{
		RailCarRow *trailer = puller->car->trailer == ecs::Entity{} ? nullptr : scene.Find(puller->car->trailer);
		if (trailer == nullptr || trailer->car->gone != 0)
		{
			puller->car->trailer = {};
			if (puller->car->endOfLine != 0 && puller->car->gone == 0)
			{
				puller->car->gone = 1;
				scene.destroyed.push_back(puller->entity);
			}
			return;
		}
		Railcar &car = *trailer->car;
		car.unpulled = 0;
		const RailTrack *own = TrackOf(scene, car.anchor);
		if (own == nullptr)
			return;
		car.conductor = puller->car->pull;
		UpdatePosition(scene, *trailer, *own, puller->car->pull, car.pull);
		puller = trailer;
	}
}

// The car's own update this frame (RailroadBehavior::update, past loading its track): a locomotive's conductor brakes
// (by Braking a frame, to a stop under 0.1: waiting WaitAtStationTime there, its train a wall on the pathfinding grid,
// its passengers let out if it stopped to unload), waits (setting off at 0.05 its way once the wait is over and no script
// holds it, the wall taken away), or speeds up (0.02 its way more, times Acceleration, within SpeedMax either way);
// a car not pulled for over two of its updates leads from then on. A leading car (a left-behind one coasting, slowed by
// Friction a frame) drives its conductor point along the track (wrapping round a loop), places itself behind it and
// pulls its chain; one not leading counts its update.
inline void StepRailcar(RailScene &scene, RailCarRow &row)
{
	Railcar &car = *row.car;
	const RailroadConfig &config = *row.config;
	const RailTrack *track = TrackOf(scene, car.anchor);
	if (track == nullptr)
		return;
	if (car.locomotive != 0)
	{
		Fixed &speed = car.conductor.speed;
		switch (car.state)
		{
		case ConductorState::ApplyBrakes:
			speed = speed * config.braking;
			if (Engine::Math::Abs(speed) < Fixed::FromRatio(1, 10))
			{
				speed = Fixed{};
				car.waitTimer = config.waitAtStationTicks;
				car.state = ConductorState::WaitAtStation;
				if (scene.requests != nullptr)
				{
					scene.requests->walls.emplace_back(row.entity, true);
					if (car.disembark != 0)
						scene.requests->disembarks.push_back(row.entity);
				}
				car.disembark = 0;
			}
			break;
		case ConductorState::WaitAtStation:
			--car.waitTimer;
			if (car.waitTimer <= 0 && car.held == 0)
			{
				car.state = ConductorState::Accelerate;
				speed = Fixed::FromRatio(1, 20) * Fixed::FromInt(car.conductor.direction);
				if (scene.requests != nullptr)
					scene.requests->walls.emplace_back(row.entity, false);
			}
			// The whistle a quarter of the wait before it goes.
			else if (car.waitTimer == config.waitAtStationTicks / 4 && scene.cues != nullptr && !config.whistleSound.empty())
				scene.cues->list.push_back({RailroadCue::Kind::Whistle, row.entity, row.transform->position, Fixed::One(), 0xFFFFFFFFu, config.whistleSound});
			break;
		case ConductorState::Accelerate:
			speed = (speed + Fixed::FromRatio(1, 50) * Fixed::FromInt(car.conductor.direction)) * config.acceleration;
			if (speed > config.speedMax)
				speed = config.speedMax;
			else if (speed < -config.speedMax)
				speed = -config.speedMax;
			break;
		case ConductorState::Coast: break;
		}
	}
	if (car.unpulled > 2)
		car.lead = 1;
	if (car.lead == 0)
	{
		if (car.unpulled <= 2)
			++car.unpulled;
		return;
	}
	if (car.state == ConductorState::Coast)
		car.conductor.speed = car.conductor.speed * config.friction;
	car.conductor.distance += car.conductor.speed;
	if (track->looping && track->length > Fixed{})
	{
		while (car.conductor.distance > track->length)
			car.conductor.distance -= track->length;
		while (car.conductor.distance < Fixed{})
			car.conductor.distance += track->length;
	}
	car.conductor.hitch = FindPosition(row, scene.tracks, *track, car.conductor.distance, false);
	UpdatePosition(scene, row, *track, car.conductor, car.pull);
	PullChain(scene, row, *track);
}
}
