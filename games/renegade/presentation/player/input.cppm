export module games.renegade.presentation.player.input;
import std;
export import engine.platform.events;
export import games.renegade.content.presentation.input_configuration;
export import games.renegade.gameplay.humans.components.motion;
export import Engine.Core.Math.FixedLookAngles;
export import games.renegade.gameplay.weapons.components.weapon_state;

export namespace renegade::presentation {
struct PlayerWeaponCommand {WeaponInput input;engine::gameplay::InventoryControl inventory;};
// Presentation input owner, injected with the already-bound control profile.
// It produces tick commands; authoritative character state remains in SoA.
class PlayerInput {
public:
    explicit PlayerInput(content::InputConfiguration configuration):m_configuration(std::move(configuration)) {}
    void Reset(Engine::Math::TurnAngle facing) {ClearHeld();m_facing=facing;m_pitch={};m_enabled=true;m_control_allowed=true;}
    void ClearHeld() {m_keys.fill(false);m_jump=false;m_action=false;m_view_toggle=false;m_objective_cycle=false;m_reload=false;m_inventory={};}
    void SetFocused(bool focused) {ClearHeld();m_enabled=focused;}
    void SetControlAllowed(bool allowed) {if(allowed!=m_control_allowed) ClearHeld();m_control_allowed=allowed;}
    Engine::Math::LookAngles Orientation() const noexcept {return {m_facing,-m_pitch};}
    // Script camera commands are independent of the local input permission.
    // They do not clear held buttons or move the authoritative player body.
    void SetOrientation(Engine::Math::LookAngles angles) noexcept {m_facing=angles.heading;m_pitch=-angles.pitch;}
    void Configure(content::InputConfiguration configuration) {m_configuration=std::move(configuration);ClearHeld();}
    void Handle(const engine::platform::PlatformEvent& event) {
        using namespace engine::platform;
        if(event.type==EventType::focus_lost) {ClearHeld();m_enabled=false;return;}
        if(event.type==EventType::focus_gained) {m_enabled=true;return;}
        if(!m_control_allowed) return;
        if(event.type==EventType::key_down || event.type==EventType::key_up) {
            const auto key=static_cast<unsigned>(event.key);if(!key || key>=static_cast<unsigned>(KeyCode::count)) return;
            const bool down=event.type==EventType::key_down;const bool edge=down && !event.repeat && !m_keys[key];m_keys[key]=down;
            if(edge && Matches("Jump",key)) m_jump=true;
            if(edge && Matches("Action",key)) m_action=true;
            if(edge && Matches("FirstPersonToggle",key)) m_view_toggle=!m_view_toggle;
            if(edge && Matches("CyclePog",key)) m_objective_cycle=true;
            if(edge) WeaponEdge(key);
        } else if(event.type==EventType::mouse_button_down || event.type==EventType::mouse_button_up) {
            const unsigned key=event.code==1 ? 260 : event.code==3 ? 261 : event.code==2 ? 262 : 0;
            if(!key) return;const bool down=event.type==EventType::mouse_button_down;const bool edge=down && !m_keys[key];m_keys[key]=down;
            if(edge) WeaponEdge(key);
        } else if(event.type==EventType::mouse_wheel && m_enabled && std::isfinite(event.y)) {
            const auto amount=event.y*(event.flipped ? -1 : 1);if(amount!=0) WeaponEdge(amount>0 ? 269 : 270);
        } else if(event.type==EventType::mouse_moved && m_enabled && std::isfinite(event.x) && std::isfinite(event.y)) {
            using namespace Engine::Math;
            const auto scale=m_configuration.misc.MouseScale();
            // Original input.cpp integrates relative mouse axes in radians.
            const auto radians=[](float value) {return Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(value));};
            m_facing-=TurnFromRadians(radians(event.x*scale));
            // SDL's positive Y points down; the camera's positive tilt also
            // points down. Inversion reverses that ordinary screen convention.
            m_pitch+=radians(event.y*scale*(m_configuration.misc.invert ? -1.f : 1.f));
            // CCameraClass::Handle_Input clamps ordinary input to +/-80deg.
            const auto limit=Radians(TurnFromDegrees(80));m_pitch=std::clamp(m_pitch,-limit,limit);
        }
    }
    HumanControl Sample() {
        using namespace Engine::Math;
        HumanControl control;control.forward=Fixed::FromInt(int(Down("MoveForward"))-int(Down("MoveBackward")));
        control.left=Fixed::FromInt(int(Down("MoveLeft"))-int(Down("MoveRight")));
        control.facing=m_facing;control.enabled=m_enabled && m_control_allowed;control.jump=std::exchange(m_jump,false);control.action=std::exchange(m_action,false);return control;
    }
    Engine::Math::Fixed Pitch() const noexcept {return m_pitch;}
    bool TakeViewToggle() noexcept {return std::exchange(m_view_toggle,false);}
    bool TakeObjectiveCycle() noexcept {return std::exchange(m_objective_cycle,false);}
    PlayerWeaponCommand SampleWeapon() {
        PlayerWeaponCommand command;command.input.primary=Down("FireWeaponPrimary");command.input.secondary=Down("FireWeaponSecondary");
        command.input.permitted=m_enabled && m_control_allowed;command.input.reload=std::exchange(m_reload,false);
        command.inventory=std::exchange(m_inventory,engine::gameplay::InventoryControl{});
        if(!command.input.permitted) {command.input.primary=command.input.secondary=command.input.reload=0;command.inventory={};}
        return command;
    }
private:
    void WeaponEdge(unsigned input) {
        using namespace engine::gameplay;
        if(Matches("ReloadWeapon",input)) m_reload=true;
        if(Matches("NextWeapon",input)) m_inventory={InventoryOperation::Next};
        if(Matches("PrevWeapon",input)) m_inventory={InventoryOperation::Previous};
        if(Matches("SelectNoWeapon",input)) m_inventory={InventoryOperation::Clear};
        for(unsigned group=0;group<10;++group) if(Matches("SelectWeapon"+std::to_string(group),input)) m_inventory={InventoryOperation::Group,group};
    }
    bool Matches(std::string_view function,unsigned input) const {const auto binding=m_configuration.Get(function);return input && (binding.primary==input || binding.secondary==input);}
    bool Down(std::string_view function) const {
        const auto binding=m_configuration.Get(function);const auto pressed=[this](unsigned input) {return input && input<m_keys.size() && m_keys[input];};
        return m_enabled && m_control_allowed && (pressed(binding.primary) || pressed(binding.secondary));
    }
    content::InputConfiguration m_configuration;
    std::array<bool,275> m_keys{};
    Engine::Math::TurnAngle m_facing;Engine::Math::Fixed m_pitch;
    bool m_jump{},m_action{},m_view_toggle{},m_objective_cycle{},m_enabled{true},m_control_allowed{true};
    bool m_reload{};engine::gameplay::InventoryControl m_inventory;
};
}
