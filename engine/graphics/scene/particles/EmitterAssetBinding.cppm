export module Graphics.Scene.Particles.EmitterAssetBinding;
import std;
import Assets.Particles;
export import Graphics.Resources.MipChain;
export namespace Graphics {
struct PreparedEmitterAsset {Assets::EmitterAssetDesc description;std::shared_ptr<const PreparedMipChain> texture;};
class EmitterAssetBinding {
public:
    EmitterAssetBinding()=default;
    ~EmitterAssetBinding() {if(m_device && m_texture.Is_Valid()) m_device->Destroy_Texture(m_texture);}
    EmitterAssetBinding(const EmitterAssetBinding&)=delete;EmitterAssetBinding& operator=(const EmitterAssetBinding&)=delete;
    bool Initialize(Device& device,const PreparedEmitterAsset& source) {
        if(m_device) return false;
        if(source.texture) {m_texture=Upload_Mip_Chain(device,*source.texture);if(!m_texture.Is_Valid()) return false;}
        m_device=&device;return true;
    }
    RHITextureHandle Texture() const noexcept {return m_texture;}
private:
    Device* m_device{};RHITextureHandle m_texture;
};
}
