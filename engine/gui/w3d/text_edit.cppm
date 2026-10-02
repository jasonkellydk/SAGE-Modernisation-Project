export module engine.gui.w3d.text_edit;
import std;
import engine.gui.mvvm.observable;

export namespace engine::gui::w3d {
// SDL text events and clipboard strings use UTF-8; W3D fonts and authored text
// use UTF-16. Reject malformed input atomically and preserve scalar boundaries.
std::optional<std::u16string> DecodeEditText(std::string_view bytes) {
    std::u16string result;
    for(std::size_t i=0;i<bytes.size();) {
        const auto first=static_cast<unsigned char>(bytes[i++]);std::uint32_t value{};unsigned count{};
        if(first<0x80) value=first;
        else if(first>=0xc2 && first<=0xdf) {value=first&31;count=1;}
        else if(first>=0xe0 && first<=0xef) {value=first&15;count=2;}
        else if(first>=0xf0 && first<=0xf4) {value=first&7;count=3;}
        else return {};
        if(count>bytes.size()-i) return {};
        for(unsigned j=0;j<count;++j) {const auto next=static_cast<unsigned char>(bytes[i++]);if((next&0xc0)!=0x80) return {};value=(value<<6)|(next&63);}
        if((count==1 && value<0x80) || (count==2 && value<0x800) || (count==3 && value<0x10000) ||
            value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return {};
        if(value<0x10000) result.push_back(static_cast<char16_t>(value));
        else {value-=0x10000;result.push_back(static_cast<char16_t>(0xd800+(value>>10)));result.push_back(static_cast<char16_t>(0xdc00+(value&1023)));}
    }
    return result;
}
enum class EditAction { Backspace,Delete,Home,End,Left,Right,Enter,SelectAll };
struct EditKeyResult {bool handled{},submit{};};
struct EditSelection {std::size_t caret{},anchor{};bool operator==(const EditSelection&) const=default;};
struct EditComposition {std::u16string text;std::size_t start{},length{};bool operator==(const EditComposition&) const=default;};
class TextEditModel final {
public:
    mvvm::Observable<std::u16string> text;
    mvvm::Observable<bool> enabled{true},focused{false};
    mvvm::Observable<EditSelection> selection;
    mvvm::Observable<EditComposition> composition;
    explicit TextEditModel(std::size_t limit=65535):m_limit(std::min<std::size_t>(limit,65535)) {}
    bool Restore(std::u16string value) {
        if(value.size()>m_limit || !Valid(value)) return false;
        text.Set(std::move(value));selection.Set({0,0});composition.Set({});return true;
    }
    void Focus(bool value,std::uint64_t now) {
        if(focused.Get()==value) return;
        focused.Set(value);composition.Set({});selection.Set(value ? EditSelection{text.Get().size(),0} : EditSelection{});
        m_blink_time=now;m_caret_visible=value;
    }
    void Tick(std::uint64_t now) {
        // EditCtrl uses a strict >500ms edge and one toggle per render update.
        if(now>=m_blink_time && now-m_blink_time>500) {m_caret_visible=!m_caret_visible;m_blink_time=now;}
    }
    bool CaretVisible() const {return enabled.Get() && focused.Get() && m_caret_visible;}
    std::pair<std::size_t,std::size_t> Selection() const {const auto s=selection.Get();return std::minmax(s.caret,s.anchor);}
    void Place(std::size_t position,bool extend,std::uint64_t now) {
        position=Boundary(text.Get(),std::min(position,text.Get().size()));
        selection.Set({position,extend ? selection.Get().anchor : position});m_blink_time=now;m_caret_visible=true;composition.Set({});
    }
    bool InsertUtf8(std::string_view bytes,std::uint64_t now) {
        if(!enabled.Get() || !focused.Get()) return false;
        const auto decoded=DecodeEditText(bytes);if(!decoded) return false;
        std::u16string filtered;for(const auto c:*decoded) if(c>=32 && c!=127) filtered.push_back(c);
        if(filtered.empty()) return false;
        auto value=text.Get();const auto [first,last]=Selection();value.erase(first,last-first);
        auto count=std::min(filtered.size(),m_limit-value.size());count=Boundary(filtered,count);
        value.insert(first,filtered.substr(0,count));text.Set(std::move(value));Place(first+count,false,now);return true;
    }
    bool ComposeUtf8(std::string_view bytes,std::int32_t start,std::int32_t length) {
        if(!enabled.Get() || !focused.Get() || start < -1 || length < -1) return false;
        const auto decoded=DecodeEditText(bytes);if(!decoded || decoded->size()>m_limit) return false;
        // SDL editing offsets are Unicode characters, not UTF-8 bytes.
        const auto offset=[&](std::size_t scalars) {std::size_t i{};while(i<decoded->size() && scalars--) i=Next(*decoded,i);return i;};
        const auto first=offset(static_cast<unsigned>(std::max(0,start))),last=offset(std::uint64_t(std::max(0,start))+std::max(0,length));
        composition.Set({*decoded,first,last-first});return true;
    }
    EditKeyResult Key(EditAction action,bool shift,bool control,std::uint64_t now) {
        if(!enabled.Get() || !focused.Get()) return {};
        auto value=text.Get();auto position=selection.Get().caret;const auto [first,last]=Selection();
        switch(action) {
        case EditAction::Enter:return {true,true};
        case EditAction::SelectAll:selection.Set({value.size(),0});return {true,false};
        case EditAction::Backspace:case EditAction::Delete:
            if(first!=last) {value.erase(first,last-first);position=first;}
            else if(action==EditAction::Backspace && position) {const auto before=Previous(value,position);value.erase(before,position-before);position=before;}
            else if(action==EditAction::Delete && position<value.size()) value.erase(position,Next(value,position)-position);
            text.Set(std::move(value));Place(position,false,now);return {true,false};
        case EditAction::Home:position=0;break;case EditAction::End:position=value.size();break;
        case EditAction::Left:position=control ? Word(value,position,-1) : Previous(value,position);break;
        case EditAction::Right:position=control ? Word(value,position,1) : Next(value,position);break;
        }
        Place(position,shift,now);return {true,false};
    }
private:
    static bool High(char16_t c) {return c>=0xd800 && c<=0xdbff;}
    static bool Low(char16_t c) {return c>=0xdc00 && c<=0xdfff;}
    static bool Valid(std::u16string_view text) {
        for(std::size_t i=0;i<text.size();++i) {if(!text[i] || text[i]<32) return false;if(High(text[i])) {if(++i==text.size() || !Low(text[i])) return false;}else if(Low(text[i])) return false;}return true;
    }
    static std::size_t Boundary(std::u16string_view text,std::size_t p) {return p && p<text.size() && High(text[p-1]) && Low(text[p]) ? p-1 : p;}
    static std::size_t Previous(std::u16string_view text,std::size_t p) {return p ? Boundary(text,p-1) : 0;}
    static std::size_t Next(std::u16string_view text,std::size_t p) {return p<text.size() ? p+(High(text[p]) ? 2 : 1) : text.size();}
    static std::size_t Word(std::u16string_view text,std::size_t p,int direction) {
        for(std::int64_t i=std::int64_t(p)+direction;i>=0 && i<std::int64_t(text.size());i+=direction)
            if(text[i]!=u' ' && (!i || text[i-1]==u' ')) return Boundary(text,static_cast<std::size_t>(i));
        return direction<0 ? 0 : text.size();
    }
    std::size_t m_limit;std::uint64_t m_blink_time{};bool m_caret_visible{};
};
}
