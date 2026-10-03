export module games.renegade.hud.objective_view_model;
import std;
export import engine.gui.mvvm.observable;
export import games.renegade.gameplay.missions.components.objective;

export namespace renegade::hud {
struct ObjectiveRow {
    std::uint64_t identity{};MissionObjective value;Engine::Math::Fixed age;Engine::Math::FixedVector3 position;
};
struct ObjectiveIcon {std::uint32_t token{};Engine::Math::Fixed left{},top{},right{},bottom{},fly{};};
// Combat/hud.cpp Objective_Update: pending positive-priority objectives,
// priority order, reset selection when the current indexed object changes.
class ObjectiveViewModel {
public:
    engine::gui::mvvm::Observable<std::uint32_t> selected;
    engine::gui::mvvm::Observable<bool> visible;
    engine::gui::mvvm::Command cycle;
    ObjectiveViewModel() {cycle.SetAction([this] {if(!m_rows.empty()) {selected.Set((selected.Get()+1)%std::uint32_t(m_rows.size()));m_identity=m_rows[selected.Get()].identity;}});}
    void Update(std::vector<ObjectiveRow> rows) {
        std::erase_if(rows,[](const auto& row) {return row.value.status!=ObjectiveStatus::Pending || row.value.priority<=Engine::Math::Fixed{};});
        std::ranges::stable_sort(rows,[](const auto& a,const auto& b) {return a.value.priority!=b.value.priority ? a.value.priority>b.value.priority : a.identity<b.identity;});
        if(selected.Get()>=rows.size() || rows[selected.Get()].identity!=m_identity) selected.Set(0);
        m_rows=std::move(rows);m_identity=m_rows.empty() ? 0 : m_rows[selected.Get()].identity;
        visible.Set(!m_rows.empty());cycle.enabled.Set(!m_rows.empty());
    }
    const ObjectiveRow* Current() const noexcept {return m_rows.empty() ? nullptr : &m_rows[selected.Get()];}
    const std::vector<ObjectiveRow>& Rows() const noexcept {return m_rows;}
    std::vector<ObjectiveIcon> Icons(unsigned width,unsigned height) const {
        using Engine::Math::Fixed;
        std::vector<ObjectiveIcon> result;
        for(std::size_t reverse=m_rows.size();reverse>0;--reverse) {
            const auto slot=reverse-1,index=(slot+selected.Get())%m_rows.size();const auto& row=m_rows[index];
            const auto fly=std::clamp(Fixed::FromInt(2)-row.age,Fixed{},Fixed::One());
            const auto left=Fixed::FromInt(width)-Fixed::FromInt(80)+Fixed::FromInt(10*slot)-Fixed::FromRatio(3*width,8)*fly;
            const auto top=Fixed::FromInt(8)+Fixed::FromRatio(height,2)*fly;
            result.push_back({row.value.pog,left,top,left+Fixed::FromInt(64),top+Fixed::FromInt(64),fly});
        }return result;
    }
    static std::int64_t Range(const ObjectiveRow& row,Engine::Math::FixedVector3 player) {
        return (Engine::Math::Length(row.position-player).Raw()/Engine::Math::Fixed::OneRaw/10)*10;
    }
private:
    std::vector<ObjectiveRow> m_rows;std::uint64_t m_identity{};
};
}
