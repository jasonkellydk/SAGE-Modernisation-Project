module;
#include <cstdint>
#include <optional>
export module Graphics.Resources.Textures.CPUImage;
export import Assets.Images.Buffer;
export import Graphics.Resources.Textures.Resource;

namespace Graphics {
// Authoritative CPU pixels for incrementally edited textures. Edits never read
// the GPU; Upload publishes one complete image when its consumer needs it.
export class CPUTextureImage final {
public:
    bool Initialize(unsigned width,unsigned height,Assets::PixelEncoding encoding) {
        Reset();
        m_image=Assets::ImageBuffer::Create(width,height,encoding);
        return m_image.has_value();
    }
    void Reset() noexcept {
        m_image.reset(); m_dirty=true; m_target={}; m_epoch=0;
    }
    bool Is_Valid() const noexcept { return m_image.has_value(); }
    Assets::ImageBuffer& Edit() noexcept { m_dirty=true; return *m_image; }
    const Assets::ImageBuffer& Image() const noexcept { return *m_image; }
    // Epoch changes when the destination device/resources are recreated, even
    // if its allocator reuses the same handle. This object retains no GPU owner.
    bool Upload(TextureResource& target,std::uint64_t epoch) noexcept {
        if (!m_image || target.Description().width!=m_image->Width()
            || target.Description().height!=m_image->Height()
            || target.Encoding()!=m_image->Encoding()) return false;
        if (!m_dirty && m_target==target.Handle() && m_epoch==epoch) return true;
        if (!target.Owner().Update_Texture(target.Handle(),{m_image->Bytes(),m_image->Row_Pitch()})) return false;
        m_dirty=false; m_target=target.Handle(); m_epoch=epoch;
        return true;
    }
private:
    std::optional<Assets::ImageBuffer> m_image;
    bool m_dirty=true;
    RHITextureHandle m_target{};
    std::uint64_t m_epoch=0;
};
}
