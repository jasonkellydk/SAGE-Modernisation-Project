export module games.renegade.presentation.menu.audio_preferences;
import std;
export import engine.config.adapters.preferences.preferences_file;
import games.renegade.presentation.menu.audio_settings;

export namespace renegade::presentation {
struct AudioPreferences {AudioVolumes volumes;std::array<bool,4> enabled{true,true,true,true};};
// WWAudio.cpp Load/Save_From_Registry: preserve the original key names,
// defaults, percent volumes and exact integer-one enable rule. The modern
// storage is a portable preferences file, shared with GeneralsZH's parser.
constexpr std::array<std::string_view,4> audio_volume_keys{"sound volume","music volume","dialog volume","cinematic volume"};
constexpr std::array<std::string_view,4> audio_enabled_keys{"sound enabled","music enabled","dialog enabled","cinematic enabled"};
AudioPreferences ReadAudioPreferences(const engine::config::Preferences& preferences,AudioVolumes defaults) {
    AudioPreferences state{defaults};
    for(unsigned index=0;index<4;++index) {
        state.volumes[index]=static_cast<int>(std::clamp<std::int64_t>(preferences.Number(audio_volume_keys[index],defaults[index]),0,100));
        state.enabled[index]=preferences.Number(audio_enabled_keys[index],1)==1;
    }
    return state;
}
AudioPreferences CaptureAudioPreferences(const AudioSettings& settings) {
    AudioPreferences state;
    for(unsigned index=0;index<4;++index) {
        state.volumes[index]=settings.volumes[index].position.Get();state.enabled[index]=settings.enabled[index].checked.Get();
    }
    return state;
}
void ApplyAudioPreferences(AudioSettings& settings,const AudioPreferences& state) {
    for(unsigned index=0;index<4;++index) {
        settings.volumes[index].Set(state.volumes[index]);settings.enabled[index].checked.Set(state.enabled[index]);
    }
}
void WriteAudioPreferences(engine::config::Preferences& preferences,const AudioPreferences& state) {
    for(unsigned index=0;index<4;++index) {
        preferences.Set(audio_volume_keys[index],std::clamp(state.volumes[index],0,100));
        preferences.Set(audio_enabled_keys[index],static_cast<std::int64_t>(state.enabled[index]));
    }
}
}
