export module games.renegade.presentation.menu.main_menu;
import std;
import engine.gui.mvvm.observable;

export namespace renegade::presentation
{
enum class MainAction { Campaign, Internet, Lan, Practice, Options, Quit };
enum class MenuScreen { Main, Campaign, Lan, Options, Quit };
enum class TransitionDirection { In, Out };

// Commando/mainmenutransition.cpp: frame-authored motion, separate entry and
// exit segments, clamped even when a presentation frame takes a long time.
class MainMenuTransition {
public:
    void Start(TransitionDirection direction) { m_direction=direction; m_frame=direction==TransitionDirection::In ? 27.0f : 66.0f; }
    void Advance(float seconds,float frame_rate) {
        if (seconds>=0 && std::isfinite(seconds) && frame_rate>0 && std::isfinite(frame_rate))
            m_frame=std::min(End(),m_frame+seconds*frame_rate);
    }
    float Frame() const { return m_frame; }
    bool Done() const { return m_frame>=End(); }
    bool ModelVisible() const { return m_direction==TransitionDirection::In || !Done(); }
    std::string_view Sound() const { return m_direction==TransitionDirection::In ? "interface_mainmove.wav" : "interface_movezoom.wav"; }
private:
    float End() const { return m_direction==TransitionDirection::In ? 65.0f : 99.0f; }
    TransitionDirection m_direction{TransitionDirection::In};
    float m_frame{};
};

class MainMenuViewModel {
public:
    MainMenuViewModel() {
        campaign.SetAction([this] { Navigate(MenuScreen::Campaign); });
        lan.SetAction([this] { Navigate(MenuScreen::Lan); });
        options.SetAction([this] { Navigate(MenuScreen::Options); });
        quit.SetAction([this] { Navigate(MenuScreen::Quit); });
        cancel.SetAction([this] { if (screen.Get()==MenuScreen::Main) Navigate(MenuScreen::Quit); else Navigate(MenuScreen::Main); });
        // Internet browser/services are outside the requested migration scope.
        internet.enabled.Set(false);
        practice.enabled.Set(false);
    }
    engine::gui::mvvm::Observable<MenuScreen> screen{MenuScreen::Main};
    engine::gui::mvvm::Command campaign, internet, lan, practice, options, quit, cancel;
    engine::gui::mvvm::Command& Action(MainAction action) {
        switch(action) {
        case MainAction::Campaign:return campaign; case MainAction::Internet:return internet;
        case MainAction::Lan:return lan; case MainAction::Practice:return practice;
        case MainAction::Options:return options; case MainAction::Quit:return quit;
        }
        return cancel;
    }
private:
    void Navigate(MenuScreen next) { screen.Set(next); }
};
}
