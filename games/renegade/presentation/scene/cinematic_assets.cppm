export module games.renegade.presentation.scene.cinematic_assets;
import std;
export import games.renegade.gameplay.cinematics.systems.cinematic_system;
export import games.renegade.content.cinematics.startup;
import games.renegade.content.levels.render_assets;
import games.renegade.content.characters.actor_animation;
import games.renegade.content.levels.vehicle_presentation;
import games.renegade.content.levels.weapon_presentation;
import engine.gameplay.common.physics.systems.suspension_system;
import Assets.Cache;
import Assets.Models;
import Assets.Identity;
import Graphics.Scene.Models.AssetPose;
import Graphics.Scene.AffineTransform;
import Graphics.Scene.Props.AssetPreparation;
import engine.audio.decoders.ffmpeg.ffmpeg_decoder;
import engine.filesystem.core.virtual_file_system;
import Engine.Core.Math.FixedPresentation;
export namespace renegade::presentation {
struct PreparedCinematicActor {
    std::int32_t slot{};std::string name;Graphics::PreparedPropAsset gpu;
    Graphics::ModelAssetPose pose;engine::level::PoseTrack motion;engine::gameplay::ClipPlayback initial_clip;
    std::vector<engine::gameplay::Suspension> wheels;
    std::optional<content::WeaponPresentation> weapon;
    Graphics::PreparedPropAsset held_weapon;
    Graphics::PreparedPropAsset projectile;
    engine::level::PoseTrack muzzle;
};
struct PreparedCinematic {
    CinematicLibrary library;std::vector<PreparedCinematicActor> actors;
    std::shared_ptr<const engine::audio::PcmBuffer> music;
    content::CinematicStartup startup;
};
// Preparation runs on the existing shared resource worker. All W3D poses,
// textures and decoded PCM are ready before publication; no game-loop I/O.
inline std::expected<PreparedCinematic,std::string> PrepareCinematic(Assets::AssetCache& assets,
    const engine::filesystem::VirtualFileSystem& files,const content::DefinitionCatalog& definitions,
    const content::CinematicStartup& startup,std::uint32_t sample_rate,const std::function<bool()>& cancelled) {
    using namespace Engine::Math;using O=content::CinematicOpcode;
    const auto text=files.ReadText(startup.control_file);if(!text) return std::unexpected("missing cinematic "+startup.control_file);
    auto definition=content::ReadCinematic(*text);if(!definition) return std::unexpected(definition.error());
    PreparedCinematic result;result.startup=startup;result.library.definition=std::move(*definition);
    auto textures=std::make_shared<Graphics::PreparedPropTextures>();
    struct State {
        std::int32_t parent{-1};std::string bone;Graphics::RenderTransform world=Graphics::Affine_Identity();
        std::vector<Assets::ModelAnimationDesc> clips;std::uint32_t clip{},previous{};Fixed start{},previous_start{},blend{};bool looping{},previous_looping{};
        Graphics::RenderTransform muzzle=Graphics::Affine_Identity();
    };
    std::vector<State> states;std::map<std::int32_t,std::size_t> slots;
    const auto& actions=result.library.definition.actions;
    for(unsigned index=0;index<actions.size();++index) if(actions[index].opcode==O::Script) {
        const auto& action=actions[index];if(Assets::Canonicalize_Asset_Name(action.asset)!="m00_cinematic_attack_command_dls") continue;
        const auto source=files.ReadText(action.asset+".lua");if(!source) return std::unexpected("missing modifiable cinematic attack script "+action.asset);
        const auto attack=content::ReadCinematicAttack(*source,action.parameters);if(!attack) return std::unexpected(attack.error());result.library.attacks.emplace(index,*attack);
    }
    for(const auto& action:actions) if(action.opcode==O::CreateModel || action.opcode==O::CreatePreset) {
        if(cancelled()) return std::unexpected("loading cancelled");
        if(action.slot<0 || slots.contains(action.slot)) return std::unexpected("cinematic slot reuse requires a lifecycle adapter");
        std::string model=action.asset;const content::LegacyDefinition* physics{};const content::LegacyDefinition* preset_definition{};
        if(action.opcode==O::CreatePreset) {
            const auto preset=std::ranges::find_if(definitions.by_id,[&](const auto& pair) {return Assets::Canonicalize_Asset_Name(pair.second.name)==Assets::Canonicalize_Asset_Name(action.asset);});
            if(preset==definitions.by_id.end()) return std::unexpected("missing cinematic preset "+action.asset);
            preset_definition=&preset->second;physics=definitions.Find(preset->second.physics);model=physics ? physics->model : preset->second.model;
        } else if(!model.ends_with(".w3d")) model+=".w3d";
        auto path=content::ResolvePresetModel(files,model);if(!path) return std::unexpected(path.error());
        PreparedCinematicActor actor;actor.slot=action.slot;actor.name=action.asset;actor.gpu.model=assets.Request_Model(*path);assets.Wait(actor.gpu.model);
        const auto* source=assets.Try_Get_Model(actor.gpu.model);if(!source) return std::unexpected(action.asset+": "+assets.Get_Error(actor.gpu.model));
        State state;
        if(preset_definition) {
            auto weapon=content::ReadEquippedWeapon(definitions,*preset_definition);if(!weapon) return std::unexpected(action.asset+": "+weapon.error());actor.weapon=std::move(*weapon);
        }
        for(unsigned i=0;i<actions.size();++i) if(actions[i].opcode==O::Animation && actions[i].slot==action.slot) {
            std::string error;const auto animation=assets.Load_Rig(Assets::AssetType::Animation,actions[i].asset,error);
            if(!animation || animation->animations.empty()) return std::unexpected(actions[i].asset+": "+error);
            const auto& clip=animation->animations.front();state.clips.push_back(clip);
            engine::gameplay::ClipPlayback playback;playback.clip=std::uint32_t(state.clips.size());playback.frame_count=clip.frame_count;
            playback.frames_per_second=Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(clip.frame_rate));
            playback.loop_period=Fixed::FromInt(clip.frame_count>1 ? clip.frame_count-1 : 1);
            playback.mode=actions[i].looping ? engine::gameplay::ClipMode::Loop : engine::gameplay::ClipMode::Once;
            result.library.animations.emplace(i,playback);
        }
        // HumanState's upright standing pose exists before the control file
        // supplies its first clip: X00's slot 11 waits 106 frames after creation.
        const auto skeleton=Assets::Canonicalize_Asset_Name(source->Rig().skeleton_name);
        if(skeleton=="s_a_human" || skeleton=="s_b_human") {
            std::string error;const auto standing=assets.Load_Rig(Assets::AssetType::Animation,content::StandingAnimation(skeleton[2]-'a'+'A',actor.weapon ? actor.weapon->style : 7),error);
            if(!standing || standing->animations.empty()) return std::unexpected("cinematic standing pose: "+error);
            const auto& clip=standing->animations.front();state.clips.push_back(clip);
            actor.initial_clip.clip=std::uint32_t(state.clips.size());actor.initial_clip.frame_count=clip.frame_count;
            actor.initial_clip.frames_per_second=Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(clip.frame_rate));actor.initial_clip.mode=engine::gameplay::ClipMode::Loop;
            actor.initial_clip.loop_period=Fixed::FromInt(clip.frame_count>1 ? clip.frame_count-1 : 1);
            state.clip=actor.initial_clip.clip;state.looping=true;
        }
        std::string error;
        if(!source->Rig().bones.empty() && !actor.pose.Initialize(source->Rig(),std::span<const Assets::ModelAnimationDesc>(state.clips),error)) return std::unexpected(action.asset+": "+error);
        if(physics) {
            const auto vehicle=content::ReadVehiclePresentation(*physics);if(!vehicle) return std::unexpected(vehicle.error());
            if(*vehicle) for(std::size_t bone=0;bone<source->Rig().bones.size();++bone) {
                const auto name=Assets::Canonicalize_Asset_Name(source->Rig().bones[bone].name);if(!name.starts_with("wheelp") || name.size()<8) continue;
                const auto related=[&](std::string_view prefix) {
                    for(std::size_t candidate=0;candidate<source->Rig().bones.size();++candidate) {
                        const auto other=Assets::Canonicalize_Asset_Name(source->Rig().bones[candidate].name);
                        if(other.starts_with(prefix) && other.size()>=8 && other.substr(6,2)==name.substr(6,2)) return std::uint32_t(candidate);
                    }return ~0u;
                };
                const auto rotation=related("wheelc"),axis=related("wheelt"),fork=related("wheelf");
                // Forked suspensions require their own constraint binding.
                if(fork!=~0u) return std::unexpected("unported vehicle fork constraint: "+action.asset+"/"+name);
                Graphics::RenderTransform socket;if(!actor.pose.Bone_Transform(bone,socket)) return std::unexpected("missing wheel socket");
                engine::gameplay::Suspension wheel;wheel.position_bone=axis==~0u ? std::uint32_t(bone) : axis;wheel.rotation_bone=rotation;wheel.travel=(**vehicle).spring_length;wheel.categories=1;
                for(unsigned element=0;element<12;++element) wheel.socket.elements[element]=Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(socket.matrix[element]));
                if(rotation!=~0u) {
                    Graphics::RenderTransform center;if(!actor.pose.Bone_Transform(rotation,center)) return std::unexpected("missing wheel center");
                    const FixedVector3 delta{Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(socket.matrix[3]-center.matrix[3])),Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(socket.matrix[7]-center.matrix[7])),Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(socket.matrix[11]-center.matrix[11]))};wheel.radius=Length(delta);
                }
                if(axis!=~0u) {
                    Graphics::RenderTransform axis_transform;if(!actor.pose.Bone_Transform(axis,axis_transform)) return std::unexpected("missing suspension translation axis");
                    const auto alignment=axis_transform.matrix[2]*socket.matrix[2]+axis_transform.matrix[6]*socket.matrix[6]+axis_transform.matrix[10]*socket.matrix[10];
                    if(std::abs(alignment)<.0001f) return std::unexpected("degenerate suspension translation axis");wheel.translation_scale=Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(1.f/alignment));
                }
                actor.wheels.push_back(wheel);
            }
        }
        if(!source->Submeshes().empty()) {
            if(!Graphics::Build_Prop_Asset_Geometry(*source,actor.gpu.geometry,error) || !Graphics::Prepare_Prop_Textures(assets,actor.gpu.model,*textures,error)) return std::unexpected(action.asset+": "+error);
            actor.gpu.textures=textures;
        }
        if(actor.weapon && !actor.weapon->model.empty() && actor.pose.Bone_Index("GUNBONE")<actor.pose.Bone_Count()) {
            const auto path=content::ResolvePresetModel(files,actor.weapon->model);if(!path) return std::unexpected(path.error());actor.held_weapon.model=assets.Request_Model(*path);assets.Wait(actor.held_weapon.model);
            const auto* gun=assets.Try_Get_Model(actor.held_weapon.model);if(!gun) return std::unexpected(assets.Get_Error(actor.held_weapon.model));
            if(!Graphics::Build_Prop_Asset_Geometry(*gun,actor.held_weapon.geometry,error) || !Graphics::Prepare_Prop_Textures(assets,actor.held_weapon.model,*textures,error)) return std::unexpected(error);actor.held_weapon.textures=textures;
            if(!gun->Rig().bones.empty()) {Graphics::ModelAssetPose gun_pose;if(!gun_pose.Initialize(gun->Rig(),error)) return std::unexpected(error);auto bone=gun_pose.Bone_Index("muzzlea0");if(bone>=gun_pose.Bone_Count()) bone=0;if(!gun_pose.Bone_Transform(bone,state.muzzle)) return std::unexpected("weapon muzzle pose is missing");}
        }
        if(actor.weapon && !actor.weapon->projectile.empty() && Assets::Canonicalize_Asset_Name(actor.weapon->projectile)!="null") {
            const auto path=content::ResolvePresetModel(files,actor.weapon->projectile);if(!path) return std::unexpected(path.error());actor.projectile.model=assets.Request_Model(*path);assets.Wait(actor.projectile.model);
            const auto* projectile=assets.Try_Get_Model(actor.projectile.model);if(!projectile) return std::unexpected(assets.Get_Error(actor.projectile.model));
            if(!projectile->Submeshes().empty() && (!Graphics::Build_Prop_Asset_Geometry(*projectile,actor.projectile.geometry,error) || !Graphics::Prepare_Prop_Textures(assets,actor.projectile.model,*textures,error))) return std::unexpected(error);actor.projectile.textures=textures;
        }
        actor.motion.samples_per_second=60;actor.muzzle.samples_per_second=60;slots.emplace(action.slot,result.actors.size());result.actors.push_back(std::move(actor));states.push_back(std::move(state));
    }
    const auto evaluate=[&](Fixed now)->bool {
        for(std::size_t i=0;i<states.size();++i) if(states[i].clip) {
            const auto& clip=states[i].clips[states[i].clip-1];const auto fps=Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(clip.frame_rate));
            auto frame=(now-states[i].start)*fps;
            if(states[i].looping) {const auto period=Fixed::FromInt(clip.frame_count>1 ? clip.frame_count-1 : 1).Raw();frame=Fixed::FromRaw(frame.Raw()%period);}
            if(states[i].previous && states[i].blend>Fixed{} && now-states[i].start<states[i].blend) {
                const auto& previous=states[i].clips[states[i].previous-1];const auto previous_fps=Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(previous.frame_rate));auto previous_frame=(now-states[i].previous_start)*previous_fps;
                if(states[i].previous_looping) {const auto period=Fixed::FromInt(previous.frame_count>1 ? previous.frame_count-1 : 1).Raw();previous_frame=Fixed::FromRaw(previous_frame.Raw()%period);}
                if(!result.actors[i].pose.Evaluate_Blended(states[i].previous-1,ToFloat(previous_frame),states[i].clip-1,ToFloat(frame),ToFloat((now-states[i].start)/states[i].blend))) return false;
            } else if(!result.actors[i].pose.Evaluate(states[i].clip-1,ToFloat(frame),false)) return false;
        }
        return true;
    };
    std::function<std::optional<Graphics::RenderTransform>(std::size_t,unsigned)> world;
    world=[&](std::size_t i,unsigned depth)->std::optional<Graphics::RenderTransform> {
        if(depth>states.size()) return {};
        if(states[i].parent<0) return states[i].world;
        const auto parent=slots.find(states[i].parent);if(parent==slots.end()) return {};
        const auto parent_world=world(parent->second,depth+1);if(!parent_world) return {};
        auto bone=Graphics::Affine_Identity();
        if(!states[i].bone.empty()) {
            const auto& pose=result.actors[parent->second].pose;auto index=pose.Bone_Index(states[i].bone);
            // HTreeClass::Get_Bone_Index returns root index zero on a miss.
            // X00 creates its Nod actors on the earlier GDI trajectory before
            // their actual Nod attachment host is created later in the film.
            if(index>=pose.Bone_Count()) index=0;
            if(pose.Bone_Count() && !pose.Bone_Transform(index,bone)) return {};
        }
        return Graphics::Multiply_Affine(*parent_world,bone);
    };
    const auto& cues=result.library.definition.timeline.cues;std::size_t cursor{};
    const auto duration=cues.empty() ? Fixed{} : cues.back().time;
    const auto count=std::uint64_t((duration*Fixed::FromInt(60)).Raw()/Fixed::OneRaw)+1;
    if(count>60*600) return std::unexpected("cinematic motion exceeds preparation budget");
    for(std::uint64_t tick=0;tick<count;++tick) {
        if(cancelled()) return std::unexpected("loading cancelled");const auto now=Fixed::FromRatio(tick,60);
        if(!evaluate(now)) return std::unexpected("cinematic clip evaluation failed");
        while(cursor<cues.size() && cues[cursor].time<=now) {
            const auto& cue=cues[cursor++];const auto& action=actions[cue.action];const auto found=slots.find(action.slot);
            if(found==slots.end()) continue;auto& state=states[found->second];
            if(action.opcode==O::Animation) {state.previous=state.clip;state.previous_start=state.start;state.previous_looping=state.looping;state.blend=action.blended ? Fixed::FromRatio(1,5) : Fixed{};state.clip=result.library.animations.at(cue.action).clip;state.start=cue.time;state.looping=action.looping;}
            else if(action.opcode==O::Attach) {
                const auto previous=world(found->second,0);if(!previous) return std::unexpected("invalid cinematic attachment graph");
                state.parent=action.host;state.bone=action.bone;if(action.host<0) state.world=*previous;
            } else if(action.opcode==O::CreatePreset && action.host>=0) {
                const auto previous_parent=state.parent;const auto previous_bone=state.bone;state.parent=action.host;state.bone=action.bone;const auto initial=world(found->second,0);
                state.parent=previous_parent;state.bone=previous_bone;if(!initial) return std::unexpected("invalid cinematic creation bone at slot "+std::to_string(action.slot)+": "+action.bone);state.world=*initial;
            }
        }
        if(!evaluate(now)) return std::unexpected("cinematic clip evaluation failed");
        for(std::size_t i=0;i<states.size();++i) {
            const auto transform=world(i,0);if(!transform) return std::unexpected("unresolved cinematic socket for slot "+std::to_string(result.actors[i].slot));
            FixedAffineTransform3 fixed;
            for(unsigned element=0;element<12;++element) fixed.elements[element]=Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(transform->matrix[element]));
            result.actors[i].motion.samples.push_back(fixed);
            if(result.actors[i].weapon) {
                auto muzzle=*transform;
                if(result.actors[i].held_weapon.model.Is_Valid()) {Graphics::RenderTransform gun;if(!result.actors[i].pose.Bone_Transform(result.actors[i].pose.Bone_Index("GUNBONE"),gun)) return std::unexpected("animated gun socket is missing");muzzle=Graphics::Multiply_Affine(Graphics::Multiply_Affine(muzzle,gun),states[i].muzzle);}
                else {auto bone=result.actors[i].pose.Bone_Index("muzzlea0");if(bone<result.actors[i].pose.Bone_Count()) {Graphics::RenderTransform socket;if(!result.actors[i].pose.Bone_Transform(bone,socket)) return std::unexpected("vehicle muzzle socket is missing");muzzle=Graphics::Multiply_Affine(muzzle,socket);}}
                for(unsigned element=0;element<12;++element) fixed.elements[element]=Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(muzzle.matrix[element]));result.actors[i].muzzle.samples.push_back(fixed);
            }
        }
    }
    if(!startup.music.empty()) {
        const auto path=content::ResolvePresetModel(files,startup.music);if(!path) return std::unexpected(path.error());const auto bytes=files.Read(*path);
        if(!bytes) return std::unexpected("missing cinematic music");auto pcm=engine::audio::DecodeAll(*bytes,sample_rate);if(!pcm) return std::unexpected("cinematic music decode failed");
        result.music=std::make_shared<const engine::audio::PcmBuffer>(std::move(*pcm));
    }
    return result;
}
}
