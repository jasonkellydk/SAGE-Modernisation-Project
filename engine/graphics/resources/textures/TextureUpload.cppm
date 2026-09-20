module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
export module Graphics.Resources.Textures.Upload;
import Graphics.RHI;
import Graphics.Resources.Textures.Resource;

namespace Graphics
{
// Begin and Finish run on the device thread. Workers may fill disjoint mapped
// subresources between them; the caller must join workers before Finish.
// The device outlives this owner. No partially prepared image is published here.
export class TextureUpload final
{
public:
    TextureUpload() = default;
    TextureUpload(const TextureUpload&) = delete;
    TextureUpload& operator=(const TextureUpload&) = delete;
    ~TextureUpload() { Finish(); }

    bool Begin(Device& device, RHITextureHandle texture, std::uint32_t mip_count,
        std::uint32_t layer_count = 1)
    {
        if (Active() || mip_count==0 || layer_count==0
            || layer_count>std::numeric_limits<std::size_t>::max()/mip_count)
            return false;
        m_mappings.resize(std::size_t(mip_count)*layer_count);
        if (!device.Retain_Texture(texture)) {
            m_mappings.clear();
            return false;
        }
        m_device = &device;
        m_texture = texture;
        m_mip_count = mip_count;
        for (std::uint32_t layer=0;layer<layer_count;++layer) {
            for (std::uint32_t mip=0;mip<mip_count;++mip) {
                auto& mapping = m_mappings[m_mapped_count];
                if (!device.Map_Texture(texture,mip,layer,false,mapping)) {
                    Finish();
                    return false;
                }
                ++m_mapped_count;
                if (mapping.bytes.empty()) {
                    Finish();
                    return false;
                }
            }
        }
        return true;
    }

    // A fresh image supplies every texel. CPU staging avoids mapping the old
    // GPU contents (and a synchronous readback for every mip on DX12).
    bool Begin_Overwrite(TextureResource& resource)
    {
        return Prepare_Overwrite(resource.Description(),resource.Encoding()) && Attach_Overwrite(resource);
    }

    // CPU storage only: safe on a decoding worker, without creating a GPU image.
    bool Prepare_Overwrite(const RHITexture& description, Assets::PixelEncoding encoding)
    {
        if (Active()) return false;
        const bool compressed = Assets::Is_Block_Compressed(encoding);
        const unsigned unit = compressed ? (encoding == Assets::PixelEncoding::BC1 ? 8u : 16u)
            : Assets::Pixel_Size(encoding);
        if (!unit || !description.mip_count || !description.array_size) return false;
        const std::size_t count = std::size_t(description.mip_count) * description.array_size;
        m_staging.clear(); m_staging.resize(count);
        m_mappings.clear(); m_mappings.resize(count);
        for (unsigned layer = 0; layer < description.array_size; ++layer) {
            for (unsigned mip = 0; mip < description.mip_count; ++mip) {
                const unsigned width = std::max(1u, description.width >> mip);
                const unsigned height = std::max(1u, description.height >> mip);
                const unsigned depth = std::max(1u, description.depth >> mip);
                const std::size_t pitch = std::size_t(compressed ? (width + 3u) / 4u : width) * unit;
                const std::size_t rows = compressed ? (height + 3u) / 4u : height;
                if (pitch > std::numeric_limits<unsigned>::max() / rows ||
                    pitch * rows > std::numeric_limits<std::size_t>::max() / depth) {
                    m_staging.clear(); m_mappings.clear(); return false;
                }
                const auto index = std::size_t(layer) * description.mip_count + mip;
                m_staging[index].resize(pitch * rows * depth);
                m_mappings[index] = {m_staging[index], static_cast<unsigned>(pitch), static_cast<unsigned>(pitch * rows)};
            }
        }
        m_mip_count = description.mip_count; m_mapped_count = count;
        m_overwrite = true;
        return true;
    }

    // Attach the completed CPU image on the device thread immediately before upload.
    bool Attach_Overwrite(TextureResource& resource)
    {
        if (Active() || !m_overwrite || m_mappings.empty()
            || resource.Description().mip_count != m_mip_count
            || resource.Description().array_size * m_mip_count != m_mapped_count) return false;
        if (!resource.Owner().Retain_Texture(resource.Handle())) {
            m_staging.clear(); m_mappings.clear(); return false;
        }
        m_device = &resource.Owner(); m_texture = resource.Handle();
        return true;
    }

    bool Finish() noexcept
    {
        if (!Active()) return true;
        bool complete = true;
        for (std::size_t index=0;index<m_mapped_count;++index) {
            const auto mip = static_cast<std::uint32_t>(index%m_mip_count);
            const auto layer = static_cast<std::uint32_t>(index/m_mip_count);
            const auto& mapping = m_mappings[index];
            const bool uploaded = m_overwrite
                ? m_device->Update_Texture(m_texture, {mapping.bytes, mapping.row_pitch, mapping.slice_pitch, mip, layer})
                : m_device->Unmap_Texture(m_texture, mip, layer);
            if (!uploaded) complete = false;
        }
        m_device->Destroy_Texture(m_texture);
        m_device = nullptr;
        m_texture = {};
        m_mappings.clear();
        m_mapped_count = 0;
        m_mip_count = 0;
        m_overwrite = false; m_staging.clear();
        return complete;
    }

    RHITextureMapping Mapping(std::uint32_t mip, std::uint32_t layer = 0) const noexcept
    {
        if (mip>=m_mip_count || layer>=m_mapped_count/m_mip_count) return {};
        return m_mappings[std::size_t(layer)*m_mip_count+mip];
    }

    bool Active() const noexcept { return m_device!=nullptr; }

private:
    Device* m_device = nullptr;
    RHITextureHandle m_texture{};
    std::vector<RHITextureMapping> m_mappings;
    std::vector<std::vector<std::byte>> m_staging;
    bool m_overwrite = false;
    std::size_t m_mapped_count = 0;
    std::uint32_t m_mip_count = 0;
};
}
