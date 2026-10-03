export module games.renegade.presentation.menu.navigation;
import std;
import engine.gui.mvvm.observable;

export namespace renegade::presentation
{
constexpr bool IsPopupResource(std::uint32_t resource) {return resource==129 || resource==209 || resource==210;}
// Commando/renegadedialogmgr.cpp FactoryArray and Default_On_Command;
// dialogresource.h's link ids, resource.h's dialog ids. The VM owns the stack;
// views receive commands and never decide the destination of a game control.
class MenuNavigation {
public:
    engine::gui::mvvm::Observable<std::uint32_t> dialog{128};
    engine::gui::mvvm::Observable<bool> quitting{false};
    // MenuDialogClass::End_Dialog feedback belongs to closing a stacked menu,
    // rather than to every Escape key or popup cancellation.
    engine::gui::mvvm::Observable<std::uint64_t> closed_menus{0};
    MenuNavigation() { back.SetAction([this] { Back(); }); }
    engine::gui::mvvm::Command back;
    void RememberFocus(std::optional<std::uint32_t> control) { m_focus=control; }
    std::optional<std::uint32_t> Focus() const { return m_focus; }
    void Open(std::uint32_t resource) { if(resource!=dialog.Get()) { m_stack.push_back({dialog.Get(),m_focus});m_focus.reset();dialog.Set(resource); } }
    void Reset(std::uint32_t resource) {m_stack.clear();m_focus.reset();dialog.Set(resource);}
    void Bind(std::uint32_t id,std::function<void()> action) {auto& command=For(id);command.SetAction(std::move(action));command.enabled.Set(true);}
    void Back() {
        if(m_stack.empty()) { Open(129);return; }
        if(!IsPopupResource(dialog.Get())) closed_menus.Set(closed_menus.Get()+1);
        const auto previous=m_stack.back();m_stack.pop_back();m_focus=previous.focus;dialog.Set(previous.resource);
    }
    engine::gui::mvvm::Command& For(std::uint32_t id) {
        if(auto found=m_commands.find(id);found!=m_commands.end()) return *found->second;
        auto command=std::make_unique<engine::gui::mvvm::Command>();
        if(id==11034 || id==1179 || id==2) command->SetAction([this] { Back(); });
        else if(id==1000) command->SetAction([this] { if(dialog.Get()==129) quitting.Set(true); });
        else if(auto target=Target(id)) command->SetAction([this,target] { Open(*target); });
        else command->enabled.Set(false);
        auto& out=*command;m_commands.emplace(id,std::move(command));return out;
    }
    static std::optional<std::uint32_t> Target(std::uint32_t id) {
        switch(id) {
        case 11000:return 130;case 11003:return 135;case 11004:return 131;case 11006:return 145;
        case 11011:return 136;case 11012:return 167;case 11013:return 168;case 11014:return 169;
        case 11015:return 170;case 11016:return 171;case 11017:return 172;case 11018:return 129;
        case 11019:return 153;case 11020:return 128;case 11021:return 211;case 11027:return 166;
        case 1338:return 216;
        // LAN's browser and hosting screens will be bound to the session host.
        case 11030:return 174;
        default:return {};
        }
    }
private:
    struct Frame {std::uint32_t resource;std::optional<std::uint32_t> focus;};
    std::vector<Frame> m_stack;
    std::optional<std::uint32_t> m_focus;
    std::map<std::uint32_t,std::unique_ptr<engine::gui::mvvm::Command>> m_commands;
};
}
