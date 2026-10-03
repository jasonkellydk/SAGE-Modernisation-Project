export module games.renegade.gameplay.missions.systems.objective_system;
import std;
export import games.renegade.gameplay.missions.components.objective;
export import games.renegade.gameplay.missions.resources.mission_requests;
import games.renegade.session.scene_state;

export namespace renegade {
enum class ObjectiveOperationKind {Add,Remove,Status,Type,Point,Target,Hud,HudPoint};
struct ObjectiveOperation {
    ObjectiveOperationKind kind{};std::int32_t id{};MissionObjective value;
    engine::gameplay::TrackedPosition anchor;
};
struct ObjectiveTransaction {
    std::vector<ObjectiveOperation> operations;
    ecs::ChunkOutputs<std::int32_t> existing;
};
struct ObjectiveBundle {MissionObjective value;engine::gameplay::SimulationAge age;engine::gameplay::TrackedPosition anchor;};
}
export namespace ecs {
template<> struct ResourceTraits<renegade::ObjectiveTransaction> {static constexpr std::string_view StableName="renegade.objective_transaction";};
}
export namespace renegade {
// Combat/objectives.cpp Add/Remove/Set_Status/Radar/HUD. Each objective is an
// archetype row. Ordered script operations reduce independently per ID; new
// and removed rows commit at the ECS barrier, including remove/add in one tick.
struct ObjectiveSystem {
    using Query=ecs::Query<ecs::Write<MissionObjective>,ecs::Write<engine::gameplay::SimulationAge>,ecs::Write<engine::gameplay::TrackedPosition>>;
    using Resources=ecs::Resources<ecs::Read<MissionRequests>,ecs::Read<SceneState>,ecs::Write<ObjectiveVocabulary>,ecs::Write<ObjectiveTransaction>,ecs::Write<ObjectiveChanges>>;
    using Lookup=ecs::Lookup<ecs::Read<engine::gameplay::Transform>>;
    void BeforeChunks(Query& query,ecs::SystemContext& context) const {
        auto& plan=context.Write<ObjectiveTransaction>();plan.operations.clear();plan.existing.Reset(query.PreparedChunkCount());
        // Include a separate finalization slot for newly created IDs.
        context.Write<ObjectiveChanges>().Reset(query.PreparedChunkCount()+1);
        auto& vocabulary=context.Write<ObjectiveVocabulary>();std::vector<engine::gameplay::BehaviorEmission> requests;
        context.Read<MissionRequests>().AppendTo(requests);std::ranges::stable_sort(requests,{},&engine::gameplay::BehaviorEmission::order);
        for(const auto& request:requests) {
            const auto& command=request.command;const auto& name=command.name;ObjectiveOperation op;
            if(name=="Add_Objective") op.kind=ObjectiveOperationKind::Add;
            else if(name=="Remove_Objective") op.kind=ObjectiveOperationKind::Remove;
            else if(name=="Set_Objective_Status") op.kind=ObjectiveOperationKind::Status;
            else if(name=="Change_Objective_Type") op.kind=ObjectiveOperationKind::Type;
            else if(name=="Set_Objective_Radar_Blip") op.kind=ObjectiveOperationKind::Point;
            else if(name=="Set_Objective_Radar_Blip_Object") op.kind=ObjectiveOperationKind::Target;
            else if(name=="Set_Objective_HUD_Info") op.kind=ObjectiveOperationKind::Hud;
            else if(name=="Set_Objective_HUD_Info_Position") op.kind=ObjectiveOperationKind::HudPoint;
            else continue;
            const auto integer=[&](std::size_t at)->std::int64_t {
                if(at>=command.arguments.size() || !std::holds_alternative<std::int64_t>(command.arguments[at])) throw std::invalid_argument(name+": integer required");
                return std::get<std::int64_t>(command.arguments[at]);
            };
            const auto word=[&](std::size_t at)->std::uint32_t {const auto value=integer(at);if(value<0 || value>0xffffffffll) throw std::out_of_range(name+": identifier range");return std::uint32_t(value);};
            const auto text=[&](std::size_t at)->const std::string& {
                if(at>=command.arguments.size() || !std::holds_alternative<std::string>(command.arguments[at])) throw std::invalid_argument(name+": string required");return std::get<std::string>(command.arguments[at]);
            };
            const auto number=[&](std::size_t at) {
                if(at<command.arguments.size() && std::holds_alternative<std::int64_t>(command.arguments[at])) return Engine::Math::Fixed::FromInt(integer(at));
                const auto parsed=Engine::Math::Fixed::ParseDecimal(text(at));if(!parsed) throw std::invalid_argument(name+": decimal required");return *parsed;
            };
            const auto translated=[&](std::size_t at) {
                if(at<command.arguments.size() && std::holds_alternative<std::int64_t>(command.arguments[at])) return word(at);
                const auto found=vocabulary.translations.find(text(at));if(found==vocabulary.translations.end()) throw std::invalid_argument(name+": unknown translation "+text(at));return found->second;
            };
            const auto type=[&](std::size_t at) {const auto value=word(at);if(value<1 || value>3) throw std::invalid_argument(name+": objective type");return static_cast<ObjectiveType>(value);};
            const auto status=[&](std::size_t at) {const auto value=word(at);if(value>3) throw std::invalid_argument(name+": objective status");return static_cast<ObjectiveStatus>(value);};
            const auto id=integer(0);if(id<std::numeric_limits<std::int32_t>::min() || id>std::numeric_limits<std::int32_t>::max()) throw std::out_of_range(name+": objective ID");
            op.id=std::int32_t(id);op.value.id=op.id;
            const auto count=command.arguments.size();
            if(op.kind==ObjectiveOperationKind::Add) {
                if(count!=6) throw std::invalid_argument(name+": argument count");op.value.type=type(1);op.value.status=status(2);
                op.value.short_text=translated(3);op.value.description_sound=vocabulary.Intern(text(4));op.value.long_text=translated(5);
            } else if(op.kind==ObjectiveOperationKind::Remove) {if(count!=1) throw std::invalid_argument(name+": argument count");}
            else if(op.kind==ObjectiveOperationKind::Status || op.kind==ObjectiveOperationKind::Type) {
                if(count!=2) throw std::invalid_argument(name+": argument count");if(op.kind==ObjectiveOperationKind::Status) op.value.status=status(1);else op.value.type=type(1);
            } else if(op.kind==ObjectiveOperationKind::Point) {
                if(count!=4) throw std::invalid_argument(name+": argument count");op.anchor.point={number(1),number(2),number(3)};
            } else if(op.kind==ObjectiveOperationKind::Target) {
                if(count!=2) throw std::invalid_argument(name+": argument count");const auto actor=integer(1);const auto& scene=context.Read<SceneState>();
                if(actor==1) op.anchor.target=scene.player;
                else if(const auto found=scene.authored_entities.find(std::uint64_t(actor));found!=scene.authored_entities.end()) op.anchor.target=found->second;
                if(!context.Lookup<Lookup>().Get<engine::gameplay::Transform>(op.anchor.target)) op.anchor.target={};
            } else {
                if(count!=(op.kind==ObjectiveOperationKind::HudPoint ? 7u : 4u)) throw std::invalid_argument(name+": argument count");
                op.value.priority=number(1);op.value.pog=vocabulary.Intern(text(2));op.value.hud_message=translated(3);
                if(count==7) op.anchor.point={number(4),number(5),number(6)};
            }
            plan.operations.push_back(op);
        }
    }
    static std::optional<ObjectiveBundle> Reduce(std::int32_t id,std::optional<ObjectiveBundle> current,const std::vector<ObjectiveOperation>& operations,std::vector<ObjectiveChange>& notices,bool& replaced) {
        for(const auto& op:operations) if(op.id==id) {
            if(op.kind==ObjectiveOperationKind::Add) {
                if(current) continue;current=ObjectiveBundle{op.value};current->age.enabled=op.value.status!=ObjectiveStatus::Hidden;replaced=true;
                if(op.value.status!=ObjectiveStatus::Hidden) notices.push_back({op.value,ObjectiveNotice::Added});
            } else if(op.kind==ObjectiveOperationKind::Remove) {
                if(current) notices.push_back({current->value,ObjectiveNotice::Cancelled});current.reset();
            } else if(current) {
                auto& value=current->value;
                if(op.kind==ObjectiveOperationKind::Status) {
                    const bool unhide=value.status==ObjectiveStatus::Hidden && op.value.status!=ObjectiveStatus::Hidden;
                    value.status=op.value.status;current->age.enabled=value.status!=ObjectiveStatus::Hidden;
                    if(unhide) current->age.ticks=0;
                    notices.push_back({value,unhide && value.status!=ObjectiveStatus::Accomplished ? ObjectiveNotice::Added : value.status==ObjectiveStatus::Hidden ? ObjectiveNotice::Refreshed : ObjectiveNotice::StatusChanged});
                } else if(op.kind==ObjectiveOperationKind::Type) value.type=op.value.type;
                else if(op.kind==ObjectiveOperationKind::Point) {current->anchor.target={};current->anchor.point=op.anchor.point;value.draw_blip=1;}
                else if(op.kind==ObjectiveOperationKind::Target) current->anchor.target=op.anchor.target;
                else {
                    value.priority=op.value.priority;value.pog=op.value.pog;value.hud_message=op.value.hud_message;
                    // HUD-position changes do not detach an actor radar target.
                    if(op.kind==ObjectiveOperationKind::HudPoint) current->anchor.point=op.anchor.point;
                }
            }
        }return current;
    }
    static void Create(ecs::SystemContext& context,const ObjectiveBundle& value) {
        const auto entity=context.Commands().Create();context.Commands().Add<MissionObjective>(entity,value.value);
        context.Commands().Add<engine::gameplay::SimulationAge>(entity,value.age);context.Commands().Add<engine::gameplay::TrackedPosition>(entity,value.anchor);
    }
    void Execute(Query::Chunk chunk,ecs::SystemContext& context) const {
        const auto values=chunk.Get<MissionObjective>();const auto ages=chunk.Get<engine::gameplay::SimulationAge>();const auto anchors=chunk.Get<engine::gameplay::TrackedPosition>();const auto entities=chunk.Entities();
        auto& existing=context.Write<ObjectiveTransaction>().existing.Slot(context);auto& notices=context.Write<ObjectiveChanges>().Slot(context);
        const auto& operations=context.Read<ObjectiveTransaction>().operations;
        for(std::size_t row=0;row<values.size();++row) {
            existing.push_back(values[row].id);bool replaced{};const auto result=Reduce(values[row].id,ObjectiveBundle{values[row],ages[row],anchors[row]},operations,notices,replaced);
            if(!result || replaced) context.Commands().Destroy(entities[row]);
            if(result) {if(replaced) Create(context,*result);else {values[row]=result->value;ages[row]=result->age;anchors[row]=result->anchor;}}
        }
    }
    void AfterChunks(Query&,ecs::SystemContext& context) const {
        const auto& plan=context.Read<ObjectiveTransaction>();std::set<std::int32_t> existing;
        plan.existing.ForEach([&](auto id) {if(!existing.insert(id).second) throw std::logic_error("duplicate live objective ID");});
        std::set<std::int32_t> created;auto& notices=context.Write<ObjectiveChanges>().SlotAt(plan.existing.SlotCount());
        for(const auto& op:plan.operations) if(op.kind==ObjectiveOperationKind::Add && !existing.contains(op.id) && created.insert(op.id).second) {
            bool replaced{};if(const auto result=Reduce(op.id,{},plan.operations,notices,replaced)) Create(context,*result);
        }
    }
};
}
export namespace ecs {
template<> struct SystemTraits<renegade::ObjectiveSystem> {
    static constexpr std::string_view StableName="renegade.objectives";
    static constexpr std::size_t PieceRows=32;static constexpr SystemPhase Phase=SystemPhase::PostSimulation;
    using Before=SystemTypeList<engine::gameplay::TrackedPositionSystem>;using After=SystemTypeList<>;
};
}
