export module games.renegade.presentation.menu.single_player;
import std;
import games.renegade.content.campaign.campaign_flow;
import games.renegade.presentation.menu.navigation;
import engine.gui.mvvm.observable;

export namespace renegade::presentation {
struct SinglePlayerStart {
    std::string map;
    int difficulty{};
    bool tutorial{};
};
// The host injects the session start operation. No platform, renderer, loader,
// configuration document, or service singleton belongs in this view model.
class SinglePlayerMenu final {
public:
    using Start=std::function<std::expected<void,std::string>(const SinglePlayerStart&)>;
    engine::gui::mvvm::Observable<bool> active{false};
    engine::gui::mvvm::Observable<int> difficulty{0};
    engine::gui::mvvm::Observable<std::string> error;
    SinglePlayerMenu(MenuNavigation& navigation,content::CampaignFlow flow,Start start):
        m_navigation(navigation),m_flow(std::move(flow)),m_start(std::move(start)) {
        m_navigation.Bind(11005,[this] {
            // dialogtests.cpp StartSPGameDialogClass::On_Command resets the
            // campaign and starts tutorial directly, with Backdrop90.
            if(m_navigation.dialog.Get()==130) Begin({"M00_Tutorial.mix",0,true});
        });
        for(unsigned id=11007;id<=11010;++id) m_navigation.Bind(id,[this,id] {
            if(m_navigation.dialog.Get()!=131 || active.Get()) return;
            // DifficultyMenuClass uses control id minus IDC_DIFFCULTY01.
            // Start_Campaign resets its cursor then Continue selects entry 0.
            if(m_flow.steps.empty() || m_flow.steps.front().kind!=content::CampaignStepKind::Level) {
                error.Set("campaign must begin with a level in the current session host");return;
            }
            Begin({m_flow.steps.front().asset,int(id-11007),false});
        });
    }
    void ReturnToMenu() {active.Set(false);error.Set({});m_navigation.Reset(128);}
private:
    void Begin(const SinglePlayerStart& request) {
        if(active.Get()) return;
        error.Set({});
        const auto result=m_start ? m_start(request) : std::expected<void,std::string>(std::unexpected("single-player session host unavailable"));
        if(!result) {error.Set(result.error());return;}
        difficulty.Set(request.difficulty);active.Set(true);
    }
    MenuNavigation& m_navigation;
    content::CampaignFlow m_flow;
    Start m_start;
};
}
