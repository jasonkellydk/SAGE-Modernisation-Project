export module Graphics.Scene.Props.AssetBinding;
import Graphics.Resources.MipChain;
import std;
export import Graphics.Scene.Props.Submission;
export import Graphics.Scene.Models.AssetPose;
export import Graphics.Scene.Props.AssetPreparation;
import Graphics.Scene.Models.PartBones;
import Assets.Cache;
import Assets.Models;
import Assets.Materials;
import Assets.Textures;
import Assets.Handles;
import Graphics.Materials.TextureMapping;

namespace Graphics {
export struct PropTextureMappingContext final {
    std::uint32_t milliseconds=0;
    std::array<float,16> projection{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
};
// Own GPU versions for a resolved asset. Draws use immutable prepared state;
// source IO and asset-name lookup never occur during submission.
export enum class PropAssetLoadState { Pending, Ready, Failed };
export class PropAssetBinding final {
public:
    PropAssetBinding()=default;
    PropAssetBinding(const PropAssetBinding&)=delete;
    PropAssetBinding& operator=(const PropAssetBinding&)=delete;
    ~PropAssetBinding() { Clear(); }
    void Clear() noexcept {
        Cancel_Load();
        if(m_renderer) for(const auto& part:m_parts) m_renderer->Destroy_Mesh(part.mesh);
        if(m_device) for(const auto& texture:m_textures) m_device->Destroy_Texture(texture.second);
        m_parts.clear(); m_textures.clear(); m_device=nullptr; m_renderer=nullptr; m_rest_pose={};
    }
    // `base_override`: a texture drawn on every part in place of its own base map (the original's tree buffer draws each
    // tree type with its TextureName); none: as authored.
    // `base_replacements`: base maps swapped by name for this binding (the original's replacePrototypeTexture: each
    // pair's first, wherever a part uses it, drawn as its second).
    bool Load(Device& device,PropRenderer& renderer,Assets::AssetCache& assets,
        Assets::ModelAssetHandle model_handle,std::string& error,Assets::TextureAssetHandle base_override={},
        std::span<const std::pair<Assets::TextureAssetHandle,Assets::TextureAssetHandle>> base_replacements={},
        unsigned mip_reduction=0,unsigned minimum_texture_dimension=1) {
        const auto* model=assets.Try_Get_Model(model_handle);
        if(!model) { error="model asset is not ready"; return false; }
        std::vector<PropAssetPart> geometry;
        if(!Build_Prop_Asset_Geometry(*model,geometry,error)) return false;
        PreparedPropAsset prepared{model_handle,std::move(geometry),{}};
        if(!Begin_Load(device,renderer,assets,std::move(prepared),error,base_override,base_replacements,mip_reduction,minimum_texture_dimension)) return false;
        return Advance_Load(assets,(std::numeric_limits<std::size_t>::max)(),std::chrono::steady_clock::time_point::max(),error)==PropAssetLoadState::Ready;
    }
    // CPU geometry/mip preparation happens before Begin_Load. GPU work is
    // transactional and advances between frames; the old binding remains valid.
    bool Begin_Load(Device& device,PropRenderer& renderer,Assets::AssetCache& assets,PreparedPropAsset prepared,
        std::string& error,Assets::TextureAssetHandle base_override={},
        std::span<const std::pair<Assets::TextureAssetHandle,Assets::TextureAssetHandle>> base_replacements={},
        unsigned mip_reduction=0,unsigned minimum_texture_dimension=1) {
        Cancel_Load();
        const auto* model=assets.Try_Get_Model(prepared.model);
        if(!model || prepared.geometry.empty()) {error="prepared model is not ready";return false;}
        auto next=std::make_unique<PropAssetBinding>();
        next->m_device=&device;next->m_renderer=&renderer;
        next->m_mip_reduction=mip_reduction;next->m_minimum_texture_dimension=minimum_texture_dimension;
        next->m_prepared_textures=prepared.textures;
        if(!model->Rig().skeleton_name.empty() && !next->m_rest_pose.Initialize(model->Rig(),error)) return false;
        m_loading=std::move(prepared);m_pending=std::move(next);
        m_base_override=base_override;m_base_replacements.assign(base_replacements.begin(),base_replacements.end());
        m_loading_cursor=0;error.clear();return true;
    }
    PropAssetLoadState Advance_Load(Assets::AssetCache& assets,std::size_t maximum_parts,
        std::chrono::steady_clock::time_point deadline,std::string& error) {
        if(!m_pending) {error="no model upload in progress";return PropAssetLoadState::Failed;}
        const auto* model=assets.Try_Get_Model(m_loading.model);
        if(!model) {error="prepared model disappeared";Cancel_Load();return PropAssetLoadState::Failed;}
        std::size_t processed=0;
        while(m_loading_cursor<m_loading.geometry.size() && processed<maximum_parts && std::chrono::steady_clock::now()<deadline) {
            if(!m_pending->Append_Part(m_loading.geometry[m_loading_cursor],*model,assets,error,m_base_override,m_base_replacements)) {
                Cancel_Load();return PropAssetLoadState::Failed;
            }
            // Release CPU batches as their GPU copies are published.
            m_loading.geometry[m_loading_cursor]={};++m_loading_cursor;++processed;
        }
        if(m_loading_cursor<m_loading.geometry.size()) return PropAssetLoadState::Pending;
        auto next=std::move(m_pending);
        std::swap(m_device,next->m_device); std::swap(m_renderer,next->m_renderer);
        m_parts.swap(next->m_parts); m_textures.swap(next->m_textures);
        std::swap(m_rest_pose,next->m_rest_pose);
        std::swap(m_mip_reduction,next->m_mip_reduction);std::swap(m_minimum_texture_dimension,next->m_minimum_texture_dimension);
        m_loading={};m_base_replacements.clear();error.clear();return PropAssetLoadState::Ready;
    }
    void Cancel_Load() noexcept {m_pending.reset();m_loading={};m_base_replacements.clear();m_loading_cursor=0;}
    std::size_t Uploaded_Parts() const noexcept {return m_pending ? m_loading_cursor : m_parts.size();}
    std::size_t Part_Count() const noexcept { return m_parts.size(); }
    // Every part's textures sampled clamped at their edges (a sky box's faces meet without seams).
    void Clamp_Texture_Addressing() noexcept {
        for(auto& part:m_parts)
            for(auto& sampler:part.style.samplers) sampler.address.fill(RHISamplerAddress::Clamp);
    }
    std::size_t Texture_Count() const noexcept { return m_textures.size(); }
    std::string_view Part_Name(std::size_t part) const noexcept { return part<m_parts.size() ? m_parts[part].name : std::string_view{}; }
    std::int32_t Part_Sort_Level(std::size_t part) const noexcept { return part<m_parts.size() ? m_parts[part].sort_level : 0; }
    // Ordinary blended geometry bypasses authored static bins and enters the
    // shared triangle sorter. An alpha-tested surface remains an opaque draw.
    bool Part_Requires_Transparency_Sorting(std::size_t index) const noexcept {
        if(index>=m_parts.size()) return false;
        const auto& part=m_parts[index];
        return part.sort_level==0 && part.alpha_cutoff==0 &&
            (part.style.destination_blend!=RHIBlendFactor::Zero || part.style.source_blend!=RHIBlendFactor::One);
    }
    // The bone a part hangs on (Invalid_Bone_Index when it has none).
    std::uint32_t Part_Bone(std::size_t part) const noexcept { return part<m_parts.size() ? m_parts[part].bone : Invalid_Bone_Index; }
    // `shroud`: the viewer's shroud image in the shroud slot (parameters.shroud on: the part multiplied by it).
    // `stencil`: written (or tested) as it says while the part draws (none: as authored, no stencil).
    bool Draw_Part(CommandList& commands,std::size_t index,PropParameters parameters,
        const ModelAssetPose* pose=nullptr,std::uint32_t lod=0,RHITextureHandle shroud={},
        const RHIStencilDescription* stencil=nullptr,const PropTextureMappingContext* mapping=nullptr) const {
        if(!m_renderer || index>=m_parts.size()) return false;
        const auto& part=m_parts[index];
        if(part.lod!=lod) return true;
        bool visible=true;
        if(!Prepare_Pose(part,pose,parameters,visible)) return false;
        if(!visible) return true;
        Prepare(part,parameters,mapping);
        MeshVersion mesh{m_renderer,part.mesh,false};
        if(!Mesh_For_Pose(part,pose,mesh)) return false;
        if(stencil) {
            PropStyle style=part.style;
            style.stencil=*stencil;
            return m_renderer->Draw(commands,mesh.handle,style,parameters,Textures(part,shroud));
        }
        return m_renderer->Draw(commands,mesh.handle,part.style,parameters,Textures(part,shroud));
    }
    // A part seen through at `opacity` (the original's opacity override, MeshClass alpha override): as Draw_Part, but
    // an opaque part blends source alpha over what is behind it and drops texels under 96 x opacity (alpha test).
    bool Draw_Part_Translucent(CommandList& commands,std::size_t index,PropParameters parameters,float opacity,
        const ModelAssetPose* pose=nullptr,std::uint32_t lod=0,RHITextureHandle shroud={},const RHIStencilDescription* stencil=nullptr) const {
        if(!m_renderer || index>=m_parts.size()) return false;
        const auto& part=m_parts[index];
        if(part.lod!=lod) return true;
        bool visible=true;
        if(!Prepare_Pose(part,pose,parameters,visible)) return false;
        if(!visible) return true;
        Prepare(part,parameters);
        PropStyle style=part.style;
        if(style.destination_blend==RHIBlendFactor::Zero){
            style.source_blend=RHIBlendFactor::SourceAlpha;
            style.destination_blend=RHIBlendFactor::InverseSourceAlpha;
        }
        parameters.alpha_cutoff=std::max(parameters.alpha_cutoff,static_cast<float>(static_cast<unsigned>(96*opacity))/255.0f);
        if(stencil) style.stencil=*stencil;
        MeshVersion mesh{m_renderer,part.mesh,false};
        if(!Mesh_For_Pose(part,pose,mesh)) return false;
        return m_renderer->Draw(commands,mesh.handle,style,parameters,Textures(part,shroud));
    }
    // A material pass over a part (the original's Push_Material_Pass): its geometry again, untextured with the
    // parameters' replacement material, blended and depth-tested as `pass` says.
    bool Draw_Part_Pass(CommandList& commands,std::size_t index,PropParameters parameters,const PropStyle& pass,
        const ModelAssetPose* pose=nullptr,std::uint32_t lod=0,RHITextureHandle shroud={}) const {
        if(!m_renderer || index>=m_parts.size()) return false;
        const auto& part=m_parts[index];
        if(part.lod!=lod) return true;
        bool visible=true;
        if(!Prepare_Pose(part,pose,parameters,visible)) return false;
        if(!visible) return true;
        Prepare(part,parameters);
        parameters.textured=0;
        parameters.surface.shading_model=0;
        parameters.alpha_cutoff=0;
        PropStyle style=part.style;
        style.source_blend=pass.source_blend;
        style.destination_blend=pass.destination_blend;
        style.depth_write=pass.depth_write;
        style.depth_comparison=pass.depth_comparison;
        MeshVersion mesh{m_renderer,part.mesh,false};
        if(!Mesh_For_Pose(part,pose,mesh)) return false;
        return m_renderer->Draw(commands,mesh.handle,style,parameters,Textures(part,shroud));
    }
    bool Submit_Part(PropSubmission& submission,std::size_t index,PropParameters parameters,
        PropDrawPhase phase,const std::array<float,4>& camera_depth={},
        const ModelAssetPose* pose=nullptr,std::uint32_t lod=0,const PropTextureMappingContext* mapping=nullptr) const {
        if(!m_device || index>=m_parts.size()) return false;
        const auto& part=m_parts[index];
        if(part.lod!=lod) return true;
        bool visible=true;
        if(!Prepare_Pose(part,pose,parameters,visible)) return false;
        if(!visible) return true;
        Prepare(part,parameters,mapping);
        MeshVersion mesh{m_renderer,part.mesh,false};
        if(!Mesh_For_Pose(part,pose,mesh)) return false;
        std::size_t retained=0;
        for(;retained<part.textures.size();++retained) {
            const auto texture=part.textures[retained];
            if(texture.Is_Valid() && !m_device->Retain_Texture(texture)) break;
        }
        if(retained==part.textures.size() && submission.Submit(mesh.handle,part.style,parameters,part.textures,phase,camera_depth)) return true;
        for(std::size_t i=0;i<retained;++i) if(part.textures[i].Is_Valid()) m_device->Destroy_Texture(part.textures[i]);
        return false;
    }
private:
    bool Append_Part(const PropAssetPart& source,const Assets::ModelAsset& model,Assets::AssetCache& assets,
        std::string& error,Assets::TextureAssetHandle base_override,
        std::span<const std::pair<Assets::TextureAssetHandle,Assets::TextureAssetHandle>> base_replacements) {
            if(source.material_index>=model.Materials().size()) {error="invalid prepared material index";return false;}
            const auto* material=assets.Try_Get_Material(model.Materials()[source.material_index].asset_handle);
            if(!material) { error="model material is not ready"; return false; }
            Part part; part.name=source.name;part.sort_level=source.sort_level;
            part.skinning=source.skinning;
            if(!part.skinning.empty()) {
                if(!m_rest_pose.Bone_Count()) { error="skin requires a resolved skeleton"; return false; }
                part.bind_vertices=source.vertices; part.indices=source.indices;
            }
            if(m_rest_pose.Bone_Count()) {
                const auto attached=Model_Part_Bone(model.Rig(),source.name);
                if(!attached) { error="model part has no hierarchy attachment"; return false; }
                part.bone=attached->first; part.lod=attached->second;
            }
            part.style.depth_write=material->Depth_Write();
            for(unsigned stage=0;stage<part.mappings.size();++stage) {
                if(const auto& description=material->Texture_Mappings()[stage]) {
                    try { part.mappings[stage]=TextureMapping::Create(*description,0); }
                    catch(const std::exception& exception) { error=exception.what();return false; }
                }
            }
            auto base=base_override.Is_Valid() ? base_override : material->Primary_Texture();
            for(const auto& [original,replacement]:base_replacements)
                if(!base_override.Is_Valid() && original==base && replacement.Is_Valid()) { base=replacement; break; }
            part.textured=material->Texturing() && base.Is_Valid();
            if(!Upload(assets,base,part.textures[0])) { error="could not upload base map"; return false; }
            if(material->Secondary_Texture().Is_Valid()) { error="secondary stage requires an explicit stage binding"; return false; }
            std::uint32_t maps=0;
            for(std::size_t role=0;role<PropSurfaceTextureCount;++role) {
                const auto texture=material->Surface_Texture(static_cast<Assets::MaterialTextureRole>(role));
                if(texture.Is_Valid()) maps|=1u<<role;
                if(!Upload(assets,texture,part.textures[PropSurfaceTextureFirst+role])) { error="could not upload surface map"; return false; }
            }
            if(!Configure_Prop_Surface(material->Surface(),maps,part.surface)) { error="invalid surface description"; return false; }
            switch(material->Render_Mode()) {
            case Assets::MaterialRenderMode::AlphaTest: part.alpha_cutoff=material->Surface().alpha_cutoff; break;
            case Assets::MaterialRenderMode::AlphaBlend:
                part.style.source_blend=RHIBlendFactor::SourceAlpha; part.style.destination_blend=RHIBlendFactor::InverseSourceAlpha; break;
            case Assets::MaterialRenderMode::Additive:
                part.style.source_blend=RHIBlendFactor::One; part.style.destination_blend=RHIBlendFactor::One; break;
            case Assets::MaterialRenderMode::Multiply:
                part.style.source_blend=RHIBlendFactor::Zero; part.style.destination_blend=RHIBlendFactor::SourceColor; break;
            default: break;
            }
            if(const auto& authored=material->Draw_State()) {
                if(!Assets::Validate_Material_Draw_State(*authored)) { error="invalid authored material draw state"; return false; }
                constexpr std::array factors{RHIBlendFactor::Zero,RHIBlendFactor::One,
                    RHIBlendFactor::SourceColor,RHIBlendFactor::InverseSourceColor,
                    RHIBlendFactor::SourceAlpha,RHIBlendFactor::InverseSourceAlpha};
                constexpr std::array comparisons{RHIComparison::Never,RHIComparison::Less,
                    RHIComparison::Equal,RHIComparison::LessEqual,RHIComparison::Greater,
                    RHIComparison::NotEqual,RHIComparison::GreaterEqual,RHIComparison::Always};
                part.style.source_blend=factors[static_cast<std::size_t>(authored->source)];
                part.style.destination_blend=factors[static_cast<std::size_t>(authored->destination)];
                part.style.depth_comparison=comparisons[static_cast<std::size_t>(authored->depth_comparison)];
            }
            part.mesh=m_renderer->Create_Mesh(source.vertices,source.indices);
            if(!part.mesh.Is_Valid()) { error="could not create model geometry"; return false; }
            m_parts.push_back(std::move(part));
        return true;
    }
    static constexpr std::size_t Shroud_Texture_Slot=3;
    struct Part;
    static std::array<RHITextureHandle,PropTextureCount> Textures(const Part& part,RHITextureHandle shroud) noexcept;
    struct MeshVersion {
        PropRenderer* renderer;
        PropMeshHandle handle;
        bool temporary;
        ~MeshVersion() { if(temporary && handle.Is_Valid()) renderer->Destroy_Mesh(handle); }
    };
    struct Part {
        std::string name;
        std::int32_t sort_level=0;
        PropMeshHandle mesh;
        PropStyle style;
        PropSurfaceParameters surface;
        float alpha_cutoff=0;
        bool textured=false;
        std::uint32_t bone=Invalid_Bone_Index;
        std::uint32_t lod=0;
        std::array<RHITextureHandle,PropTextureCount> textures{};
        std::array<std::shared_ptr<TextureMapping>,2> mappings{};
        std::vector<PropVertex> bind_vertices;
        std::vector<std::uint32_t> indices;
        std::vector<PropSkinInfluences> skinning;
    };
    bool Mesh_For_Pose(const Part& part,const ModelAssetPose* instance,MeshVersion& result) const {
        if(part.skinning.empty()) return true;
        const auto& pose=instance ? *instance : m_rest_pose;
        std::vector<RenderTransform> bones(pose.Bone_Count());
        for(std::size_t i=0;i<bones.size();++i) if(!pose.Bone_Transform(static_cast<std::uint32_t>(i),bones[i])) return false;
        std::vector<PropVertex> vertices;
        if(!Pose_Prop_Vertices(part.bind_vertices,part.skinning,bones,vertices)) return false;
        result.handle=m_renderer->Create_Mesh(vertices,part.indices); result.temporary=true;
        return result.handle.Is_Valid();
    }
    bool Prepare_Pose(const Part& part,const ModelAssetPose* instance,PropParameters& parameters,bool& visible) const {
        if(part.bone==Invalid_Bone_Index) return instance==nullptr;
        const auto& pose=instance ? *instance : m_rest_pose;
        if(pose.Skeleton_Name()!=m_rest_pose.Skeleton_Name() || pose.Bone_Count()!=m_rest_pose.Bone_Count()) return false;
        RenderTransform bone;
        if(!pose.Bone_Transform(part.bone,bone)) return false;
        visible=pose.Visible(part.bone);
        // Skin vertices already contain the evaluated skeleton transform.
        if(!part.skinning.empty()) return true;
        const auto world=parameters.world;
        for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c) {
            float value=0;for(unsigned k=0;k<4;++k)value+=world[r*4+k]*bone.matrix[k*4+c];
            parameters.world[r*4+c]=value;
        }
        return true;
    }
    static void Prepare(const Part& part,PropParameters& parameters,const PropTextureMappingContext* context=nullptr) {
        const auto team_color=parameters.surface.team_color;
        parameters.surface=part.surface; parameters.surface.team_color=team_color;
        parameters.textured=part.textured ? 1 : 0;
        parameters.alpha_cutoff=part.alpha_cutoff;
        const PropTextureMappingContext defaults;
        const auto& frame=context ? *context : defaults;
        for(unsigned stage=0;stage<part.mappings.size();++stage) if(const auto& mapping=part.mappings[stage]) {
            const auto result=mapping->Evaluate(frame.milliseconds,parameters.view,frame.projection);
            parameters.uv_transform[stage]=result.transform;
            parameters.uv_sources[stage*2]=static_cast<float>(result.coordinates.source);
            parameters.uv_sources[stage*2+1]=result.coordinates.projected ? 1.0f : 0.0f;
            if(result.bump) parameters.bump_matrix=*result.bump;
        }
    }
    bool Upload(Assets::AssetCache& assets,Assets::TextureAssetHandle handle,RHITextureHandle& result) {
        if(!handle.Is_Valid()) { result={}; return true; }
        for(const auto& entry:m_textures) if(entry.first==handle) { result=entry.second; return true; }
        const auto* source=assets.Try_Get_Texture(handle);
        if(!source || !source->Has_Pixels()) return false;
        // W3D's mesh textures load with every mip level (MIP_LEVELS_ALL, box filtered): far off they do not shimmer.
        if(m_prepared_textures) {
            const auto prepared=std::ranges::find_if(*m_prepared_textures,[&](const auto& entry) {return entry.first==handle;});
            if(prepared==m_prepared_textures->end() || !prepared->second) return false;
            result=Upload_Mip_Chain(*m_device,*prepared->second);
        } else result=Create_Mipped_Texture(*m_device,source->Width(),source->Height(),source->Pixels(),source->Row_Pitch(),
            0,m_mip_reduction,m_minimum_texture_dimension);
        if(!result.Is_Valid()) return false;
        m_textures.emplace_back(handle,result); return true;
    }
    std::unique_ptr<PropAssetBinding> m_pending;
    PreparedPropAsset m_loading;
    std::size_t m_loading_cursor{};
    Assets::TextureAssetHandle m_base_override;
    std::vector<std::pair<Assets::TextureAssetHandle,Assets::TextureAssetHandle>> m_base_replacements;
    std::shared_ptr<const PreparedPropTextures> m_prepared_textures;
    Device* m_device=nullptr;
    unsigned m_mip_reduction{},m_minimum_texture_dimension{1};
    PropRenderer* m_renderer=nullptr;
    ModelAssetPose m_rest_pose;
    std::vector<Part> m_parts;
    std::vector<std::pair<Assets::TextureAssetHandle,RHITextureHandle>> m_textures;
};
inline std::array<RHITextureHandle,PropTextureCount> PropAssetBinding::Textures(const Part& part,RHITextureHandle shroud) noexcept
{
    auto textures=part.textures;
    if(shroud.Is_Valid()) textures[Shroud_Texture_Slot]=shroud;
    return textures;
}
}
