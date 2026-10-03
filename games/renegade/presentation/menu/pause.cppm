export module games.renegade.presentation.menu.pause;
import std;
import games.renegade.presentation.menu.navigation;
import engine.gui.mvvm.observable;
import engine.gui.w3d.navigation;

export namespace renegade::presentation {
// dlgevaencyclopedia.cpp: Escape opens EVA, Back resumes the same game,
// Main Menu prompts before tearing it down. Child options keep it paused.
class PauseMenu {
public:
    engine::gui::mvvm::Observable<bool> active{false};
    engine::gui::w3d::TabSelectionModel tabs{7};
    engine::gui::mvvm::Observable<unsigned>& selected_tab=tabs.position;
    static constexpr std::array<unsigned,7> pages{146,147,148,149,150,151,152};
    static constexpr bool SoloControlVisible(unsigned id) noexcept {return id!=1524 && id!=1525;}
    PauseMenu(MenuNavigation& navigation,std::function<void(bool)> changed,std::function<void()> leave):
        m_navigation(navigation),m_changed(std::move(changed)),m_leave(std::move(leave)) {
        navigation.Bind(11034,[this] {if(active.Get() && m_navigation.dialog.Get()==153) Resume();else m_navigation.Back();});
        navigation.Bind(11020,[this] {if(active.Get()) {m_exit_pending=true;m_navigation.Open(209);} else m_navigation.Open(128);});
    }
    void Open() {
        if(active.Get()) return;
        m_exit_pending=false;active.Set(true);if(m_changed) m_changed(true);m_navigation.Reset(153);
    }
    void Resume() {if(active.Get()) {m_exit_pending=false;active.Set(false);if(m_changed) m_changed(false);}}
    bool ExitPending() const noexcept {return m_exit_pending;}
    void ConfirmExit(bool accepted) {
        if(!m_exit_pending) return;
        m_exit_pending=false;
        if(accepted) {active.Set(false);if(m_leave) m_leave();} else m_navigation.Back();
    }
    void SelectTab(unsigned index) {if(index<pages.size()) tabs.Select(index);}
private:
    MenuNavigation& m_navigation;std::function<void(bool)> m_changed;std::function<void()> m_leave;bool m_exit_pending{};
};
}
