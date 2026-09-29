export module games.generalszh.gameplay.containment.systems.scripted_evacuation_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.containment.components.scripted_evacuation;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.rts.containment.systems.unloading_system;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.navigation.components.navigation;

// Steps the transports a script sent to let their riders out (ScriptedEvacuation), chunk-parallel, each tick before the
// transports unload and move: as the original's AI update runs before its physics, a stage sees where the last tick's
// movement left its transport. A stage's end enters the next at once (its onEnter), whose update waits for the next tick.
//   Moving: its move over (arrived, or given up), the generic machine has its riders with an AI asked out (aiEvacuate:
//     orderAllPassengersToExit, CMD_FROM_AI: out one at a time through its door) and its team made active; then, leaving,
//     it heads back where it started, else the machine ends. A Chinook's machine lands instead. Another order (a player's,
//     where the machine is not locked) ends the machine.
//   Landing (the Chinook's): down (within LandedHeight of the ground, on its landing spot), everyone aboard is out at once
//     (removeAllContained) and its team made active; it takes off again.
//   TakingOff: back up, it heads off (leaving) or the machine ends.
//   Leaving: its move over, it is removed.
//   HeadingOff: while its temporary move lasts it flies straight for where it was made; once that is over (arrived, or
//     20 seconds on) it is removed as soon as it is outside the map, border included.
// What reaches beyond the transport (its team, its removal) goes out as ScriptedEvacuationEvents, applied after the step;
// the riders' exits are its commands, seen by the unloading after it.
export namespace generalszh::gameplay
{
struct ScriptedEvacuationSystem
{
	using Query = ecs::Query<ecs::Write<ScriptedEvacuation>, ecs::Write<engine::gameplay::MoveOrder>, ecs::Read<engine::gameplay::Transform>,
		ecs::OptionalWrite<engine::gameplay::Transport>, ecs::Optional<engine::gameplay::Disabled>, ecs::OptionalWrite<engine::gameplay::Route>,
		ecs::Exclude<engine::gameplay::Dying>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::MoveOrder>, ecs::Read<engine::gameplay::Locomotion>, ecs::Read<engine::gameplay::AiActivity>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::CargoManifest>, ecs::Read<engine::gameplay::GroundHeight>, ecs::Write<ScriptedEvacuationEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<ScriptedEvacuationEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using Kind = ScriptedEvacuationEvent::Kind;
		const gp::CargoManifest &manifest = context.Read<gp::CargoManifest>();
		const gp::GroundHeight &ground = context.Read<gp::GroundHeight>();
		auto &out = context.Write<ScriptedEvacuationEvents>().Slot(context);
		auto &commands = context.Commands();
		const auto lookup = context.Lookup<Lookup>();
		auto evacuations = chunk.Get<ScriptedEvacuation>();
		auto orders = chunk.Get<gp::MoveOrder>();
		const auto transforms = chunk.Get<gp::Transform>();
		auto transports = chunk.Get<gp::Transport>();
		const auto disabled = chunk.Get<gp::Disabled>();
		auto routes = chunk.Get<gp::Route>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		for (std::size_t row = 0; row < evacuations.size(); ++row)
		{
			ScriptedEvacuation &evacuation = evacuations[row];
			gp::MoveOrder &order = orders[row];
			gp::Transport *transport = transports.empty() ? nullptr : &transports[row];
			const ecs::Entity self = entities[row];
			const Engine::Math::FixedVector3 &at = transforms[row].position;
			const auto end = [&] { commands.Remove<ScriptedEvacuation>(self); };
			// A new move: its route planned afresh (as aiMoveToPosition), even to where it went before.
			const auto replan = [&] {
				if (!routes.empty())
					routes[row].planned = false;
			};
			// Still on the move this machine gave (none other replaced it).
			const auto ours = [&](gp::MoveMode mode, Engine::Math::FixedVector2 goal) { return order.mode == mode && order.destination == goal; };
			switch (evacuation.stage)
			{
			case EvacuationStage::Moving:
			{
				if (ours(gp::MoveMode::Point, evacuation.destination))
					break;
				if (order.mode != gp::MoveMode::Idle)
				{
					end();
					break;
				}
				if (evacuation.chinook != 0)
				{
					// LAND_AND_EVAC: ChinookTakeoffOrLandingState(landing).
					if (transport != nullptr)
						transport->landRequested = true;
					evacuation.stage = EvacuationStage::Landing;
					break;
				}
				// aiEvacuate(FALSE, CMD_FROM_AI): not while subdued; each rider with an AI asked out (aiExit).
				const bool subdued = !disabled.empty() && (disabled[row].mask & gp::disabled_type::Subdued) != 0;
				if (!subdued)
					for (const ecs::Entity rider : manifest.Aboard(self))
					{
						if (lookup.Get<gp::MoveOrder>(rider) == nullptr && lookup.Get<gp::Locomotion>(rider) == nullptr)
							continue;
						commands.Set<gp::ExitIntent>(rider, gp::ExitIntent{self, 0u, 0u});
						if (const gp::AiActivity *activity = lookup.Get<gp::AiActivity>(rider); activity != nullptr && activity->commanded != 0)
						{
							gp::AiActivity own = *activity;
							own.commanded = 0;
							commands.Set<gp::AiActivity>(rider, own);
						}
					}
				out.push_back({self, Kind::ActivateTeam});
				if (evacuation.exits == 0)
				{
					end();
					break;
				}
				// AI_MOVE_AND_DELETE: back to where it started (setGoalPosition(m_origin)), invalid positions allowed.
				evacuation.stage = EvacuationStage::Leaving;
				order = gp::MoveToPoint(evacuation.origin);
				replan();
				break;
			}
			case EvacuationStage::Landing:
			{
				if (transport == nullptr)
				{
					end();
					break;
				}
				const bool down = transport->landing && !transport->seekingSpot && at.z - ground.Surface(at.XY()) <= gp::LandedHeight();
				if (!down)
					break;
				// EVAC_AND_TAKEOFF: ChinookEvacuateState::onEnter (removeAllContained(FALSE), setActive), then TAKING_OFF.
				for (const ecs::Entity rider : manifest.Aboard(self))
					commands.Set<gp::ExitIntent>(rider, gp::ExitIntent{self, 1u, 0u});
				out.push_back({self, Kind::ActivateTeam});
				transport->landRequested = false;
				evacuation.stage = EvacuationStage::TakingOff;
				break;
			}
			case EvacuationStage::TakingOff:
				if (transport != nullptr && (transport->takingOff || transport->landing))
					break;
				if (evacuation.exits == 0)
				{
					end();
					break;
				}
				// HEAD_OFF_MAP: aiMoveToPosition(original position, CMD_FROM_AI) from a busy state: a temporary AI_MOVE_TO
				// of at most 20 seconds, the destination as given (OBJECT_STATUS_RIDER8), invalid positions allowed.
				evacuation.stage = EvacuationStage::HeadingOff;
				evacuation.moveEnds = tick + 20 * 30;
				order = gp::MoveStraightTo(evacuation.origin);
				replan();
				break;
			case EvacuationStage::Leaving:
				if (ours(gp::MoveMode::Point, evacuation.origin))
					break;
				if (order.mode == gp::MoveMode::Idle)
					out.push_back({self, Kind::Remove});
				else
					end();
				break;
			case EvacuationStage::HeadingOff:
			{
				if (evacuation.moveEnds != 0)
				{
					const bool moving = ours(gp::MoveMode::Direct, evacuation.origin);
					if (!moving && order.mode != gp::MoveMode::Idle)
					{
						end();
						break;
					}
					// The temporary state's update: on while it moves and has time left (m_temporaryStateFramEnd).
					if (moving && tick <= evacuation.moveEnds)
						break;
					if (moving)
						order.mode = gp::MoveMode::Idle;
					evacuation.moveEnds = 0;
				}
				else if (order.mode != gp::MoveMode::Idle)
				{
					end();
					break;
				}
				const auto [low, high] = ground.ExtentIncludingBorder();
				if (at.x < low.x || at.y < low.y || at.x > high.x || at.y > high.y)
					out.push_back({self, Kind::Remove});
				break;
			}
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::ScriptedEvacuationSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.scripted_evacuation";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::UnloadingSystem>;
	using After = SystemTypeList<>;
};
}
