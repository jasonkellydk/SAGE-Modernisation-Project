export module engine.gameplay.rts.vision.systems.dynamic_clearing_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.vision.components.dynamic_clearing;
export import engine.gameplay.rts.vision.components.vision;
export import engine.gameplay.common.identity.components.definition_ref;

// DynamicShroudClearingRangeUpdate::update for everything whose clearing range swells and fades, chunk-parallel, every
// tick until it sleeps: its phase from the ticks left (done once none are, or past its end; shrinking once ShrinkDelay
// has passed; holding from the end of its growth; growing from GrowDelay), its range stepped for the phase (growing by a
// GrowTime-th of its full range a tick, and holding once full; full; shrinking by a ShrinkTime-th of the way to
// FinalVision a tick; FinalVision), then the object's clearing range set to it on the first tick and every
// ChangeInterval + 1 ticks after (GrowInterval + 1 while growing); once done and set, it sleeps. Its grid is made on its
// first update; not started or growing, it animates (before the growth: its ring its range plus twice the ticks gone,
// its opacity 1 - range / full range); holding or done, it goes for good.
export namespace engine::gameplay
{
struct DynamicClearingSystem
{
	using Query = ecs::Query<ecs::Write<DynamicClearing>, ecs::Write<Vision>, ecs::Read<DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<DynamicClearingCatalog>>;

	// One tick of it; the object's clearing range, when it is set.
	static std::optional<Engine::Math::Fixed> Step(DynamicClearing &clearing, const DynamicClearingDefinition &how, std::uint64_t now) noexcept
	{
		using Engine::Math::Fixed;
		using State = DynamicClearingState;
		if (clearing.state == State::Sleeping)
			return std::nullopt;
		const std::int64_t total = static_cast<std::int64_t>(how.shrinkDelay) + how.shrinkTime;
		const std::int64_t shrinkStart = total - how.shrinkDelay;
		const std::int64_t growStart = total - how.growDelay;
		const std::int64_t sustain = growStart - how.growTime;
		// createGridDecals (m_decalsCreated).
		if (clearing.grid == 0)
			clearing.grid = 1;
		// animateGridDecals: m_totalFrames is max(1, the whole time).
		const auto animate = [&] {
			clearing.gridRadius = clearing.current + Fixed::FromInt((std::max<std::int64_t>(1, total) - clearing.countdown) * 2);
			clearing.gridOpacity = clearing.native > Fixed{} ? Fixed::One() - clearing.current / clearing.native : Fixed{};
		};
		if (clearing.countdown <= 0 || now > clearing.doneForeverTick)
			clearing.state = State::DoneForever;
		else if (clearing.countdown <= shrinkStart)
			clearing.state = State::Shrinking;
		else if (clearing.countdown <= sustain)
			clearing.state = State::Sustaining;
		else if (clearing.countdown <= growStart)
			clearing.state = State::Growing;
		switch (clearing.state)
		{
		case State::Growing:
			animate();
			clearing.current += clearing.native / Fixed::FromInt(std::max<std::int64_t>(1, how.growTime));
			if (clearing.current >= clearing.native)
				clearing.state = State::Sustaining;
			break;
		case State::Sustaining:
			clearing.current = clearing.native;
			clearing.grid = 2; // killGridDecals
			break;
		case State::Shrinking: clearing.current -= (clearing.native - how.finalVision) / Fixed::FromInt(std::max<std::int64_t>(1, how.shrinkTime)); break;
		case State::DoneForever:
			clearing.grid = 2; // killGridDecals
			clearing.current = how.finalVision;
			break;
		case State::NotStarted: animate(); break;
		case State::Sleeping: break;
		}
		if (clearing.countdown > 0)
			--clearing.countdown;
		if (clearing.changeCountdown > 0)
		{
			--clearing.changeCountdown;
			return std::nullopt;
		}
		clearing.changeCountdown = clearing.state == State::Growing ? how.growInterval : how.changeInterval;
		if (clearing.state == State::DoneForever)
			clearing.state = State::Sleeping;
		return clearing.current;
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const DynamicClearingCatalog &catalog = context.Read<DynamicClearingCatalog>();
		const std::uint64_t now = context.Tick();
		auto clearings = chunk.Get<DynamicClearing>();
		auto visions = chunk.Get<Vision>();
		const auto refs = chunk.Get<DefinitionRef>();
		for (std::size_t row = 0; row < clearings.size(); ++row)
			if (const DynamicClearingDefinition *how = catalog.Of(refs[row].index))
				if (const auto range = Step(clearings[row], *how, now))
					visions[row].clearingRange = *range; // Object::setShroudClearingRange
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::DynamicClearingSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.dynamic_clearing";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// Before the vision system (PostSimulation) looks with its range: setShroudClearingRange re-looks at once.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
