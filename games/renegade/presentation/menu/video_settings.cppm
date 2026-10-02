export module games.renegade.presentation.menu.video_settings;
import std;
import engine.gui.w3d.value_controls;
import engine.config.adapters.preferences.preferences_file;

export namespace renegade::presentation {
using VideoLevels=std::array<int,3>;
using VideoRamp=std::array<std::uint16_t,256>;
inline constexpr VideoLevels video_minimum{60,-45,50},video_maximum{210,45,200},video_default{90,0,130};
inline constexpr std::array<std::string_view,3> video_keys{"Gamma_Correction","Brightness","Contrast"};
// dlgconfigvideotab.h supplies hundredths; DX8Wrapper::Set_Gamma builds
// the same curve for all three channels, with uselimit=false in this dialog.
VideoRamp VideoCalibrationRamp(VideoLevels levels) {
    const float gamma=std::clamp(levels[0]*0.01f,0.6f,6.f);
    const float brightness=std::clamp(levels[1]*0.01f,-0.5f,0.5f);
    const float contrast=std::clamp(levels[2]*0.01f,0.5f,2.f);
    VideoRamp ramp{};const float inverse=1.f/gamma;
    for(unsigned i=0;i<ramp.size();++i) {
        const float out=std::clamp(contrast*std::pow(i/256.f,inverse)+brightness,0.f,1.f);
        ramp[i]=static_cast<std::uint16_t>(out*65535.f);
    }
    return ramp;
}
class VideoSettings final {
public:
    using Changed=std::function<void(VideoLevels,const VideoRamp&)>;
    std::array<engine::gui::w3d::SliderValueModel,3> sliders;
    explicit VideoSettings(VideoLevels levels=video_default,Changed changed={}):m_changed(std::move(changed)) {
        for(unsigned i=0;i<sliders.size();++i) {sliders[i].Range(video_minimum[i],video_maximum[i]);sliders[i].Set(levels[i]);}
        for(auto& slider:sliders) slider.position.Subscribe([this](int){if(m_active) Notify();});
    }
    VideoLevels Levels() const {return {sliders[0].position.Get(),sliders[1].position.Get(),sliders[2].position.Get()};}
    void Activate() {m_active=true;Notify();}
    void Restore(VideoLevels levels) {
        const bool active=std::exchange(m_active,false);
        for(unsigned i=0;i<sliders.size();++i) sliders[i].Set(levels[i]);
        m_active=active;if(active) Notify();
    }
    static std::optional<unsigned> SliderIndex(std::uint32_t id) {
        switch(id) {case 1385:return 0;case 1386:return 1;case 1387:return 2;default:return {};}
    }
    static std::optional<unsigned> CaptionIndex(std::uint32_t id) {
        switch(id) {case 1643:return 0;case 1644:return 1;case 1645:return 2;default:return {};}
    }
    // The source formats each slider as its hundredths, including negative brightness.
    std::string Caption(unsigned index) const {
        const int level=Levels().at(index);const unsigned magnitude=static_cast<unsigned>(std::abs(level));
        return std::format("{}{}.{:02}",level<0 ? "-" : "",magnitude/100,magnitude%100);
    }
private:
    void Notify() {if(m_changed) {const auto levels=Levels();m_changed(levels,VideoCalibrationRamp(levels));}}
    Changed m_changed;bool m_active{};
};
VideoLevels ReadVideoPreferences(const engine::config::Preferences& preferences) {
    VideoLevels levels{};
    for(unsigned i=0;i<levels.size();++i) levels[i]=static_cast<int>(std::clamp(preferences.Number(video_keys[i],video_default[i]),
        std::int64_t(video_minimum[i]),std::int64_t(video_maximum[i])));
    return levels;
}
void WriteVideoPreferences(engine::config::Preferences& preferences,VideoLevels levels) {
    for(unsigned i=0;i<levels.size();++i) preferences.Set(video_keys[i],std::clamp(levels[i],video_minimum[i],video_maximum[i]));
}
}
