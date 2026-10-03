export module games.renegade.presentation.scene.speech_assets;
import std;
import games.renegade.content.characters.visemes;
import games.renegade.gameplay.missions.resources.speech_animations;
import Assets.Cache;
import Assets.ModelRig;
import Assets.Identity;
import Graphics.Scene.Models.AnimationChannels;
import Graphics.Scene.Models.AnimationBlend;
import Graphics.Scene.Models.AnimationRotation;
export namespace renegade::presentation {
struct SpeechAssetLine {std::uint32_t id{},text{};std::string animation,english;Engine::Math::Fixed duration;};
struct PreparedSpeech {SpeechAnimation state;std::vector<SpeechClip> bank;};
// SoldierGameObj::Say_Dialogue selects an explicit head clip when supplied,
// otherwise DynamicSpeechAnimClass builds the original English viseme keys.
// All decoding and key preparation happen on the shared loading worker.
inline std::expected<std::optional<PreparedSpeech>,std::string> PrepareSpeech(Assets::AssetCache& assets,
    const Assets::ModelRigDesc& model,std::span<const SpeechAssetLine> lines,std::vector<Assets::ModelAnimationDesc>& clips) {
    using namespace Engine::Math;
    const auto head=std::ranges::find_if(model.attached_rigs,[](const auto& child) {
        const auto skeleton=Assets::Canonicalize_Asset_Name(child.skeleton_name);
        return skeleton.starts_with("s_a_") || skeleton.starts_with("s_b_");
    });
    if(head==model.attached_rigs.end()) return std::optional<PreparedSpeech>{};
    PreparedSpeech result;result.state.first_bone=head->first_bone;result.state.bone_count=head->bone_count;
    std::string error;
    const auto mouth_name=head->skeleton_name+"."+(Assets::Canonicalize_Asset_Name(head->skeleton_name).starts_with("s_a_") ? "S_A_MOUTH" : "S_B_MOUTH");
    const auto mouth=assets.Load_Rig(Assets::AssetType::Animation,mouth_name,error);
    if(!mouth || mouth->animations.size()!=1) return std::unexpected("speech mouth poses "+mouth_name+": "+error);
    for(const auto& line:lines) {
        Assets::ModelAnimationDesc clip;
        if(!line.animation.empty()) {
            const auto name=line.animation.find('.')==std::string::npos ? head->skeleton_name+"."+line.animation : line.animation;
            const auto source=assets.Load_Rig(Assets::AssetType::Animation,name,error);
            if(!source || source->animations.size()!=1) return std::unexpected("speech animation "+name+": "+error);
            clip=source->animations.front();
        } else {
            const auto poses=content::SpeechVisemes(line.english);if(poses.empty()) continue;
            // HMorphAnimClass uses 30fps independently of the pose bank.
            const auto increment=line.duration*Fixed::FromInt(30)/Fixed::FromInt(poses.size()+1);
            std::map<std::uint32_t,std::uint32_t> keys{{0,0}};
            for(std::size_t i=0;i<poses.size();++i) {
                const auto frame=std::uint32_t(((increment*Fixed::FromInt(i+1))+Fixed::FromRatio(1,2)).Raw()/Fixed::OneRaw);
                keys[frame]=poses[i]+1;
            }
            const auto close=std::uint32_t(((increment*Fixed::FromInt(poses.size()+3))+Fixed::FromRatio(1,2)).Raw()/Fixed::OneRaw);keys[close]=0;
            clip.name="speech-"+std::to_string(line.id);clip.skeleton_name=head->skeleton_name;clip.frame_count=keys.rbegin()->first+1;clip.frame_rate=30;
            for(const auto& source:mouth->animations.front().channels) {
                if(source.component>=Assets::ModelChannelComponent::Visibility) continue;
                auto channel=source;channel.samples.clear();channel.key_frames.clear();channel.step_into_key.clear();channel.first_frame=0;channel.hold_endpoints=true;
                for(const auto [time,pose]:keys) {
                    const auto sample=Graphics::Select_Animation_Interval(source,float(pose));
                    auto value=sample.first;
                    if(source.component==Assets::ModelChannelComponent::Rotation)
                        value=Graphics::Interpolate_Animation_Rotation(sample.first,sample.second,sample.fraction);
                    else for(unsigned axis=0;axis<4;++axis) value[axis]=sample.first[axis]+(sample.second[axis]-sample.first[axis])*sample.fraction;
                    channel.key_frames.push_back(time);channel.samples.push_back(value);
                }
                clip.channels.push_back(std::move(channel));
            }
        }
        if(Assets::Canonicalize_Asset_Name(clip.skeleton_name)!=Assets::Canonicalize_Asset_Name(head->skeleton_name)) return std::unexpected("speech skeleton mismatch");
        for(auto& channel:clip.channels) {
            if(channel.bone>=head->bone_count) return std::unexpected("speech pivot outside attached skeleton");
            channel.bone+=head->first_bone;
        }
        clip.skeleton_name=model.skeleton_name;
        engine::gameplay::ClipPlayback playback;playback.clip=std::uint32_t(clips.size()+1);playback.frame_count=clip.frame_count;
        playback.frames_per_second=Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(clip.frame_rate));playback.mode=engine::gameplay::ClipMode::Once;
        result.bank.push_back({line.id,playback,line.duration});clips.push_back(std::move(clip));
    }
    return std::optional<PreparedSpeech>{std::move(result)};
}
}
