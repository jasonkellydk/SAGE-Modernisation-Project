export module games.renegade.gameplay.missions.systems.mission_command_system;
import std;
export import games.renegade.gameplay.missions.resources.mission_requests;
export import games.renegade.content.campaign.behavior_programs;
export import games.renegade.session.scene_state;
export import engine.gameplay.common.scripts.systems.lua_behavior_system;
export import engine.gameplay.common.scripts.systems.behavior_attachment_system;
export import games.renegade.gameplay.missions.components.conversation_state;
export import games.renegade.gameplay.missions.resources.conversation_library;
import engine.gameplay.common.timing.components.timeline_playback;
export import games.renegade.gameplay.missions.resources.animation_library;
import engine.gameplay.common.appearance.systems.clip_request_system;
export import games.renegade.gameplay.missions.resources.routes;
export import engine.gameplay.common.input.systems.input_permission_system;
export import engine.gameplay.common.spatial.systems.position_request_system;
export import engine.gameplay.common.spatial.systems.rigid_heading_system;
export import games.renegade.gameplay.missions.resources.camera_requests;

export namespace renegade {
struct MissionCommandSystem {
    using Query=ecs::Query<ecs::Read<engine::gameplay::LuaBehavior>>;
    using Lookup=ecs::Lookup<ecs::Read<HumanGoto>,ecs::Read<engine::gameplay::InputPermission>,ecs::Read<engine::gameplay::Transform>>;
    using Resources=ecs::Resources<ecs::Read<engine::gameplay::BehaviorEmissions>,ecs::Read<engine::gameplay::BehaviorPrograms>,ecs::Write<engine::gameplay::BehaviorAttachments>,ecs::Write<engine::gameplay::InputPermissionRequests>,ecs::Write<engine::gameplay::PositionRequests>,ecs::Write<engine::gameplay::HeadingTargets>,ecs::Write<CameraLookRequests>,ecs::Read<SceneState>,ecs::Read<ConversationLibrary>,ecs::Read<MissionAnimationLibrary>,ecs::Read<MissionRoutes>,ecs::Write<HumanGotoRequests>,ecs::Write<engine::gameplay::ClipRequests>,ecs::Write<MissionRequests>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {
        context.Write<MissionRequests>().Reset(query.PreparedChunkCount());context.Write<engine::gameplay::ClipRequests>().Reset(query.PreparedChunkCount());
        context.Write<HumanGotoRequests>().Reset(query.PreparedChunkCount());
        context.Write<engine::gameplay::BehaviorAttachments>().Reset(query.PreparedChunkCount());
        context.Write<engine::gameplay::InputPermissionRequests>().Reset(query.PreparedChunkCount());
        context.Write<engine::gameplay::PositionRequests>().Reset(query.PreparedChunkCount());
        context.Write<engine::gameplay::HeadingTargets>().Reset(query.PreparedChunkCount());
        context.Write<CameraLookRequests>().Reset(query.PreparedChunkCount());
    }
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        using namespace engine::gameplay;const auto entities=chunk.Entities();const auto bindings=chunk.Get<LuaBehavior>();
        const auto& scene=context.Read<SceneState>();auto& pending=context.Write<MissionRequests>().Slot(context);
        for(std::size_t row=0;row<bindings.size();++row) {
            std::uint64_t ordinal{};
            context.Read<BehaviorEmissions>().ForEach([&](const auto& emission) {
                if(emission.observer!=entities[row]) return;
                const auto sequence=(bindings[row].order<<32)+ordinal++;
                const auto& command=emission.command;
                const auto integer=[&](std::size_t i) {
                    if(i>=command.arguments.size() || !std::holds_alternative<std::int64_t>(command.arguments[i])) throw std::invalid_argument(command.name+": expected integer argument");
                    return std::get<std::int64_t>(command.arguments[i]);
                };
                const auto recipient=[&](std::int64_t id) {
                    if(id==1) return scene.player;
                    const auto found=scene.authored_entities.find(std::uint64_t(id));return found==scene.authored_entities.end() ? ecs::Entity{} : found->second;
                };
                const auto scalar=[&](std::size_t i) {
                    if(i>=command.arguments.size() || !std::holds_alternative<std::string>(command.arguments[i])) throw std::invalid_argument(command.name+": expected decimal argument");
                    const auto value=Engine::Math::Fixed::ParseDecimal(std::get<std::string>(command.arguments[i]));
                    if(!value) throw std::invalid_argument(command.name+": invalid decimal argument");return *value;
                };
                const auto schedule=[&](BehaviorMessage message,std::int64_t milliseconds) {
                    if(milliseconds<0 || std::uint64_t(milliseconds)>1000000000ull) throw std::out_of_range("mission event delay");
                    message.due_tick=context.Tick()+(std::uint64_t(milliseconds)*context.Time().Step().TicksPerSecond()+999)/1000;
                    message.order=sequence;const auto timer=context.Commands().Create();context.Commands().Add<ScheduledBehaviorMessage>(timer,ScheduledBehaviorMessage{message});
                };
                if(command.name=="Camera_Look_Actor") {
                    if(command.arguments.size()!=2) throw std::invalid_argument("invalid actor camera request");
                    const auto actor=recipient(integer(0));const auto offset=scalar(1);
                    if(!context.Lookup<Lookup>().Get<Transform>(actor)) {auto request=emission;request.order=sequence;pending.push_back(std::move(request));return;}
                    context.Write<CameraLookRequests>().Slot(context).push_back({actor,{},offset,sequence,true});
                } else if(command.name=="Force_Camera_Look") {
                    if(command.arguments.size()!=3) throw std::invalid_argument("invalid camera point request");
                    const Engine::Math::FixedVector3 point{scalar(0),scalar(1),scalar(2)};
                    context.Write<CameraLookRequests>().Slot(context).push_back({{},point,{},sequence,false});
                } else if(command.name=="Face_Actor") {
                    if(command.arguments.size()!=2) throw std::invalid_argument("invalid actor facing request");
                    const auto subject=recipient(integer(0)),target=recipient(integer(1));const auto lookup=context.Lookup<Lookup>();
                    if(!lookup.Get<Transform>(subject) || !lookup.Get<Transform>(target)) {auto request=emission;request.order=sequence;pending.push_back(std::move(request));return;}
                    // Mission00.cpp Say_Something uses -90 - atan2(dx,dy).
                    // At coincident XY this explicitly resolves to -90 degrees.
                    HeadingTargetRequest request;request.subject=subject;request.target=target;request.order=sequence;
                    request.apply_coincident=true;request.coincident_heading=Engine::Math::TurnFromDegrees(-90);
                    context.Write<HeadingTargets>().Slot(context).push_back(request);
                } else if(command.name=="Destroy_Object") {
                    if(command.arguments.size()!=1) throw std::invalid_argument("invalid object destruction");
                    const auto subject=recipient(integer(0));if(context.Lookup<Lookup>().IsAlive(subject)) context.Commands().Destroy(subject);
                } else if(command.name=="Set_Position") {
                    if(command.arguments.size()!=4) throw std::invalid_argument("invalid object relocation");
                    const auto subject=recipient(integer(0));const Engine::Math::FixedVector3 position{scalar(1),scalar(2),scalar(3)};
                    if(!context.Lookup<Lookup>().Get<Transform>(subject)) {auto request=emission;request.order=sequence;pending.push_back(std::move(request));return;}
                    context.Write<PositionRequests>().Slot(context).push_back({subject,position,sequence});
                } else if(command.name=="Attach_Script") {
                    if(command.arguments.size()!=3 || !std::holds_alternative<std::string>(command.arguments[1]) || !std::holds_alternative<std::string>(command.arguments[2]))
                        throw std::invalid_argument("invalid script attachment");
                    const auto subject=recipient(integer(0));if(!context.Lookup<Lookup>().IsAlive(subject)) return;
                    const auto& programs=context.Read<BehaviorPrograms>().programs;
                    const auto program=std::ranges::find(programs,std::get<std::string>(command.arguments[1]),&BehaviorProgram::name);
                    if(program==programs.end()) {auto request=emission;request.order=sequence;pending.push_back(std::move(request));return;}
                    context.Write<BehaviorAttachments>().Slot(context).push_back({subject,std::uint64_t(integer(0)),sequence,std::uint32_t(program-programs.begin()),
                        {integer(0),std::get<std::string>(command.arguments[2])}});
                } else if(command.name=="Control_Enable") {
                    if(command.arguments.size()!=2 || (integer(1)!=0 && integer(1)!=1)) throw std::invalid_argument("invalid input permission");
                    const auto subject=recipient(integer(0));
                    if(!context.Lookup<Lookup>().Get<InputPermission>(subject)) {auto request=emission;request.order=sequence;pending.push_back(std::move(request));return;}
                    context.Write<InputPermissionRequests>().Slot(context).push_back({subject,sequence,std::uint32_t(integer(1))});
                } else if(command.name=="Start_Timer") {
                    if(command.arguments.size()!=3 || integer(0)!=std::int64_t(bindings[row].authored_subject)) throw std::invalid_argument("invalid mission timer owner");
                    BehaviorMessage message;message.recipient=bindings[row].subject;message.observer=entities[row];message.event=std::uint32_t(content::MissionEvent::TimerExpired);
                    message.arguments={integer(0),integer(2),0,0};message.argument_count=2;schedule(message,integer(1));
                } else if(command.name=="Send_Custom_Event") {
                    if(command.arguments.size()!=5) throw std::invalid_argument("invalid custom event");
                    BehaviorMessage message;message.recipient=recipient(integer(1));if(!message.recipient.IsValid()) return;
                    message.event=std::uint32_t(content::MissionEvent::Custom);message.arguments={integer(1),integer(2),integer(3),integer(0)};message.argument_count=4;schedule(message,integer(4));
                } else if(command.name=="Action_Goto_Waypath") {
                    if(command.arguments.size()!=6) throw std::invalid_argument("invalid mission waypath action");
                    const auto subject=recipient(integer(0));const auto id=integer(1);const auto speed=scalar(2),distance=scalar(3);
                    if(speed<Engine::Math::Fixed{} || speed>Engine::Math::Fixed::One() || distance<Engine::Math::Fixed{} || integer(4)<0 || integer(4)>0xffffffffll || integer(5)<0 || integer(5)>0xffffffffll)
                        throw std::invalid_argument("invalid mission waypath action parameters");
                    const auto& routes=context.Read<MissionRoutes>().routes;const auto route=std::ranges::find(routes,id,&MissionRoute::id);
                    if(route==routes.end() || !route->supported || !context.Lookup<Lookup>().Get<HumanGoto>(subject)) {auto request=emission;request.order=sequence;pending.push_back(std::move(request));return;}
                    HumanGoto action{entities[row],std::uint64_t(integer(0)),sequence,speed,distance,std::uint32_t(integer(5)),std::uint32_t(integer(4)),1};
                    context.Write<HumanGotoRequests>().Slot(context).push_back({subject,action,route->traversal});
                } else if(command.name=="Set_Animation_Frame" || command.name=="Set_Animation") {
                    const bool manual=command.name=="Set_Animation_Frame";
                    if(command.arguments.size()!=(manual ? 3u : 5u) || !std::holds_alternative<std::string>(command.arguments[1])) throw std::invalid_argument("invalid mission animation request");
                    const auto subject=recipient(integer(0));const auto& name=std::get<std::string>(command.arguments[1]);
                    const auto& library=context.Read<MissionAnimationLibrary>().bindings;
                    const auto binding=std::ranges::find_if(library,[&](const auto& entry) {return entry.subject==subject && entry.name==name;});
                    if(binding==library.end()) {auto request=emission;request.order=sequence;pending.push_back(std::move(request));return;}
                    auto playback=binding->playback;const auto end=Engine::Math::Fixed::FromInt(playback.frame_count-1);
                    playback.frame=std::clamp(Engine::Math::Fixed::FromInt(integer(manual ? 2 : 3)),Engine::Math::Fixed{},end);
                    if(manual) {playback.mode=ClipMode::Hold;playback.target=playback.frame;}
                    else {
                        const auto target=integer(4);playback.target=target<0 ? end : std::clamp(Engine::Math::Fixed::FromInt(target),Engine::Math::Fixed{},end);
                        playback.mode=integer(2) ? ClipMode::Loop : ClipMode::Target;
                    }
                    context.Write<ClipRequests>().Slot(context).push_back({subject,playback,sequence});
                } else if(command.name=="Start_Conversation") {
                    if(command.arguments.size()!=5 || !std::holds_alternative<std::string>(command.arguments[1])) throw std::invalid_argument("invalid conversation request");
                    const auto& definitions=context.Read<ConversationLibrary>().definitions;
                    const auto definition=std::ranges::find(definitions,std::get<std::string>(command.arguments[1]),&ConversationPlaybackDefinition::name);
                    if(definition==definitions.end()) {pending.push_back(emission);return;}
                    const auto clock=context.Commands().Create();TimelinePlayback playback;playback.timeline=definition->timeline;
                    context.Commands().Add<TimelinePlayback>(clock,playback);
                    context.Commands().Add<ConversationState>(clock,ConversationState{entities[row],bindings[row].subject,scene.player,
                        bindings[row].authored_subject,std::uint32_t(definition-definitions.begin()),std::uint32_t(integer(4)),context.Tick(),sequence});
                } else {auto request=emission;request.order=sequence;pending.push_back(std::move(request));}
            });
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<renegade::MissionCommandSystem> {
    static constexpr std::string_view StableName="renegade.mission_commands";
    static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<engine::gameplay::ClipRequestSystem>;using After=SystemTypeList<engine::gameplay::LuaBehaviorSystem>;
};
}
