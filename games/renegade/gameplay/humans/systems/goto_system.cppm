export module games.renegade.gameplay.humans.systems.goto_system;
import std;
export import games.renegade.gameplay.humans.components.motion;
export import games.renegade.gameplay.missions.resources.routes;
import games.renegade.content.campaign.behavior_programs;
import engine.gameplay.common.scripts.components.scheduled_message;

export namespace renegade {
inline void CompleteHumanGoto(ecs::SystemContext& context,ecs::Entity subject,const HumanGoto& action,std::int64_t reason) {
	engine::gameplay::BehaviorMessage message; message.recipient=subject;message.observer=action.observer;
	message.event=std::uint32_t(content::MissionEvent::ActionComplete);message.due_tick=context.Tick()+1;message.order=action.order;
	message.arguments={std::int64_t(action.authored_subject),std::int64_t(action.action),reason,0};message.argument_count=3;
	const auto timer=context.Commands().Create();context.Commands().Add<engine::gameplay::ScheduledBehaviorMessage>(timer,engine::gameplay::ScheduledBehaviorMessage{message});
}
struct HumanGotoRequestSystem {
	using Query=ecs::Query<ecs::Write<HumanGoto>,ecs::Write<engine::gameplay::RouteTraversal>>;
	using Resources=ecs::Resources<ecs::Read<HumanGotoRequests>>;
	void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
		const auto entities=chunk.Entities();const auto actions=chunk.Get<HumanGoto>();const auto routes=chunk.Get<engine::gameplay::RouteTraversal>();
		for(std::size_t row=0;row<actions.size();++row) {
			std::vector<HumanGotoRequest> requests;
			context.Read<HumanGotoRequests>().ForEach([&](const auto& request) {if(request.subject==entities[row]) requests.push_back(request);});
			std::ranges::sort(requests,{},[](const auto& request) {return request.action.order;});
			for(const auto& request:requests) {
				if(actions[row].enabled && request.action.priority<actions[row].priority) {CompleteHumanGoto(context,entities[row],request.action,1);continue;}
				if(actions[row].enabled) CompleteHumanGoto(context,entities[row],actions[row],1);
				actions[row]=request.action;routes[row]=request.traversal;
			}
		}
	}
};
struct HumanGotoControlSystem {
	using Query=ecs::Query<ecs::Read<engine::gameplay::Transform>,ecs::Read<HumanMovement>,ecs::Write<HumanControl>,
		ecs::Write<HumanGoto>,ecs::Write<engine::gameplay::RouteTraversal>,ecs::Read<engine::gameplay::RouteSteering>>;
	using Resources=ecs::Resources<ecs::Read<engine::gameplay::RouteCurves>>;
	void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
		using namespace Engine::Math;using namespace engine::gameplay;
		const auto entities=chunk.Entities();const auto poses=chunk.Get<Transform>();const auto presets=chunk.Get<HumanMovement>();
		const auto controls=chunk.Get<HumanControl>();const auto actions=chunk.Get<HumanGoto>();const auto routes=chunk.Get<RouteTraversal>();const auto targets=chunk.Get<RouteSteering>();
		for(std::size_t row=0;row<poses.size();++row) {
			auto& action=actions[row];if(!action.enabled) continue;
			auto& control=controls[row];control.forward={};control.left={};control.jump=0;control.facing=poses[row].facing;
			const auto& target=targets[row];if(!target.valid) continue;
			const auto delta=target.target-poses[row].position;const auto range=Length(delta.XY());
			bool facing=true;
			if(range>Fixed::FromRatio(1,10)) {
				const auto goal=Atan2(delta.y,delta.x);const auto difference=std::bit_cast<std::int32_t>(goal.units-poses[row].facing.units);
				if(std::abs(std::int64_t(difference))>TurnFromRadians(Fixed::FromRatio(1,1000)).units) {
					auto change=presets[row].definition.turn_rate*context.Time().SecondsPerTick();
					if(std::abs(std::int64_t(difference))<TurnFromDegrees(20).units) change*=Fixed::FromRatio(3,10);
					const auto maximum=std::int64_t(TurnFromRadians(change).units);
					const auto applied=std::clamp(std::int64_t(difference),-maximum,maximum);
					control.facing.units=poses[row].facing.units+std::uint32_t(applied);facing=applied==difference;
				}
			}
			const auto& route=routes[row];const auto& curve=context.Read<RouteCurves>().curves.at(route.curve-1);
			const auto remaining=route.looping ? curve.length : curve.length*(route.end-route.parameter)/route.end;
			if(remaining+range<=action.arrived_distance) {
				if(facing) {CompleteHumanGoto(context,entities[row],action,0);action.enabled=0;routes[row].enabled=0;}
				continue;
			}
			const auto cosine=Cos(control.facing),sine=Sin(control.facing);
			FixedVector2 local{delta.x*cosine+delta.y*sine,-delta.x*sine+delta.y*cosine};
			const auto bearing=Atan2(local.y,local.x);
			if(!facing && std::abs(std::int64_t(std::bit_cast<std::int32_t>(bearing.units)))>=TurnFromDegrees(40).units) continue;
			const auto rate=presets[row].definition.speed*action.speed*context.Time().SecondsPerTick();
			if(rate>Fixed::FromRatio(1,10000)) local=local/rate;
			const auto length=Length(local);if(length>Fixed::One() || rate<=Fixed::FromRatio(1,10000)) {if(length>Fixed{}) local=local/length;}
			control.forward=local.x*action.speed;control.left=local.y*action.speed;
		}
	}
};
}
export namespace ecs {
template<> struct SystemTraits<renegade::HumanGotoRequestSystem> {
	static constexpr std::string_view StableName="renegade.human_goto_requests";
	static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
	using Before=SystemTypeList<>;using After=SystemTypeList<>;
};
template<> struct SystemTraits<renegade::HumanGotoControlSystem> {
	static constexpr std::string_view StableName="renegade.human_goto_controls";
	static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PreSimulation;
	using Before=SystemTypeList<>;using After=SystemTypeList<engine::gameplay::RouteTraversalSystem>;
};
}
