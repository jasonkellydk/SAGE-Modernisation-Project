export module games.renegade.presentation.menu.performance_settings;
import std;
import engine.gui.w3d.value_controls;
import engine.config.adapters.preferences.preferences_file;

export namespace renegade::presentation {
// dlgconfigperformancetab.cpp: shadow, texture, particle, surface, geometry,
// static projector and NPatches. These are game policies, not engine presets.
using PerformanceValues=std::array<int,7>;
inline constexpr std::array<PerformanceValues,4> performance_presets{{
    {0,0,0,0,0,0,0},{1,1,0,0,0,0,0},{2,2,1,1,1,1,0},{3,2,2,2,2,1,0}}};
inline constexpr std::array<std::uint32_t,5> performance_slider_ids{1010,1012,1013,1015,1017};
struct PerformanceApplication final {
    int shadow_mode{2},texture_reduction{},particle_detail{1},surface_detail{2};
    int dynamic_budget{4000},static_budget{4000};
    bool static_projectors{true},npatches{};
    bool operator==(const PerformanceApplication&) const=default;
};
int GeometrySetting(int budget) {return budget<1000 ? 0 : budget<=5000 ? 1 : 2;}
// Source averages all matching preset levels per option, then rounds by +.4.
// NPatches=1 has no matching preset: ignore that unsupported value rather
// than converting the original divide-by-zero result to an integer.
int DeterminePerformance(PerformanceValues values) {
    float total{};unsigned options{};
    for(unsigned option=0;option<values.size();++option) {
        float sum{};unsigned count{};
        for(int level=3;level>=0;--level) if(performance_presets[level][option]==values[option]) {sum+=level;++count;}
        if(count) {total+=sum/count;++options;}
    }
    return options ? std::clamp(static_cast<int>(total/options+.4f),0,3) : 0;
}
class PerformanceSettings final {
public:
    std::array<engine::gui::w3d::SliderValueModel,5> sliders;
    engine::gui::w3d::SliderValueModel level;
    engine::gui::w3d::CheckValueModel expert,static_projectors,npatches;
    explicit PerformanceSettings(PerformanceApplication application={},bool supports_npatches=false)
        :m_applied(application),m_supports_npatches(supports_npatches) {
        for(unsigned i=0;i<sliders.size();++i) sliders[i].Range(0,i==0 ? 3 : 2);
        level.Range(0,3);npatches.enabled.Set(supports_npatches);
        const auto reduction=std::clamp(application.texture_reduction,0,2);
        Restore({application.shadow_mode,2-reduction,application.particle_detail,application.surface_detail,
            GeometrySetting(application.dynamic_budget),application.static_projectors,application.npatches && supports_npatches});
        // Observable subscriptions deliver the current value immediately.
        // Suppress preset commands until loaded expert values are rated.
        m_restoring=true;
        level.position.Subscribe([this](int value) {if(!m_restoring) Restore(performance_presets[value]);});
        Open();
    }
    PerformanceValues Values() const {
        return {sliders[0].position.Get(),sliders[1].position.Get(),sliders[2].position.Get(),sliders[3].position.Get(),
            sliders[4].position.Get(),static_projectors.checked.Get(),npatches.checked.Get()};
    }
    void Open() {
        expert.checked.Set(false);
        m_restoring=true;level.Set(DeterminePerformance(Values()));m_restoring=false;
    }
    const PerformanceApplication& Applied() const {return m_applied;}
    PerformanceApplication Apply() {
        const auto values=Values();constexpr std::array budgets{0,5000,10000};
        m_applied={values[0],2-values[1],values[2],values[3],budgets[values[4]],budgets[values[4]],
            bool(values[5]),m_supports_npatches ? bool(values[6]) : m_applied.npatches};
        return m_applied;
    }
    static bool ExpertControl(float control_top,float expert_top) {return control_top>expert_top;}
    engine::gui::w3d::SliderValueModel* Slider(std::uint32_t id) {
        if(id==1008) return &level;
        for(unsigned i=0;i<sliders.size();++i) if(performance_slider_ids[i]==id) return &sliders[i];
        return nullptr;
    }
    engine::gui::w3d::CheckValueModel* Check(std::uint32_t id) {
        switch(id) {case 1007:return &expert;case 1009:return &static_projectors;case 1011:return &npatches;default:return nullptr;}
    }
private:
    void Restore(PerformanceValues values) {
        m_restoring=true;
        for(unsigned i=0;i<sliders.size();++i) sliders[i].Set(values[i]);
        static_projectors.checked.Set(values[5]!=0);npatches.checked.Set(m_supports_npatches && values[6]!=0);
        m_restoring=false;
    }
    PerformanceApplication m_applied;bool m_supports_npatches{},m_restoring{};
};
PerformanceApplication ReadPerformancePreferences(const engine::config::Preferences& preferences) {
    const auto number=[&](std::string_view key,int fallback,int minimum,int maximum) {
        return static_cast<int>(std::clamp(preferences.Number(key,fallback),std::int64_t(minimum),std::int64_t(maximum)));
    };
    return {number("Shadow_Mode",2,0,3),number("Texture_Resolution",0,0,7),number("Particle_Detail",1,0,2),
        number("Surface_Effect_Detail",2,0,2),number("Dynamic_LOD_Budget",4000,0,100000),number("Static_LOD_Budget",4000,0,100000),
        preferences.Number("Static_Projectors",1)!=0,preferences.Number("NPatches",0)!=0};
}
void WritePerformancePreferences(engine::config::Preferences& preferences,PerformanceApplication application,bool supports_npatches=false) {
    preferences.Set("Shadow_Mode",application.shadow_mode);preferences.Set("Texture_Resolution",application.texture_reduction);
    preferences.Set("Particle_Detail",application.particle_detail);preferences.Set("Surface_Effect_Detail",application.surface_detail);
    preferences.Set("Dynamic_LOD_Budget",application.dynamic_budget);preferences.Set("Static_LOD_Budget",application.static_budget);
    preferences.Set("Dynamic_Projectors",application.shadow_mode!=0);preferences.Set("Static_Projectors",application.static_projectors);
    if(supports_npatches) preferences.Set("NPatches",application.npatches);
}
}
