export module games.renegade.presentation.menu.audio_settings;
import std;
import engine.gui.mvvm.observable;
export import engine.gui.w3d.value_controls;
import engine.gui.w3d.navigation;
import engine.config.adapters.ini.section_reader;

export namespace renegade::presentation {
enum class AudioCategory : unsigned { Effects,Music,Dialog,Cinematic,Count };
using AudioVolumes=std::array<int,static_cast<unsigned>(AudioCategory::Count)>;
class TechOptions final {
public:
    engine::gui::w3d::TabSelectionModel tabs{3};
    engine::gui::mvvm::Observable<unsigned>& selected_tab{tabs.position};
    bool Select(unsigned index) { return index<tabs.Count() && tabs.Select(index); }
    bool Key(engine::gui::w3d::ValueKey key) {
        using engine::gui::w3d::ValueKey;using engine::gui::w3d::SelectionAction;
        switch(key) {
        case ValueKey::Left:case ValueKey::Up:return tabs.Key(SelectionAction::Previous);
        case ValueKey::Right:case ValueKey::Down:return tabs.Key(SelectionAction::Next);
        case ValueKey::Home:return tabs.Key(SelectionAction::First);
        case ValueKey::End:return tabs.Key(SelectionAction::Last);
        default:return false;
        }
    }
    unsigned Resource() const { constexpr std::array resources{231u,233u,232u};return resources.at(selected_tab.Get()); }
};
// WWAudio.cpp Load_Default_Volume, with game-specific keys and defaults.
std::expected<AudioVolumes,std::string> ReadAudioDefaults(std::string text) {
    AudioVolumes volumes{43,31,50,100};
    if(text.empty()) return volumes;
    const auto document=engine::config::ini::ReadSections("WWAudio.ini",std::move(text));
    if(!document) return std::unexpected(document.error());
    const auto* section=engine::config::ini::FindSection(*document,"Default Volume");
    if(!section) return volumes;
    constexpr std::array keys{"sound_volume","music_volume","dialog_volume","cinematic_volume"};
    for(unsigned i=0;i<keys.size();++i) if(const auto* node=section->Find(keys[i])) {
        int value{};const auto parsed=std::from_chars(node->text.data(),node->text.data()+node->text.size(),value);
        if(parsed.ec!=std::errc{} || parsed.ptr!=node->text.data()+node->text.size())
            return std::unexpected("invalid default audio volume: "+std::string(keys[i]));
        volumes[i]=std::clamp(value,0,100);
    }
    return volumes;
}
class AudioSettings final {
public:
    using Changed=std::function<void(AudioCategory,int,bool)>;
    explicit AudioSettings(AudioVolumes defaults={43,31,50,100},Changed changed={})
        :m_defaults(defaults),m_changed(std::move(changed)) {
        for(unsigned i=0;i<volumes.size();++i) {
            volumes[i].Range(0,100);volumes[i].Set(m_defaults[i]);enabled[i].checked.Set(true);
            volumes[i].position.Subscribe([this,i](int) { Notify(i); });
            enabled[i].checked.Subscribe([this,i](bool checked) { volumes[i].enabled.Set(checked);Notify(i); });
        }
        defaults_command.SetAction([this] { Defaults(); });
    }
    std::array<engine::gui::w3d::SliderValueModel,4> volumes;
    std::array<engine::gui::w3d::CheckValueModel,4> enabled;
    engine::gui::mvvm::Command defaults_command;
    // dlgconfigaudiotab.cpp Set_Default_Volumes changes volumes, retaining
    // checkbox state. Checkboxes apply immediately and disable their slider.
    void Defaults() { for(unsigned i=0;i<volumes.size();++i) volumes[i].Set(m_defaults[i]); }
    static std::optional<unsigned> SliderIndex(std::uint32_t id) {
        switch(id) {case 1021:return 0;case 1022:return 1;case 1026:return 2;case 1028:return 3;default:return {};}
    }
    static std::optional<unsigned> CheckIndex(std::uint32_t id) {
        switch(id) {case 1023:return 0;case 1024:return 1;case 1025:return 2;case 1027:return 3;default:return {};}
    }
private:
    void Notify(unsigned index) { if(m_changed) m_changed(static_cast<AudioCategory>(index),volumes[index].position.Get(),enabled[index].checked.Get()); }
    AudioVolumes m_defaults;
    Changed m_changed;
};
}
