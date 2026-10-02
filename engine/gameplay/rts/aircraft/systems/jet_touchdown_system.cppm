export module engine.gameplay.rts.aircraft.systems.jet_touchdown_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.aircraft.components.jet;
export import engine.gameplay.rts.aircraft.components.airfield;
export import engine.gameplay.rts.aircraft.components.touchdown;
export import engine.gameplay.rts.aircraft.resources.jet_touchdowns;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.health.components.health;

// JetTakeoffOrLandingState::update while landing (JetAIUpdate.cpp): every frame after the one its landing began on (the
// state's update runs from the frame after onEnter, before the frame's physics), a living jet that has not yet touched
// down on this landing touches down once its height less 0.25 is no more than the ground under it, raised by its
// airfield's LandingDeckHeightOffset while the airfield stands (getLandingDeckHeightOffset): the presentation's
// AircraftWheelScreech where it is. As a tick ends, on the state and place the jets' step and the movement left (what
// the original's next frame checks: its landing state, begun this tick or before, and where its physics put it); a
// batch: touchdowns are rare, and the tick's list keeps the query's order.
export namespace engine::gameplay
{
struct JetTouchdownSystem
{
	using Query = ecs::Query<ecs::Read<Jet>, ecs::Read<Transform>, ecs::Optional<Touchdown>, ecs::Optional<Health>>;
	using Lookup = ecs::Lookup<ecs::Read<Airfield>>;
	using Resources = ecs::Resources<ecs::Read<GroundHeight>, ecs::Write<JetTouchdowns>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		const auto lookup = context.Lookup<Lookup>();
		const GroundHeight &ground = context.Read<GroundHeight>();
		auto &touchdowns = context.Write<JetTouchdowns>().list;
		touchdowns.clear();
		auto &commands = context.Commands();
		const Fixed slop = Fixed::FromRatio(1, 4);
		query.ForEachChunk([&](auto chunk) {
			const auto jets = chunk.template Get<Jet>();
			const auto transforms = chunk.template Get<Transform>();
			const auto marks = chunk.template Get<Touchdown>();
			const auto healths = chunk.template Get<Health>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < jets.size(); ++row)
			{
				const Jet &jet = jets[row];
				if (jet.state != JetState::Landing || jet.helicopter != 0)
					continue;
				// m_landingSoundPlayed: once a landing (reset as the state is entered).
				if (!marks.empty() && marks[row].landing == jet.since)
					continue;
				// isEffectivelyDead: the state fails before it looks.
				if (!healths.empty() && IsDead(healths[row]))
					continue;
				const Transform &transform = transforms[row];
				Fixed groundZ = ground.At(transform.position.XY());
				if (const Airfield *field = lookup.IsAlive(jet.airfield) ? lookup.Get<Airfield>(jet.airfield) : nullptr)
					groundZ = groundZ + field->deckHeight;
				if (transform.position.z - slop > groundZ)
					continue;
				touchdowns.push_back({entities[row], transform.position});
				if (marks.empty())
					commands.Add<Touchdown>(entities[row], Touchdown{jet.since});
				else
					commands.Set<Touchdown>(entities[row], Touchdown{jet.since});
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::JetTouchdownSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.jet_touchdowns";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
