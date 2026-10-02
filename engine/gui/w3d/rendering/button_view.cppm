export module engine.gui.w3d.button_view;
import std;
import engine.gui.images;
import engine.gui.w3d.text_view;
import engine.gui.w3d.value_view;
import engine.gui.text.font_face;
import Graphics.Renderer2D;

export namespace engine::gui::w3d {
struct FrameImageSpec {
    Graphics::Rect2D source_pixels;
    Graphics::Point2D texture_size,corner_size,tile_size;
};
struct ButtonSkin {
    FrameImageSpec frame;
    Graphics::Rect2D pressed_uv;
    Graphics::Point2D pressed_inset,pressed_far_edge;
};
// Tiled atlas edges retain their physical pixel size. Content selects the atlas
// regions and dimensions; this mechanism is independent of a game's skin.
std::optional<std::vector<images::ImagePatch>> Layout_Component_Frame(Graphics::Rect2D bounds,const FrameImageSpec& spec) {
    const auto source=spec.source_pixels;const auto size=spec.texture_size;
    const auto corner=spec.corner_size,tile=spec.tile_size;
    if(!images::FiniteRectangle(bounds) || !images::FiniteRectangle(source) || bounds.right<=bounds.left || bounds.bottom<=bounds.top ||
        !std::isfinite(size.x) || !std::isfinite(size.y) || size.x<=0 || size.y<=0 ||
        !std::isfinite(corner.x) || !std::isfinite(corner.y) || corner.x<=0 || corner.y<=0 ||
        !std::isfinite(tile.x) || !std::isfinite(tile.y) || tile.x<=0 || tile.y<=0 ||
        source.left<0 || source.top<0 || source.right>size.x || source.bottom>size.y ||
        source.right-source.left<2*corner.x || source.bottom-source.top<2*corner.y ||
        tile.x>source.right-source.left || tile.y>source.bottom-source.top) return {};
    std::vector<images::ImagePatch> patches;
    const auto uv=[&](Graphics::Rect2D pixels) {return Graphics::Rect2D{pixels.left/size.x,pixels.top/size.y,pixels.right/size.x,pixels.bottom/size.y};};
    const auto add=[&](Graphics::Rect2D screen,Graphics::Rect2D pixels) {patches.push_back({screen,uv(pixels)});};
    add({bounds.left,bounds.top,bounds.left+corner.x,bounds.top+corner.y},
        {source.left,source.top,source.left+corner.x,source.top+corner.y});
    add({bounds.right-corner.x,bounds.top,bounds.right,bounds.top+corner.y},
        {source.right-corner.x,source.top,source.right,source.top+corner.y});
    add({bounds.left,bounds.bottom-corner.y,bounds.left+corner.x,bounds.bottom},
        {source.left,source.bottom-corner.y,source.left+corner.x,source.bottom});
    add({bounds.right-corner.x,bounds.bottom-corner.y,bounds.right,bounds.bottom},
        {source.right-corner.x,source.bottom-corner.y,source.right,source.bottom});
    const float center_x=(source.left+source.right-tile.x)*.5f,center_y=(source.top+source.bottom-tile.y)*.5f;
    if(bounds.right-bounds.left>2*corner.x && !images::Tile_Image(
        {bounds.left+corner.x,bounds.top,bounds.right-corner.x,bounds.top+corner.y},
        uv({center_x,source.top,center_x+tile.x,source.top+corner.y}),{tile.x,corner.y},[&](auto patch) {
            patches.push_back(patch);patch.screen.top=bounds.bottom-corner.y;patch.screen.bottom=bounds.bottom;
            patch.uv.top=(source.bottom-corner.y)/size.y;patch.uv.bottom=source.bottom/size.y;
            patches.push_back(patch);return true;
        })) return {};
    if(bounds.bottom-bounds.top>2*corner.y && !images::Tile_Image(
        {bounds.left,bounds.top+corner.y,bounds.left+corner.x,bounds.bottom-corner.y},
        uv({source.left,center_y,source.left+corner.x,center_y+tile.y}),{corner.x,tile.y},[&](auto patch) {
            patches.push_back(patch);patch.screen.left=bounds.right-corner.x;patch.screen.right=bounds.right;
            patch.uv.left=(source.right-corner.x)/size.x;patch.uv.right=source.right/size.x;
            patches.push_back(patch);return true;
        })) return {};
    return patches;
}
bool Draw_Component_Button(const text::FontFace& font,Graphics::Renderer2D& renderer,std::u16string_view label,
    Graphics::Rect2D bounds,Graphics::Renderer2DTexture texture,const ButtonSkin& skin,const ValuePalette& palette,
    bool focused,bool pressed,Graphics::Color2D glow,std::span<const Graphics::Point2D> glow_offsets) {
    const auto patches=Layout_Component_Frame(bounds,skin.frame);if(!patches || !texture.index.Is_Valid()) return false;
    if(pressed && !renderer.Add_Quad(Graphics::Rect2D{bounds.left+skin.pressed_inset.x,bounds.top+skin.pressed_inset.y,
        bounds.right-skin.pressed_inset.x+skin.pressed_far_edge.x,bounds.bottom-skin.pressed_inset.y+skin.pressed_far_edge.y},
        skin.pressed_uv,texture,Graphics::Color2D{1,1,1,1},Graphics::Renderer2DBlendMode::Additive)) return false;
    for(const auto& patch:*patches) if(!renderer.Add_Quad(patch.screen,patch.uv,texture,Graphics::Color2D{1,1,1,1},
        Graphics::Renderer2DBlendMode::Additive)) return false;
    if(focused) {
        const auto size=Measure_Text(font,label);if(!size) return false;
        const float x=std::trunc(bounds.left+(bounds.right-bounds.left-(*size)[0])*.5f);
        const float y=std::trunc(bounds.top+(bounds.bottom-bounds.top-(*size)[1])*.5f);
        if(!Draw_Text_Offsets(font,renderer,label,x,y,glow,glow_offsets,Graphics::Renderer2DBlendMode::Additive)) return false;
    }
    text::TextStyle style;style.color=palette.text;style.drop_color=palette.shadow;style.x_drop=-1;style.y_drop=1;
    TextBoxOptions options;options.vertical_center=true;options.alignment=TextAlignment::Center;
    return Draw_Text_Box(font,renderer,label,bounds,style,options);
}
}
