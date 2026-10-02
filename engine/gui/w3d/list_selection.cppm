export module engine.gui.w3d.list_selection;
import std;
import engine.gui.mvvm.observable;
import engine.gui.w3d.scrollbar;

export namespace engine::gui::w3d {
enum class ListAction { Previous,Next,First,Last,PagePrevious,PageNext,Activate };
struct ListKeyResult {bool handled{},activate{};};
// Single selection and variable-height paging from W3D ListCtrl. Content,
// columns, row measurements and whether deselection is allowed belong to callers.
class ListSelectionModel final {
public:
    mvvm::Observable<int> selected{-1},scroll{0};
    ScrollBarModel scrollbar{[this](int value){scroll.Set(value);},[this](int direction){ScrollPage(direction);}};
    ListSelectionModel() {
        scrollbar.PageSize(0);
        scroll.Subscribe([this](int value){scrollbar.Set(value,false);});
    }
    bool selection_allowed{true},no_selection_allowed{};
    bool Configure(std::span<const float> heights,float page_height,bool reset=true) {
        if(heights.size()>65536 || !std::isfinite(page_height) || page_height<=0 || page_height>16777216 ||
            !std::ranges::all_of(heights,[](float h){return std::isfinite(h) && h>0 && h<=16777216;})) return false;
        m_heights.assign(heights.begin(),heights.end());m_page=page_height;m_last_top=0;
        float remaining=page_height;
        for(int i=Count()-1;i>=0;--i) if((remaining-=m_heights[i])<=0) {m_last_top=i+1;break;}
        scrollbar.Range(0,m_last_top);
        if(reset) {selected.Set(-1);scroll.Set(0);}
        else {
            // ListCtrl explicitly notifies a clamp after visibility/range
            // replacement. Parent observes the old top before advise applies.
            if(scrollbar.position.Get()>m_last_top) scrollbar.Set(m_last_top);
            selected.Set(std::min(selected.Get(),Count()-1));scroll.Set(std::clamp(scroll.Get(),0,m_last_top));Reveal();
        }
        scrollbar.Set(scroll.Get(),false);
        return true;
    }
    int Count() const noexcept {return static_cast<int>(m_heights.size());}
    float PageHeight() const noexcept {return m_page;}
    std::span<const float> RowHeights() const noexcept {return m_heights;}
    int LastPageTop() const noexcept {return m_last_top;}
    bool Select(std::int64_t index) {
        if(!selection_allowed) return false;
        const int next=(index==-1 && no_selection_allowed) || !Count() ? -1 : static_cast<int>(std::clamp<std::int64_t>(index,0,Count()-1));
        const bool changed=next!=selected.Get();selected.Set(next);Reveal();return changed;
    }
    void ClearSelection() {selected.Set(-1);}
    int EndOfPage() const {
        float height{};
        for(int i=scroll.Get();i<Count();++i) if((height+=m_heights[i])>=m_page) return i-1;
        // Preserve ListCtrl's count sentinel; Select applies its own clamp.
        return Count();
    }
    ListKeyResult Key(ListAction action) {
        switch(action) {
        case ListAction::Previous:Select(std::int64_t(selected.Get())-1);break;
        case ListAction::Next:Select(std::int64_t(selected.Get())+1);break;
        case ListAction::First:Select(0);break;
        case ListAction::Last:Select(Count()-1);break;
        case ListAction::PagePrevious:
            if(selected.Get()==scroll.Get()) ScrollPage(-1);else Select(scroll.Get());break;
        case ListAction::PageNext:
            if(selected.Get()==EndOfPage()) ScrollPage(1);else Select(EndOfPage());break;
        case ListAction::Activate:return {true,true};
        }
        return {true,false};
    }
    void Wheel(int direction) {
        if(direction<0) {if(scroll.Get()>0) scroll.Set(scroll.Get()-1);}
        else if(scroll.Get()<m_last_top) scroll.Set(scroll.Get()+1);
    }
    std::optional<int> HitRow(float y,float text_top=0) const {
        if(!std::isfinite(y) || !std::isfinite(text_top) || y<text_top || y>text_top+m_page) return {};
        float top=std::trunc(text_top);
        for(int i=scroll.Get();i<Count();++i) {
            if(y>=top && y<=top+m_heights[i]) return i;
            top=std::trunc(top+m_heights[i]);
            if(y>=text_top+m_page) break;
        }
        return {};
    }
private:
    int TopOfPage(int bottom) const {
        float remaining=m_page;
        for(int i=bottom;i>=0;--i) if((remaining-=m_heights[i])<0) return i+1;
        return 0;
    }
    void Reveal() {
        if(selected.Get()<0) return;
        if(selected.Get()<scroll.Get()) scroll.Set(selected.Get());
        else {
            float bottom{};for(int i=scroll.Get();i<=selected.Get();++i) bottom+=m_heights[i];
            if(bottom>=m_page) scroll.Set(TopOfPage(selected.Get()));
        }
    }
    void ScrollPage(int direction) {
        float remaining=m_page;int i=scroll.Get();
        for(;i>=0 && i<Count();i+=direction) if((remaining-=m_heights[i])<0) {
            scroll.Set(std::clamp(i-direction,0,m_last_top));return;
        }
        if(i<0) scroll.Set(0);
    }
    std::vector<float> m_heights;
    float m_page{};
    int m_last_top{};
};
}
