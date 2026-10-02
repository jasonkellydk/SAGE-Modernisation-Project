export module games.renegade.presentation.menu.controls_settings;
import std;
import games.renegade.content.presentation.input_configuration;
import engine.gui.w3d.input_capture;
import engine.gui.w3d.value_controls;
import engine.gui.mvvm.observable;

export namespace renegade::presentation {
struct ControlsTab {std::uint32_t resource;std::span<const std::string_view> functions;};
inline constexpr std::array<std::string_view,6> multiplayer_control_functions{
    "BeginPublicMessage","BeginTeamMessage","TeamInfoToggle","BattleInfoToggle","ObjectivesScreen","MapScreen"};
inline constexpr std::array controls_tabs{
    ControlsTab{137,std::span(content::remappable_functions).subspan(0,11)},
    ControlsTab{139,std::span(content::remappable_functions).subspan(11,7)},
    ControlsTab{140,std::span(content::remappable_functions).subspan(18,10)},
    ControlsTab{141,std::span(content::remappable_functions).subspan(28,2)},
    ControlsTab{143,multiplayer_control_functions}};
struct ControlConflict {std::uint32_t tab{},control{};std::string_view function,previous;content::InputId input{};bool operator==(const ControlConflict&) const=default;};
class ControlsSettings final {
public:
    using Caption=std::function<std::u16string(content::InputId)>;
    engine::gui::w3d::SliderValueModel sensitivity;
    engine::gui::w3d::CheckValueModel invert,invert_2d,damage_indicators,camera_locked;
    engine::gui::mvvm::Observable<std::optional<ControlConflict>> conflict;
    ControlsSettings(content::InputConfiguration configuration,Caption caption,bool locked=false)
        :m_configuration(std::move(configuration)),m_caption(std::move(caption)),m_applied_camera(locked) {
        sensitivity.Range(0,100);camera_locked.checked.Set(locked);
        for(const auto& tab:controls_tabs) for(unsigned row=0;row<tab.functions.size();++row)
            for(bool secondary:{false,true}) {
                const auto control=secondary ? content::secondary_input_controls[row] : content::primary_input_controls[row];
                m_fields.emplace(Key{tab.resource,control},Field{tab.functions[row],secondary,
                    std::make_unique<engine::gui::w3d::InputCaptureModel>(engine::gui::w3d::InputCaptureLimits{500,256})});
            }
        Reload();
    }
    const content::InputConfiguration& Configuration() const {return m_configuration;}
    engine::gui::w3d::InputCaptureModel* Capture(std::uint32_t tab,std::uint32_t control) {
        const auto found=m_fields.find({tab,control});return found==m_fields.end() ? nullptr : found->second.capture.get();
    }
    void Focus(std::uint32_t tab,std::optional<std::uint32_t> control,std::uint64_t milliseconds) {
        for(auto& [key,field]:m_fields) field.capture->Focus(control && key==Key{tab,*control},milliseconds);
    }
    bool QueueKeyboard(std::uint32_t tab,std::uint32_t control,content::InputId input) {
        auto* field=Capture(tab,control);return field && !conflict.Get() && field->QueueKeyboard(input);
    }
    void FlushKeyboard() {
        for(auto& [key,field]:m_fields) field.capture->FlushKeyboard([this,key](auto input){return Resolve(key,input);});
    }
    bool Pointer(std::uint32_t tab,std::uint32_t control,engine::gui::w3d::CapturePointer pointer,
        content::InputId input,std::uint64_t milliseconds) {
        auto* field=Capture(tab,control);return field && !conflict.Get() &&
            field->Pointer(pointer,input,milliseconds,[this,tab,control](auto input){return Resolve({tab,control},input);});
    }
    bool Confirm(bool yes) {
        const auto pending=conflict.Get();if(!pending) return false;
        if(yes) {
            const bool clear_zoom=Zoom(pending->function);
            for(unsigned i=0;i<content::input_functions.size();++i) {
                const auto function=content::input_functions[i];
                if(!clear_zoom && Zoom(function)) continue;
                if(m_configuration.bindings[i].primary==pending->input) m_configuration.Set(function,false,0);
                if(m_configuration.bindings[i].secondary==pending->input) m_configuration.Set(function,true,0);
            }
            const auto found=m_fields.find({pending->tab,pending->control});
            m_configuration.Set(pending->function,found->second.secondary,pending->input);ReloadBindings();
        } else ReloadBindings(pending->tab);
        conflict.Set({});return true;
    }
    void Defaults(content::InputConfiguration configuration) {
        conflict.Set({});m_configuration=std::move(configuration);camera_locked.checked.Set(m_applied_camera);Reload();
    }
    void Apply() {
        m_configuration.misc.sensitivity=sensitivity.position.Get()/100.0f;
        m_configuration.misc.invert=invert.checked.Get();m_configuration.misc.invert_2d=invert_2d.checked.Get();
        m_configuration.misc.damage_indicators=damage_indicators.checked.Get();m_configuration.misc.target_steering=false;
        m_applied_camera=camera_locked.checked.Get();
    }
    std::optional<std::string_view> FindConflict(std::string_view destination,content::InputId input) const {
        for(const auto function:content::remappable_functions) {
            const auto binding=m_configuration.Get(function);
            if(binding.primary!=input && binding.secondary!=input) continue;
            if(!Zoom(function) && !Zoom(destination)) return function;
            if(function!=destination && Zoom(function) && Zoom(destination)) return function;
        }
        return {};
    }
private:
    using Key=std::pair<std::uint32_t,std::uint32_t>;
    struct Field {std::string_view function;bool secondary;std::unique_ptr<engine::gui::w3d::InputCaptureModel> capture;};
    static bool Zoom(std::string_view function) {return function=="ZoomIn" || function=="ZoomOut";}
    std::optional<engine::gui::w3d::InputAssignment> Resolve(Key key,content::InputId input) {
        const auto found=m_fields.find(key);if(found==m_fields.end() || conflict.Get()) return {};
        const auto name=content::InputFileName(input);
        if(!input || name.empty() || name=="App_Menu_Key" || name.find("Windows_Key")!=name.npos ||
            name.find("Control_Key")!=name.npos || name.find("Alt_Key")!=name.npos) return {};
        auto& field=found->second;
        if(input==unsigned(content::KeyCode::del)) {
            m_configuration.Set(field.function,field.secondary,0);return engine::gui::w3d::InputAssignment{};
        }
        const auto previous=FindConflict(field.function,input);
        if(previous && *previous!=field.function) conflict.Set(ControlConflict{key.first,key.second,field.function,*previous,input});
        else m_configuration.Set(field.function,field.secondary,input);
        return engine::gui::w3d::InputAssignment{input,m_caption ? m_caption(input) : std::u16string{}};
    }
    void ReloadBindings(std::uint32_t tab=0) {
        for(auto& [key,field]:m_fields) if(!tab || key.first==tab) {
            const auto binding=m_configuration.Get(field.function);
            const auto input=field.secondary ? binding.secondary : binding.primary;
            field.capture->Restore({input,input && m_caption ? m_caption(input) : std::u16string{}});
        }
    }
    void Reload() {
        ReloadBindings();sensitivity.Set(static_cast<int>(m_configuration.misc.sensitivity*100));
        invert.checked.Set(m_configuration.misc.invert);invert_2d.checked.Set(m_configuration.misc.invert_2d);
        damage_indicators.checked.Set(m_configuration.misc.damage_indicators);
    }
    content::InputConfiguration m_configuration;Caption m_caption;bool m_applied_camera{};std::map<Key,Field> m_fields;
};
}
