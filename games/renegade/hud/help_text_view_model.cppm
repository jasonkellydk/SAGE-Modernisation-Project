export module games.renegade.hud.help_text_view_model;
import std;
export import engine.gui.mvvm.observable;
export import engine.time.value_transition;

export namespace renegade::hud {
// Combat/hud.cpp HUD_Help_Text_Render: two seconds at half opacity,
// then a two-second fade. The caller supplies the paused simulation clock.
class HelpTextViewModel {
public:
    using Fixed=Engine::Math::Fixed;
    engine::gui::mvvm::Observable<std::u16string> text;
    engine::gui::mvvm::Observable<Fixed> opacity;
    engine::gui::mvvm::Observable<std::array<Fixed,3>> color;
    void Show(std::u16string value,std::array<Fixed,3> tint,Fixed now) {
        if(std::ranges::any_of(tint,[](Fixed channel) {return channel<Fixed{} || channel>Fixed::One();}))
            throw std::invalid_argument("HUD help color outside unit range");
        text.Set(std::move(value));color.Set(tint);
        m_fade={Fixed::FromRatio(1,2),{},now+Fixed::FromInt(2),Fixed::FromInt(2)};
        Update(now);
    }
    void Update(Fixed now) {
        if(text.Get().empty()) {opacity.Set({});return;}
        opacity.Set(engine::time::Sample(m_fade,now));
        if(opacity.Get()==Fixed{}) text.Set({});
    }
private:
    engine::time::ValueTransition m_fade;
};
}
