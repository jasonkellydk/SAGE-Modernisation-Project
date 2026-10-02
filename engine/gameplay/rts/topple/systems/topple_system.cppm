export module engine.gameplay.rts.topple.systems.topple_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.rts.topple.components.topple;
export import engine.gameplay.rts.topple.resources.topple_events;
export import engine.gameplay.rts.collision.systems.collision_systems;
export import engine.gameplay.common.spatial.components.attitude;
export import engine.gameplay.common.spatial.components.body_extent;
export import engine.gameplay.common.lifetime.components.lifetime;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.death.resources.blast_waves;
export import Engine.Core.Math.FixedAngle;
export import Engine.Core.Math.FixedVector;

// Toppling, in parallel per chunk: an upright thing run into by a heavy
// crusher (crusher level above 1), or reached by a blast's push (the
// tick's BlastWaves: away from the blast at its topple speed, not bouncing,
// no bounce effects), or pushed by a game rule (the tick's TopplePushes: a
// flood wave's victims), starts falling away from it; nothing dead topples
// (ToppleUpdate::applyTopplingForce); falling, it
// turns to lie along its fall, tips over by its angular velocity (with its
// acceleration), bounces back off the ground with a share of it until it
// hardly moves, and is then down, killed (as toppled) when it says so.
// Starts and bounces are events the game plays effects for.
export namespace engine::gameplay
{
struct ToppleSystem
{
	using Query = ecs::Query<ecs::Write<Topple>, ecs::Write<Transform>, ecs::Write<Attitude>, ecs::Optional<Dying>, ecs::Optional<BodyExtent>>;
	using Resources = ecs::Resources<ecs::Read<Contacts>, ecs::Read<ToppleSettings>, ecs::Read<BlastWaves>, ecs::Read<TopplePushes>, ecs::Write<ToppleEvents>>;

	// Just short of flat, as the original.
	static constexpr Engine::Math::Fixed Limit = Engine::Math::Fixed::FromRatio(152170, 100000); // pi/2 - pi/64

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<ToppleEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		const Contacts &contacts = context.Read<Contacts>();
		const ToppleSettings &settings = context.Read<ToppleSettings>();
		const auto &pushes = context.Read<BlastWaves>().pushes;
		const auto &rulePushes = context.Read<TopplePushes>().list;
		auto &events = context.Write<ToppleEvents>().Slot(context);
		const std::uint64_t tick = context.Tick();
		auto topples = chunk.Get<Topple>();
		auto transforms = chunk.Get<Transform>();
		auto attitudes = chunk.Get<Attitude>();
		const auto dyings = chunk.Get<Dying>();
		const auto extents = chunk.Get<BodyExtent>();
		const auto entities = chunk.Entities();
		const auto kill = [&](ecs::Entity entity) {
			context.Commands().Set<Lifetime>(entity, Lifetime{tick, 0, settings.toppledDeathType});
		};
		for (std::size_t row = 0; row < topples.size(); ++row)
		{
			Topple &topple = topples[row];
			Transform &transform = transforms[row];
			if (topple.state == ToppleState::Upright)
			{
				if (!dyings.empty())
					continue;
				const auto start = [&](Engine::Math::FixedVector2 away, Fixed speed, std::uint32_t options) {
					if ((topple.flags & topple_flag::KillWhenStarting) != 0)
					{
						topple.state = ToppleState::Down;
						kill(entities[row]);
						return;
					}
					Start(topple, transform, away, speed, options);
					events.push_back({entities[row], ToppleEvent::Kind::Started, transform.position, transform.facing});
				};
				// NeutronMissileSlowDeathBehavior::doBlast: other->topple(missile to it, ToppleSpeed, NO_BOUNCE | NO_FX).
				for (const BlastPush &push : pushes)
					if (topple.state == ToppleState::Upright && BlastWaves::Reaches(push.center, push.radius, transform.position))
						start(transform.position.XY() - push.center.XY(), push.toppleSpeed, topple_option::NoBounce | topple_option::NoFx);
				// Object::topple from a game rule (WaveGuideUpdate::doDamage: a flood wave's victim, NO_BOUNCE | NO_FX).
				for (const TopplePush &push : rulePushes)
					if (topple.state == ToppleState::Upright && push.entity == entities[row])
						start(push.away, push.speed, push.options);
				if (topple.state != ToppleState::Upright)
					continue;
				for (const Contact &contact : contacts.For(entities[row]))
				{
					if (contact.crusherLevel <= 1)
						continue;
					start(transform.position.XY() - contact.moverPosition.XY(), contact.moverSpeed, 0);
					break;
				}
				continue;
			}
			if (topple.state != ToppleState::Falling)
				continue;
			if (topple.facingSteps > 0)
			{
				transform.facing = transform.facing + topple.facingStep;
				--topple.facingSteps;
			}
			Fixed step = topple.angularVelocity;
			if (topple.fallen + step > Limit)
				step = Limit - topple.fallen;
			topple.fallen += step;
			attitudes[row].pitch = Engine::Math::TurnFromRadians(topple.fallSign > 0 ? topple.fallen : Fixed{} - topple.fallen);
			if (topple.fallen >= Limit && topple.angularVelocity > Fixed{})
			{
				topple.angularVelocity = Fixed{} - topple.angularVelocity * topple.bounceVelocity;
				if ((topple.options & topple_option::NoBounce) != 0 || Engine::Math::Abs(topple.angularVelocity) < Fixed::FromRatio(1, 100))
				{
					topple.angularVelocity = {};
					topple.state = ToppleState::Down;
					if ((topple.flags & topple_flag::KillWhenDown) != 0)
					{
						kill(entities[row]);
						// ReorientToppledRubble: its separate rubble state upright and centred on where its top (its geometry's
						// height above its position, tilted as it lies) now is.
						if ((topple.flags & topple_flag::ReorientRubble) != 0 && !extents.empty())
						{
							const Fixed height = extents[row].maxHeight;
							const Engine::Math::TurnAngle tilt = Engine::Math::TurnFromRadians(topple.fallen);
							const auto toward = Engine::Math::Direction(transform.facing) * (Fixed::FromInt(topple.fallSign) * height * Engine::Math::Sin(tilt));
							transform.position = {transform.position.x + toward.x, transform.position.y + toward.y, transform.position.z + height * Engine::Math::Cos(tilt)};
							attitudes[row].pitch = {};
							attitudes[row].roll = {};
						}
					}
				}
				else if (Engine::Math::Abs(topple.angularVelocity) >= Fixed::FromRatio(3, 100) && (topple.options & topple_option::NoFx) == 0)
					events.push_back({entities[row], ToppleEvent::Kind::Bounced, transform.position, transform.facing});
			}
			else
				topple.angularVelocity += topple.angularAcceleration;
		}
	}

private:
	// How far apart two angles are (turn units, 0 to half a turn).
	static std::int64_t Apart(Engine::Math::TurnAngle a, Engine::Math::TurnAngle b)
	{
		const std::int64_t delta = static_cast<std::int32_t>((a - b).units);
		return delta < 0 ? -delta : delta;
	}

