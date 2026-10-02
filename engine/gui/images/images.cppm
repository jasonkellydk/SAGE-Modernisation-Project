export module engine.gui.images;
import std;
import Graphics.Renderer2D;
import Assets.Cache;
import Assets.Handles;
import Assets.Textures;

export namespace engine::gui::images {
struct ImagePatch {Graphics::Rect2D screen,uv;};
bool FiniteRectangle(Graphics::Rect2D rect) {
    constexpr float limit=16777216;
    return std::isfinite(rect.left) && std::isfinite(rect.top) && std::isfinite(rect.right) && std::isfinite(rect.bottom) &&
        std::abs(rect.left)<limit && std::abs(rect.top)<limit && std::abs(rect.right)<limit && std::abs(rect.bottom)<limit;
}
// Shared horizontal/vertical tiling, including partial final-tile UVs. Bound
// work before iteration so malformed authored dimensions cannot stall a frame.
template<class Append>
bool Tile_Image(Graphics::Rect2D screen,Graphics::Rect2D uv,Graphics::Point2D tile,Append&& append) {
    if(!FiniteRectangle(screen) || !FiniteRectangle(uv) || !std::isfinite(tile.x) || !std::isfinite(tile.y) ||
        tile.x<=0 || tile.y<=0 || screen.right<screen.left || screen.bottom<screen.top) return false;
    if(screen.right==screen.left || screen.bottom==screen.top) return true;
    const double width=double(screen.right)-screen.left,height=double(screen.bottom)-screen.top;
    const double columns=std::ceil(width/tile.x),rows=std::ceil(height/tile.y);
    if(columns*rows>65536) return false;
    for(std::size_t y=0;y<static_cast<std::size_t>(rows);++y) for(std::size_t x=0;x<static_cast<std::size_t>(columns);++x) {
        const float left=static_cast<float>(screen.left+x*double(tile.x)),top=static_cast<float>(screen.top+y*double(tile.y));
        const float right=std::min(screen.right,static_cast<float>(screen.left+(x+1)*double(tile.x)));
        const float bottom=std::min(screen.bottom,static_cast<float>(screen.top+(y+1)*double(tile.y)));
        if(right<=left || bottom<=top) return false;
        const Graphics::Rect2D piece_uv{uv.left,uv.top,uv.left+(uv.right-uv.left)*(right-left)/tile.x,
            uv.top+(uv.bottom-uv.top)*(bottom-top)/tile.y};
        if(!append(ImagePatch{{left,top,right,bottom},piece_uv})) return false;
    }
    return true;
}
// This is the existing WND asset binding. Both GUI formats now use the same
// resource namespace, lifetime, decoded pixels and renderer-owned GPU texture.
Graphics::Renderer2DTexture Resolve_Texture(Assets::AssetCache& cache,Assets::TextureAssetHandle handle,
    Graphics::Renderer2D& renderer) {
    if(!handle.Is_Valid()) return {};
    cache.Wait(handle);const auto* asset=cache.Try_Get_Texture(handle);
    if(!asset || !asset->Has_Pixels()) return {};
    const Graphics::TextureHandle owner(
        static_cast<Graphics::TextureHandle::Index>(0x40000000u | handle.Get_Index()),
        static_cast<Graphics::TextureHandle::Generation>(handle.Get_Generation()));
    return renderer.Register_Texture({owner,asset->Width(),asset->Height(),asset->Row_Pitch(),1,asset->Pixels()});
}
}
