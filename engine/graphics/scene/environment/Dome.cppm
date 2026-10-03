export module Graphics.Scene.Environment.Dome;
import std;
export import Graphics.Scene.Props.Renderer;
export namespace Graphics {
struct DomeBand {float polar_angle{};std::array<float,4> color{1,1,1,1};};
struct DomeGeometry {std::vector<PropVertex> vertices;std::vector<std::uint32_t> indices;};
// Camera-relative surface of revolution. Palettes, bands and dimensions are
// supplied by content; this mechanism knows no game's weather rules.
inline std::expected<DomeGeometry,std::string> Build_Dome(std::span<const DomeBand> bands,unsigned segments,float radius) {
    if(bands.size()<2 || bands.size()>128 || segments<3 || segments>4096 || !std::isfinite(radius) || radius<=0) return std::unexpected("invalid dome dimensions");
    float previous=-1;
    for(const auto& band:bands) {
        if(!std::isfinite(band.polar_angle) || band.polar_angle<0 || band.polar_angle>std::numbers::pi_v<float> || band.polar_angle<=previous ||
            !std::ranges::all_of(band.color,[](float value) {return std::isfinite(value);})) return std::unexpected("invalid dome band");
        previous=band.polar_angle;
    }
    DomeGeometry result;result.vertices.reserve(segments*bands.size());
    for(unsigned segment=0;segment<segments;++segment) {
        const auto angle=2*std::numbers::pi_v<float>*segment/segments;
        for(const auto& band:bands) {
            PropVertex vertex;const auto horizontal=radius*std::sin(band.polar_angle);
            vertex.position={horizontal*std::cos(angle),horizontal*std::sin(angle),radius*std::cos(band.polar_angle)};
            vertex.color=band.color;result.vertices.push_back(vertex);
        }
    }
    for(unsigned segment=0;segment<segments;++segment) for(unsigned row=0;row+1<bands.size();++row) {
        const auto first=std::uint32_t(segment*bands.size()+row),next=std::uint32_t(((segment+1)%segments)*bands.size()+row);
        result.indices.insert(result.indices.end(),{first,next,next+1,next+1,first+1,first});
    }
    return result;
}
class DomeRenderer {
public:
    explicit DomeRenderer(PropRenderer& renderer):m_renderer(renderer) {}
    ~DomeRenderer() {if(m_mesh.Is_Valid()) m_renderer.Destroy_Mesh(m_mesh);}
    DomeRenderer(const DomeRenderer&)=delete;DomeRenderer& operator=(const DomeRenderer&)=delete;
    bool Initialize(const DomeGeometry& geometry) {
        if(m_mesh.Is_Valid()) m_renderer.Destroy_Mesh(m_mesh);
        m_mesh=m_renderer.Create_Mesh(geometry.vertices,geometry.indices);return m_mesh.Is_Valid();
    }
    bool Draw(CommandList& commands,PropParameters parameters) {
        parameters.world={1,0,0,parameters.camera_position[0],0,1,0,parameters.camera_position[1],0,0,1,parameters.camera_position[2],0,0,0,1};
        parameters.textured=0;parameters.primary_gradient=1;parameters.scene_ambient={1,1,1,0};parameters.fog_state={};
        parameters.light_diffuse={};parameters.light_ambient={};parameters.light_specular={};
        PropStyle style;style.depth_test=false;style.depth_write=false;style.blend=RHIBlendMode::Disabled;
        const std::array<RHITextureHandle,PropTextureCount> textures{};
        return m_renderer.Draw(commands,m_mesh,style,parameters,textures);
    }
private:
    PropRenderer& m_renderer;PropMeshHandle m_mesh;
};
}