	// `away`: the way it is pushed; `speed`: how hard (the crusher's speed, a blast's topple speed).
	static void Start(Topple &topple, const Transform &transform, Engine::Math::FixedVector2 away, Engine::Math::Fixed speed, std::uint32_t options)
	{
		using Engine::Math::Fixed;
		using Engine::Math::TurnAngle;
		// adjustToppleDirection: a script's direction for this one, whatever pushed it.
		if (topple.scripted != 0)
			away = topple.scriptedDirection;
		if (away.x == Fixed{} && away.y == Fixed{})
			away = {Fixed::One(), Fixed{}};
		TurnAngle heading = Engine::Math::Heading(away);
		if ((topple.flags & topple_flag::LeftOrRightOnly) != 0)
		{
			// Only sideways from its facing: the side nearer the push.
			const TurnAngle left = transform.facing + TurnAngle::Quarter_Turn(), right = transform.facing - TurnAngle::Quarter_Turn();
			heading = Apart(heading, left) <= Apart(heading, right) ? left : right;
		}
		// Lie along the fall: turn to face it, or away from it, whichever is nearer (falling backwards then).
		const TurnAngle backwards = heading + TurnAngle::Half_Turn();
		const bool forwards = Apart(heading, transform.facing) <= Apart(backwards, transform.facing);
		const std::int32_t turn = static_cast<std::int32_t>(((forwards ? heading : backwards) - transform.facing).units);
		topple.fallSign = forwards ? 1 : -1;
		topple.angularVelocity = speed * topple.initialVelocity;
		topple.angularAcceleration = speed * topple.initialAcceleration;
		topple.options = options;
		if (topple.angularVelocity <= Fixed{})
			topple.angularVelocity = Fixed::FromRatio(1, 100);
		const std::int64_t steps = (Limit / (topple.angularVelocity * Fixed::FromInt(2))).Floor();
		topple.facingSteps = static_cast<std::uint32_t>(steps < 1 ? 1 : steps);
		topple.facingStep = TurnAngle{static_cast<std::uint32_t>(turn / static_cast<std::int32_t>(topple.facingSteps))};
		topple.fallen = {};
		topple.state = ToppleState::Falling;
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::ToppleSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.topple";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::ContactSystem>;
};
}
