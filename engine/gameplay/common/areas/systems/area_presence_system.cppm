export module engine.gameplay.common.areas.systems.area_presence_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.areas.components.area_presence;
export import engine.gameplay.common.areas.resources.trigger_areas;
export import engine.gameplay.common.areas.resources.area_activity;
export import engine.gameplay.common.spatial.components.transform;

// Object::setTriggerAreaFlagsForChangeInPosition, after each tick's moves and in parallel: an object whose whole
// position changed first drops what an earlier tick's entries and exits left (updateTriggerAreaFlags), then exits the
// areas its new position is outside and enters those it is now inside (up to five), noting the tick. Two retail quirks
// are fixed: the exit test used the old position (an exit was noticed only on the next move), and the flag compaction
// tested the wrong entry (after an exit it dropped the later areas the object was still in). Any entry or exit makes the
// tick the last change of what the areas hold (GameLogic::updateObjectsChangedTriggerAreas).
export namespace engine::gameplay
{
namespace area_presence_detail
{
// REAL_TO_INT: toward zero.
inline std::int32_t Whole(Engine::Math::Fixed value) noexcept
{
	const std::int64_t raw = value.Raw();
	return static_cast<std::int32_t>(raw >= 0 ? raw >> 16 : -((-raw) >> 16));
}
}

// Whether an area was entered or left.
inline bool UpdateAreaPresence(AreaPresence &presence, const TriggerAreas &areas, std::int32_t x, std::int32_t y, std::uint64_t now)
{
	if (presence.known != 0 && presence.x == x && presence.y == y)
		return false;
	bool changed = false;
	presence.known = 1;
	if (presence.changedTick != 0 && presence.changedTick != now)
	{
		std::uint8_t kept = 0;
		for (std::size_t index = 0; index < presence.count; ++index)
			if ((presence.flags[index] & area_flag::Inside) != 0)
			{
				presence.area[kept] = presence.area[index];
				presence.flags[kept] = area_flag::Inside;
				++kept;
			}
		presence.count = kept;
	}
	for (std::size_t index = 0; index < presence.count; ++index)
		if ((presence.flags[index] & area_flag::Inside) != 0 && !areas.areas[presence.area[index]].Contains(x, y))
		{
			presence.flags[index] = static_cast<std::uint8_t>((presence.flags[index] & ~area_flag::Inside) | area_flag::Exited);
			presence.changedTick = now;
			changed = true;
		}
	presence.x = x;
	presence.y = y;
	for (std::uint32_t which = static_cast<std::uint32_t>(areas.areas.size()); which-- > 0;)
	{
		bool listed = false;
		for (std::size_t index = 0; index < presence.count && !listed; ++index)
			listed = presence.area[index] == which;
		if (listed || !areas.areas[which].Contains(x, y) || presence.count >= AreaPresence::Capacity)
			continue;
		presence.area[presence.count] = which;
		presence.flags[presence.count] = area_flag::Inside | area_flag::Entered;
		++presence.count;
		presence.changedTick = now;
		changed = true;
	}
	return changed;
}

struct AreaPresenceSystem
{
	using Query = ecs::Query<ecs::Write<AreaPresence>, ecs::Read<Transform>>;
	using Resources = ecs::Resources<ecs::Read<TriggerAreas>, ecs::Write<AreaChanges>, ecs::Write<AreaActivity>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<AreaChanges>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const TriggerAreas &areas = context.Read<TriggerAreas>();
		const std::uint64_t now = context.Tick();
		auto presences = chunk.Get<AreaPresence>();
		const auto transforms = chunk.Get<Transform>();
		bool changed = false;
		for (std::size_t row = 0; row < presences.size(); ++row)
			changed = UpdateAreaPresence(presences[row], areas, area_presence_detail::Whole(transforms[row].position.x),
				area_presence_detail::Whole(transforms[row].position.y), now) || changed;
		if (changed)
			context.Write<AreaChanges>().Slot(context).push_back(1);
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const
	{
		bool changed = false;
		context.Write<AreaChanges>().ForEach([&](std::uint8_t) { changed = true; });
		if (changed)
			context.Write<AreaActivity>().lastChange = context.Tick();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::AreaPresenceSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.area_presence";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
