export module engine.gui.w3d.marquee;
import std;
import engine.gui.mvvm.observable;

export namespace engine::gui::w3d {
struct MarqueeLine {std::u16string text;unsigned font{};std::uint32_t color{};};

namespace marquee_detail {
bool Prefix(std::u16string_view text,std::u16string_view prefix) {
    if(text.size()<prefix.size()) return false;
    for(std::size_t i=0;i<prefix.size();++i) {
        auto ch=text[i];if(ch>=u'A' && ch<=u'Z') ch+=u'a'-u'A';
        if(ch!=prefix[i]) return false;
    }
    return true;
}
unsigned ColorByte(std::u16string_view value) {
    std::size_t i{};while(i<value.size() && (value[i]==u' ' || (value[i]>=u'\t' && value[i]<=u'\r'))) ++i;
    bool negative=false;if(i<value.size() && (value[i]==u'-' || value[i]==u'+')) negative=value[i++]==u'-';
    unsigned result{};
    while(i<value.size() && value[i]>=u'0' && value[i]<=u'9') result=(result*10+unsigned(value[i++]-u'0'))&255u;
    return negative ? (256u-result)&255u : result;
}
}

// TextMarqueeCtrl::Read_Line/Read_Tag: each line starts with default style.
// Recognized tags replace the text start, including discarding preceding text;
// unknown tags remain literal. Keep this legacy markup behavior in the widget.
std::expected<std::vector<MarqueeLine>,std::string> ReadMarqueeLines(std::u16string_view text,std::uint32_t default_color) {
    if(text.size()>1048576) return std::unexpected("marquee text exceeds the widget capacity");
    if(const auto nul=text.find(u'\0');nul!=text.npos) text=text.substr(0,nul);
    std::vector<MarqueeLine> lines;
    for(std::size_t start=0;start<text.size();) {
        const auto newline=text.find(u'\n',start);
        const auto end=newline==text.npos ? text.size() : newline;
        MarqueeLine line{{},0,default_color};std::size_t visible=start;
        for(std::size_t i=start;i<end;++i) if(text[i]==u'<') {
            const auto close=text.find(u'>',i+1);if(close==text.npos || close>=end) continue;
            const auto tag=text.substr(i+1,close-i-1);bool recognized=false;
            if(marquee_detail::Prefix(tag,u"bold")) {line.font=1;recognized=true;}
            else if(marquee_detail::Prefix(tag,u"color=")) {
                auto params=tag.substr(6);std::array<unsigned,3> channels{};
                for(auto& channel:channels) {
                    const auto comma=params.find(u',');channel=marquee_detail::ColorByte(params.substr(0,comma));
                    if(comma==params.npos) break;
                    params.remove_prefix(comma+1);
                }
                line.color=0xff000000u|(channels[0]<<16)|(channels[1]<<8)|channels[2];recognized=true;
            }
            if(recognized) {visible=close+1;i=close;}
        }
        line.text=std::u16string(text.substr(visible,end-visible));lines.push_back(std::move(line));
        if(newline==text.npos) break;
        start=end+1;
    }
    return lines;
}

class MarqueeScrollModel final {
public:
    mvvm::Observable<float> position{0};
    bool Configure(float row_height,float page_height,float character_height,float rows_per_second,unsigned blank_pages) {
        if(!std::isfinite(row_height) || !std::isfinite(page_height) || !std::isfinite(character_height) ||
            !std::isfinite(rows_per_second) || row_height<0 || page_height<0 || character_height<=0 || rows_per_second<0)
            return false;
        const double extent=double(row_height)+double(page_height)*blank_pages;
        const double speed=double(character_height)*rows_per_second;
        if(extent>16777216 || speed>16777216) return false;
        m_extent=static_cast<float>(extent);m_speed=static_cast<float>(speed);position.Set(0);m_cycles=0;return true;
    }
    bool Advance(float seconds) {
        if(!std::isfinite(seconds) || seconds<0 || m_extent<=0) return false;
        const double next=double(position.Get())+double(seconds)*m_speed;
        // The original restarts at zero, discarding excess elapsed distance.
        if(next>=m_extent) {position.Set(0);++m_cycles;} else position.Set(static_cast<float>(next));
        return true;
    }
    float Extent() const noexcept {return m_extent;}
    std::uint64_t Cycles() const noexcept {return m_cycles;}
private:
    float m_extent{},m_speed{};
    std::uint64_t m_cycles{};
};
}
