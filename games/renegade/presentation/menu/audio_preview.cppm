export module games.renegade.presentation.menu.audio_preview;
import std;
export import games.renegade.presentation.menu.audio_settings;

export namespace renegade::presentation {
enum class PreviewAction { Start,Stop,SetLooping };
struct PreviewCommand { AudioCategory category;PreviewAction action;bool loop{}; };

// Commando/dlgconfigaudiotab.cpp: preview sounds, restart throttling and the
// timeout that changes infinite playback to one final cycle. The host executes
// these commands through shared audio; this policy owns no audio device.
class AudioPreview final {
public:
    static constexpr std::array<std::string_view,4> filenames{
        "laser_rifle_fire_01.wav","sakura battle theme.mp3","m00s1_s1s1gbmg_snd.wav","00-n000e.wav"};
    std::vector<PreviewCommand> Changed(AudioCategory category,std::uint64_t milliseconds,bool available=true) {
        const auto index=static_cast<unsigned>(category);
        if(index>=4 || !available) return {};
        std::vector<PreviewCommand> commands;
        if(category==AudioCategory::Effects) {
            if(m_playing[index] && !After(milliseconds,m_started[index],150)) return {};
            if(m_playing[index]) commands.push_back({category,PreviewAction::Stop});
            commands.push_back({category,PreviewAction::Start,true});
        } else if(category==AudioCategory::Music) {
            if(!m_playing[index]) commands.push_back({category,PreviewAction::Start,false});
        } else {
            if(m_playing[index]) commands.push_back({category,PreviewAction::SetLooping,true});
            else commands.push_back({category,PreviewAction::Start,true});
        }
        m_playing[index]=true;m_finishing[index]=false;m_started[index]=milliseconds;
        return commands;
    }
    std::vector<PreviewCommand> Frame(std::uint64_t milliseconds) {
        std::vector<PreviewCommand> commands;
        for(unsigned index=0;index<4;++index) {
            if(!m_playing[index] || !After(milliseconds,m_started[index],index==0 ? 500 : 2000)) continue;
            const auto category=static_cast<AudioCategory>(index);
            if(category==AudioCategory::Music) {
                commands.push_back({category,PreviewAction::Stop});m_playing[index]=false;
            } else if(!m_finishing[index]) {
                commands.push_back({category,PreviewAction::SetLooping,false});m_finishing[index]=true;
            }
        }
        return commands;
    }
    void Finished(AudioCategory category) {
        const auto index=static_cast<unsigned>(category);
        if(index<4) {m_playing[index]=false;m_finishing[index]=false;}
    }
    std::vector<PreviewCommand> Close() {
        std::vector<PreviewCommand> commands;
        for(unsigned index=0;index<4;++index) if(m_playing[index])
            commands.push_back({static_cast<AudioCategory>(index),PreviewAction::Stop});
        m_playing={};m_finishing={};return commands;
    }
private:
    static bool After(std::uint64_t now,std::uint64_t start,std::uint64_t delay) {
        return now>=start && now-start>delay;
    }
    std::array<std::uint64_t,4> m_started{};
    std::array<bool,4> m_playing{},m_finishing{};
};
}
