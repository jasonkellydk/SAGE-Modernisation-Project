export module engine.gui.w3d.scrollbar_view;
import std;
import engine.gui.w3d.scrollbar;
import engine.gui.w3d.dialog_input;
import engine.gui.images;
import Graphics.Renderer2D;

export namespace engine::gui::w3d {
// Atlas regions, dimensions and scaling are supplied by content. None of the
// original game's skin coordinates or screen-resolution constants live here.
struct ScrollSkin {
    Graphics::Rect2D previous_up,previous_down,next_up,next_down,thumb_up,thumb_down;
    Graphics::Point2D uv_divisor,scale;
    float width{},button_offset{};
    bool centered_thumb{},flip_thumb_below_midpoint{};
};
struct ScrollLayout {HitRect bounds,client,outline;ScrollGeometry geometry;};
std::optional<ScrollLayout> Layout_Scrollbar(const ScrollBarModel& model,HitRect requested,const ScrollSkin& skin) {
    const auto rect=[](HitRect value){return Graphics::Rect2D{value.left,value.top,value.right,value.bottom};};
    if(!images::FiniteRectangle(rect(requested)) || requested.bottom<=requested.top ||
        !std::isfinite(skin.width) || skin.width<=0 || skin.width>=16777216 ||
        !std::isfinite(skin.button_offset) || std::abs(skin.button_offset)>=16777216 ||
        !std::isfinite(skin.scale.x) || !std::isfinite(skin.scale.y) || skin.scale.x<=0 || skin.scale.y<=0) return {};
    for(const auto region:{skin.previous_up,skin.previous_down,skin.next_up,skin.next_down,skin.thumb_up,skin.thumb_down})
        if(!images::FiniteRectangle(region) || region.right<=region.left || region.bottom<=region.top) return {};
    const float tw=(skin.thumb_up.right-skin.thumb_up.left)*skin.scale.x,th=(skin.thumb_up.bottom-skin.thumb_up.top)*skin.scale.y;
    const float bw=(skin.previous_up.right-skin.previous_up.left)*skin.scale.x,bh=(skin.previous_up.bottom-skin.previous_up.top)*skin.scale.y;
    if(!std::isfinite(tw+th+bw+bh) || std::min({tw,th,bw,bh})<1 || std::max({tw,th,bw,bh})>=16777216) return {};
    const int thumb_width=static_cast<int>(tw),thumb_height=static_cast<int>(th);
    const int button_width=static_cast<int>(bw),button_height=static_cast<int>(bh);
    if(requested.right>=0) requested.left=std::trunc(requested.right-skin.width);
    else requested.right=requested.left+skin.width;
    ScrollLayout layout;layout.client=requested;layout.bounds=requested;
    auto& g=layout.geometry;
    g.previous.left=std::trunc(requested.left+(requested.right-requested.left)*.5f-button_width/2);
    g.previous.top=std::trunc(requested.top-skin.button_offset*skin.scale.y);
    g.previous.right=g.previous.left+button_width;g.previous.bottom=g.previous.top+button_height;
    g.next.left=g.previous.left;g.next.right=g.previous.right;
    g.next.top=std::trunc(requested.bottom-button_height+skin.button_offset*skin.scale.y);g.next.bottom=g.next.top+button_height;
    layout.bounds.top=g.previous.top;layout.bounds.bottom=g.next.bottom;
    g.track=requested;g.track.top=g.previous.bottom;
    // Deliberate finite no-travel hardening for a gutter shorter than its skin.
    g.track.bottom=std::max(g.track.top,g.next.top-thumb_height);
    const auto span=std::int64_t(model.Maximum())-model.Minimum();
    const float ratio=span ? float(std::int64_t(model.position.Get())-model.Minimum())/float(span) : 0;
    if(skin.centered_thumb) {
        g.thumb.left=std::trunc(requested.left+(requested.right-requested.left)*.5f-thumb_width/2);
        g.thumb.right=g.thumb.left+thumb_width;
    } else {g.thumb.right=requested.right-1;g.thumb.left=g.thumb.right-thumb_width;}
    g.thumb.top=std::trunc(g.previous.bottom+(g.track.bottom-g.track.top)*ratio);g.thumb.bottom=g.thumb.top+thumb_height;
    layout.outline=layout.bounds;layout.outline.top=g.previous.bottom-(g.previous.bottom-g.previous.top)*.5f;
    layout.outline.bottom=g.next.top+(g.next.bottom-g.next.top)*.5f;
    for(const auto value:{layout.bounds,layout.client,layout.outline,g.previous,g.next,g.thumb,g.track})
        if(!images::FiniteRectangle(rect(value))) return {};
    return layout;
}
std::optional<std::array<images::ImagePatch,3>> Scrollbar_Patches(const ScrollBarModel& model,const ScrollLayout& layout,const ScrollSkin& skin) {
    if(!std::isfinite(skin.uv_divisor.x) || !std::isfinite(skin.uv_divisor.y) || skin.uv_divisor.x<=0 || skin.uv_divisor.y<=0) return {};
    auto thumb=model.Interaction()==ScrollInteraction::Drag ? skin.thumb_down : skin.thumb_up;
    if(skin.flip_thumb_below_midpoint && layout.geometry.thumb.top>layout.client.top+(layout.client.bottom-layout.client.top)*.5f)
        std::swap(thumb.top,thumb.bottom);
    const auto patch=[&](HitRect screen,Graphics::Rect2D pixels) {
        return images::ImagePatch{{screen.left,screen.top,screen.right,screen.bottom},
            {pixels.left/skin.uv_divisor.x,pixels.top/skin.uv_divisor.y,pixels.right/skin.uv_divisor.x,pixels.bottom/skin.uv_divisor.y}};
    };
    return std::array{patch(layout.geometry.thumb,thumb),
        patch(layout.geometry.previous,model.Interaction()==ScrollInteraction::Previous ? skin.previous_down : skin.previous_up),
        patch(layout.geometry.next,model.Interaction()==ScrollInteraction::Next ? skin.next_down : skin.next_up)};
}
// Drawing never mutates the model. The visible control's composition performs
// one AdvanceRender call; auxiliary pixel probes cannot add hold repetitions.
bool Draw_Scrollbar(Graphics::Renderer2D& renderer,const ScrollBarModel& model,const ScrollLayout& layout,
    const ScrollSkin& skin,Graphics::Renderer2DTexture texture,Graphics::Color2D line,float gradient_edge_alpha) {
    const auto patches=Scrollbar_Patches(model,layout,skin);
    if(!patches || !texture.index.Is_Valid() || !std::isfinite(gradient_edge_alpha) || gradient_edge_alpha<0 || gradient_edge_alpha>1) return false;
    const auto outline=layout.outline;
    if(!renderer.Add_Outline({outline.left,outline.top,outline.right,outline.bottom},1,line)) return false;
    auto inner=line,outer=line;inner.alpha=1;outer.alpha=gradient_edge_alpha;
    const auto gradient=[&](float top,float bottom,Graphics::Color2D from,Graphics::Color2D to) {
        if(bottom<=top) return true;
        const auto client=layout.client;const float x=(client.left+client.right)*.5f;
        return renderer.Add_Gradient_Line({x,top},{x,bottom},client.right-client.left,{from,from,to,to});
    };
    if(!gradient(outline.top,layout.geometry.thumb.top,inner,outer) ||
        !gradient(layout.geometry.thumb.bottom,outline.bottom,outer,inner)) return false;
    for(const auto& patch:*patches) if(!renderer.Add_Quad(patch.screen,patch.uv,texture,Graphics::Color2D{1,1,1,1})) return false;
    return true;
}
}
