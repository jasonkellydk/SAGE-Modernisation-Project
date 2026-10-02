export module games.renegade.hud.vitals_view_model;
import std;
export import engine.gui.mvvm.observable;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.components.shield;

export namespace renegade::hud
{
struct VitalsViewModel
{
	engine::gui::mvvm::Observable<std::string> healthText;
	engine::gui::mvvm::Observable<std::string> armorText;
	void Update(const engine::gameplay::Health &health, const engine::gameplay::Shield &shield)
	{
		healthText.Set(std::to_string(health.current.Raw() / Engine::Math::Fixed::OneRaw));
		armorText.Set(std::to_string(shield.current.Raw() / Engine::Math::Fixed::OneRaw));
	}
};
}
