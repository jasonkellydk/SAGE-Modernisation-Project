export module games.renegade.hud.player_hud_view_model;
import std;
export import games.renegade.hud.vitals_view_model;
import engine.gameplay.common.inventory.components.magazine;

export namespace renegade::hud {
using Engine::Math::Fixed;
// Combat/hud.cpp Info_Update_Health_Shield. Render interpolation is cosmetic;
// health/shield values always come from the completed authoritative ECS tick.
class PlayerHudViewModel {
public:
    engine::gui::mvvm::Observable<std::u16string> healthText,shieldText;
    engine::gui::mvvm::Observable<Fixed> healthFraction,shieldFraction,healthColorFraction,crossFlash,centerHealthAlpha;
    engine::gui::mvvm::Observable<bool> shieldVisible;
    engine::gui::mvvm::Observable<std::u16string> clipText,reserveText,weaponName;
    engine::gui::mvvm::Observable<std::uint32_t> weaponDefinition;
    engine::gui::mvvm::Observable<Fixed> centerClipAlpha;
    void UpdateWeapon(std::uint32_t definition,std::u16string name,const engine::gameplay::Magazine* magazine,Fixed elapsed) {
        if(elapsed<Fixed{}) throw std::invalid_argument("negative HUD elapsed time");weaponDefinition.Set(definition);weaponName.Set(std::move(name));
        if(!magazine) {clipText.Set({});reserveText.Set({});centerClipAlpha.Set(Fixed{});return;}
        const auto number=[](std::int32_t value) {if(value<0) return std::u16string(u"999");auto text=std::to_string(value%1000);text.insert(0,3-text.size(),'0');return std::u16string(text.begin(),text.end());};
        clipText.Set(number(magazine->loaded));reserveText.Set(number(magazine->reserve));
        if(magazine->loaded!=m_last_clip) {m_last_clip=magazine->loaded;m_clip_remaining=Fixed::FromInt(2);}
        centerClipAlpha.Set(std::clamp(m_clip_remaining,Fixed{},Fixed::One()));m_clip_remaining=std::max(Fixed{},m_clip_remaining-elapsed);
    }
    void Update(const engine::gameplay::Health& health,const engine::gameplay::Shield& shield,Fixed elapsed) {
        if(elapsed<Fixed{}) throw std::invalid_argument("negative HUD elapsed time");
        const auto fraction=[](Fixed value,Fixed maximum) {return maximum>Fixed{} ? std::clamp(value/maximum,Fixed{},Fixed::One()) : Fixed{};};
        const auto health_target=fraction(health.current,health.maximum),shield_target=fraction(shield.current,shield.maximum);
        const auto ease=[&](Fixed previous,Fixed target) {return previous+std::clamp(target-previous,-elapsed,elapsed);};
        healthFraction.Set(ease(healthFraction.Get(),health_target));shieldFraction.Set(ease(shieldFraction.Get(),shield_target));
        healthColorFraction.Set(std::max(healthFraction.Get(),health_target));shieldVisible.Set(shieldFraction.Get()>Fixed{});
        healthText.Set(Number(health.current,true));shieldText.Set(Number(shield.current,false));
        m_flash+=elapsed*Fixed::FromInt(4);const auto cycle=Fixed::FromInt(2);m_flash=Fixed::FromRaw(m_flash.Raw()%cycle.Raw());
        if(health_target>Fixed::FromRatio(1,4)) m_flash={};
        crossFlash.Set(m_flash>Fixed::One() ? cycle-m_flash : m_flash);
        if(health.current!=m_last_health || health_target<=Fixed::FromRatio(1,4)) {
            m_last_health=health.current;m_center_remaining=Fixed::FromInt(2);
        }
        centerHealthAlpha.Set(std::clamp(m_center_remaining,Fixed{},Fixed::One()));
        m_center_remaining=std::max(Fixed{},m_center_remaining-elapsed);
    }
    static std::u16string Number(Fixed value,bool living_minimum) {
        value=std::max(value,Fixed{});if(living_minimum && value>Fixed{} && value<Fixed::One()) value=Fixed::One();
        // Retail x86 fistp uses round-to-nearest-even. Spell it explicitly so
        // display rounding is stable on modern platforms and independent of FPU.
        auto integer=value.Raw()/Fixed::OneRaw;const auto remainder=value.Raw()%Fixed::OneRaw;
        if(remainder>Fixed::OneRaw/2 || remainder==Fixed::OneRaw/2 && integer%2) ++integer;
        auto text=std::to_string(integer%10000);if(text.size()<3) text.insert(0,3-text.size(),'0');
        return {text.begin(),text.end()};
    }
private:Fixed m_flash,m_last_health,m_center_remaining,m_clip_remaining;std::int32_t m_last_clip{};
};
}
