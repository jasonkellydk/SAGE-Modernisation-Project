export module games.renegade.presentation.menu.multiplayer_options;
import std;
import engine.gui.w3d.value_controls;
import engine.config.adapters.preferences.preferences_file;

export namespace renegade::presentation {
// dlgmultiplayoptions.cpp On_Init_Dialog/Save_Settings and useroptions.cpp.
// Service-only WOL fields remain visible but unavailable; local player names
// are an independent game setting. No networking policy belongs to this VM.
class MultiplayerOptions final {
public:
    engine::gui::w3d::CheckValueModel player_names;
    explicit MultiplayerOptions(const engine::config::Preferences& preferences):
        m_applied(preferences.Number("ShowNamesOnSoldier",1)==1) {
        for(auto& check:m_service_checks) check.enabled.Set(false);
        Open();
    }
    void Open() {player_names.checked.Set(m_applied);}
    void Apply(engine::config::Preferences& preferences) {
        m_applied=player_names.checked.Get();preferences.Set("ShowNamesOnSoldier",std::int64_t(m_applied));
    }
    bool Applied() const noexcept {return m_applied;}
    engine::gui::w3d::CheckValueModel* Check(std::uint32_t id) {
        if(id==1214) return &player_names;
        if(id>=1215 && id<=1222) return &m_service_checks[id-1215];
        return nullptr;
    }
    static constexpr bool ServiceOnlyControl(std::uint32_t id) {
        return (id>=1215 && id<=1222) || id==1389;
    }
private:
    // Renegade's original service control IDs and disposition stay game-local.
    std::array<engine::gui::w3d::CheckValueModel,8> m_service_checks;
    bool m_applied{};
};
}
