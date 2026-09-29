export module games.generalszh.presentation.hud.algorithms.radar_event_rules;
import std;

export import games.generalszh.presentation.hud.resources.radar_events;

// Radar::createEvent / internalCreateEvent / tryEvent / update: an event takes the next place in the ring, lives
// LOGICFRAMES_PER_SECOND x its seconds (4 by default), fading its last half second, in its type's two colours. A
// tried one is refused if one of its type began within 250 of it less than 10 seconds ago (under attack: anywhere on
// the map, the retail suppression). Events past their die frame go inactive.
export namespace generalszh::presentation
{
// radarColorLookupTable.
inline std::pair<std::array<std::uint8_t, 4>, std::array<std::uint8_t, 4>> RadarEventColors(RadarEventType type)
{
	using C = std::array<std::uint8_t, 4>;
	switch (type)
	{
	case RadarEventType::Construction: return {C{128, 128, 255, 255}, C{128, 255, 255, 255}};
	case RadarEventType::Upgrade: return {C{128, 0, 64, 255}, C{255, 185, 220, 255}};
	case RadarEventType::UnderAttack: return {C{255, 0, 0, 255}, C{255, 128, 128, 255}};
	case RadarEventType::Information:
	case RadarEventType::BeaconPulse: return {C{255, 255, 0, 255}, C{255, 255, 128, 255}};
	case RadarEventType::Infiltration: return {C{0, 255, 255, 255}, C{128, 255, 255, 255}};
	case RadarEventType::BattlePlan: return {C{255, 255, 255, 255}, C{255, 255, 255, 255}};
	case RadarEventType::StealthDiscovered:
	case RadarEventType::StealthNeutralized: return {C{0, 255, 0, 255}, C{0, 128, 0, 255}};
	case RadarEventType::Fake: return {C{0, 0, 0, 0}, C{0, 0, 0, 0}};
	default: return {C{255, 255, 255, 255}, C{255, 255, 255, 255}};
	}
}

inline void CreateRadarEvent(RadarEvents &radar, std::array<float, 3> world, RadarEventType type, std::uint64_t frame, float secondsToLive = 4.0f)
{
	const auto [color1, color2] = RadarEventColors(type);
	RadarEvent &event = radar.events[radar.next];
	event.type = type;
	event.active = true;
	event.createFrame = frame;
	// LOGICFRAMES_PER_SECOND * secondsToLive, truncated into the UnsignedInt frame.
	event.dieFrame = frame + static_cast<std::uint64_t>(30.0f * secondsToLive);
	event.fadeFrame = event.dieFrame - static_cast<std::uint64_t>(30.0f * 0.5f);
	event.color1 = color1;
	event.color2 = color2;
	event.world = world;
	event.soundPlayed = false;
	if (type != RadarEventType::BeaconPulse)
		radar.last = radar.next;
	radar.next = (radar.next + 1) % RadarEvents::Capacity;
}

inline bool TryRadarEvent(RadarEvents &radar, RadarEventType type, std::array<float, 3> world, std::uint64_t frame)
{
	if (type == RadarEventType::Invalid)
		return false;
	constexpr float closeEnoughSquared = 250.0f * 250.0f;
	constexpr std::uint64_t framesBetween = 30 * 10;
	for (const RadarEvent &event : radar.events)
	{
		if (event.type != type)
			continue;
		const float dx = event.world[0] - world[0], dy = event.world[1] - world[1];
		const bool close = dx * dx + dy * dy <= closeEnoughSquared || type == RadarEventType::UnderAttack;
		if (close && frame - event.createFrame < framesBetween)
			return false;
	}
	CreateRadarEvent(radar, world, type, frame);
	return true;
}

inline void UpdateRadarEvents(RadarEvents &radar, std::uint64_t frame)
{
	for (RadarEvent &event : radar.events)
		if (event.active && event.createFrame != 0 && frame > event.dieFrame)
			event.active = false;
}
}
