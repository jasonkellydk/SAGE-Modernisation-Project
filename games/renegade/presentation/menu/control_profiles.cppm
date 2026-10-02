export module games.renegade.presentation.menu.control_profiles;
import std;
import games.renegade.content.input.profile_catalog;
import engine.gui.w3d.text_edit;
import engine.gui.w3d.list_selection;

export namespace renegade::presentation {
enum class ProfileAction { None,NeedName,PromptOverwrite,PromptDelete,Create,Save,Load,Delete };
struct ProfileRequest {
    ProfileAction action{};std::string filename;std::u16string name;
    bool operator==(const ProfileRequest&) const=default;
};
// The VM owns selection/edit/prompt state. The composition root performs file
// effects, commits the catalog and reloads Controls after a successful load.
class ControlProfiles final {
public:
    engine::gui::w3d::ListSelectionModel list;
    engine::gui::w3d::TextEditModel name;
    explicit ControlProfiles(const content::InputProfileCatalog& catalog):m_catalog(catalog) {
        list.selected.Subscribe([this](int){UpdateName();});
    }
    void Open() {Refresh();list.Select(std::int64_t(m_rows.size())-1);m_pending={};}
    void Refresh(bool preserve_selection=false) {
        const auto selected=Selected();const std::string filename=selected ? selected->filename : std::string{};
        m_rows.clear();for(std::size_t i=0;i<m_catalog.profiles.size();++i) m_rows.push_back(i);
        std::ranges::stable_sort(m_rows,[&](auto a,auto b) {
            const auto& first=m_catalog.profiles[a];const auto& second=m_catalog.profiles[b];
            if(first.default_profile!=second.default_profile) return first.default_profile;
            if(first.custom!=second.custom) return first.custom;
            // Original wcsicmp depends on the OS locale. Fold ASCII for the
            // English retail names and order other UTF-16 scalars consistently.
            const auto fold=[](char16_t c){return c>=u'A' && c<=u'Z' ? char16_t(c+32) : c;};
            return std::lexicographical_compare(first.name.begin(),first.name.end(),second.name.begin(),second.name.end(),
                [&](auto x,auto y){return fold(x)<fold(y);});
        });
        m_rows.push_back(m_catalog.profiles.size());std::vector<float> heights(m_rows.size(),1);
        list.Configure(heights,float(m_rows.size()+1));
        if(preserve_selection) {
            auto row=std::ranges::find_if(m_rows,[&](auto index){return index<m_catalog.profiles.size() && m_catalog.profiles[index].filename==filename;});
            list.Select(row==m_rows.end() ? std::int64_t(m_rows.size())-1 : row-m_rows.begin());
        }
        UpdateName();
    }
    std::span<const std::size_t> Rows() const {return m_rows;}
    const content::InputProfile* Selected() const {
        const auto index=list.selected.Get();if(index<0 || std::size_t(index)>=m_rows.size() || m_rows[index]>=m_catalog.profiles.size()) return nullptr;
        return &m_catalog.profiles[m_rows[index]];
    }
    ProfileRequest Save() {
        const auto selected=Selected();
        if(selected && !selected->custom) return {};
        if(name.text.Get().empty()) return {ProfileAction::NeedName};
        if(!selected) return {ProfileAction::Create,{},name.text.Get()};
        m_pending={ProfileAction::Save,selected->filename,name.text.Get()};
        return {ProfileAction::PromptOverwrite,selected->filename,name.text.Get()};
    }
    ProfileRequest Load() const {
        const auto selected=Selected();return selected ? ProfileRequest{ProfileAction::Load,selected->filename,selected->name} : ProfileRequest{};
    }
    ProfileRequest Delete() {
        const auto selected=Selected();if(!selected || !selected->custom) return {};
        m_pending={ProfileAction::Delete,selected->filename,selected->name};
        return {ProfileAction::PromptDelete,selected->filename,selected->name};
    }
    ProfileRequest Confirm(bool yes) {auto request=std::exchange(m_pending,{});return yes ? request : ProfileRequest{};}
    bool Pending() const {return m_pending.action!=ProfileAction::None;}
private:
    void UpdateName() {
        const auto selected=Selected();name.enabled.Set(!selected || selected->custom);name.Restore(selected ? selected->name : std::u16string{});
    }
    const content::InputProfileCatalog& m_catalog;std::vector<std::size_t> m_rows;ProfileRequest m_pending;
};
}
