export module Assets.Adapters.W3D.ResolvedModel;
import std;
import Assets.Identity;
import Assets.Models;
import Assets.Models.Composition;
import Assets.Importers.Models;
import Assets.Adapters.W3D.Chunks;
import Assets.Adapters.W3D.Mesh;
import Assets.Adapters.W3D.Model;
import Assets.Adapters.W3D.Rig;
import Assets.Adapters.W3D.Aggregate;
import Assets.Adapters.W3D.Box;
import Assets.Adapters.W3D.Null;
import Assets.Adapters.W3D.RequestPolicy;
import Assets.Adapters.W3D.Particles;

namespace Assets::W3D::ResolvedDetail
{
class Reader
{
public:
	explicit Reader(const AssetSource &source) : m_source(source) {}
	std::unique_ptr<ModelAssetDesc> Load(std::string name, W3DByteSpan supplied = {})
	{
		name = Canonicalize_Asset_Name(name);
		if (name.ends_with(".w3d")) name.resize(name.size() - 4);
		if (m_active.size() >= 64 || !m_active.insert(name).second) {error = "cyclic or excessive W3D model composition: " + name; return {};}
		struct Guard {std::set<std::string> &active; std::string name; ~Guard() {active.erase(name);}} guard{m_active, name};
		const auto file = W3D_Request_Filename(W3DRequestKind::Model, name);
		if (!file) {error = "invalid W3D composition identity"; return {};}
		const auto bytes = supplied.empty() ? Source(AssetType::Model, *file) : supplied;
		if (bytes.empty() || !W3DValidate_Chunk_Tree(bytes)) {error = "missing or malformed W3D composition source: " + *file; return {};}
		auto model = std::make_unique<ModelAssetDesc>(); model->name = name; model->source_name = *file; model->source_format = "W3D";
		std::optional<W3DAggregateDescription> aggregate;
        bool hierarchical{},drawable_objects{};
        std::vector<EmitterAssetDesc> emitter_prototypes;
		if (!W3DVisit_Chunks(bytes, [&](const auto &chunk) {
            if(chunk.id==W3DChunkEmitter) {
                EmitterAssetDesc description;
                if(!W3DRead_Emitter(chunk.payload,description,error)) return false;
                emitter_prototypes.push_back(std::move(description));
            }
			if (chunk.id == W3DChunkAggregate) {
				W3DAggregateDescription value;
				if (aggregate || !W3DRead_Model_Aggregate(chunk.payload, value, error)) return false;
				aggregate = std::move(value);
			}
			if (chunk.id == 0x700 || chunk.id == 0x300) hierarchical = true;
			return true;
		})) return {};
        // Embedded prototypes belong to the hierarchy's authored attachments.
        // Activating one again at the model root duplicates its particles.
        if(!hierarchical && !aggregate && !emitter_prototypes.empty()) {
            if(emitter_prototypes.size()!=1) {error="ambiguous standalone emitter identity";return {};}
            model->emitter=std::move(emitter_prototypes.front());
        }
		if (aggregate) {
			auto base = Load(aggregate->model.base_model); if (!base) return {};
			std::size_t index{};
			for (const auto &attachment : aggregate->model.attachments) {
				const auto bone = std::ranges::find_if(base->rig.bones, [&](const auto &value) {
					return Canonicalize_Asset_Name(value.name) == Canonicalize_Asset_Name(attachment.bone_name);
				});
				if (bone == base->rig.bones.end()) {error = "aggregate attachment bone is missing: " + attachment.bone_name; return {};}
				const auto parent = static_cast<std::uint32_t>(bone - base->rig.bones.begin());
				auto child = Load(attachment.model_name); if (!child) return {};
				if (!Append_Model_Attachment(*base, *child, parent, aggregate->model.match_detail_levels,
					name + "/" + std::to_string(index++), error)) return {};
			}
			base->name = aggregate->model.name; base->source_name = *file;
			return base;
		}
		if (!W3DRead_Model_Rig(bytes, model->rig, error)) return {};
		if (hierarchical) {
			if (!Skeleton(model->rig)) return {};
			const auto attachments = model->rig.attachments;
			for (const auto &attachment : attachments) {
				const auto child_file = W3D_Request_Filename(W3DRequestKind::Model, attachment.object_name);
				if (!child_file) {error = "invalid W3D child identity"; return {};}
				const auto child_bytes = Canonicalize_Asset_Name(*child_file) == Canonicalize_Asset_Name(*file)
					? bytes : Source(AssetType::Model, *child_file);
                // HLodClass constructs each LOD by adding only non-null
                // Create_Render_Obj results. Retail lower detail exports can
                // retain references to absent collision/helper prototypes.
                if(child_bytes.empty()) continue;
				bool matched{}, no_draw{};
				W3DParsedMesh mesh;
				std::optional<W3DBoxDescription> box;
                std::optional<EmitterAssetDesc> emitter;
				if (!Mesh(child_bytes, attachment.object_name, mesh, matched, no_draw,box,&emitter)) return {};
                if(!matched) continue;
                if(emitter) model->emitters.push_back({std::move(*emitter),attachment.bone,attachment.lod});
				if(box) model->collision.boxes.push_back({attachment.object_name,box->center,box->extent,
					(box->attributes>>W3DBoxAttributeCollisionTypeShift)&0xffu,box->Is_Aligned(),attachment.bone,attachment.lod,true});
				if (no_draw) continue;
                drawable_objects=true;
				const auto first = model->submeshes.size();
				const auto collision_first=model->collision.parts.size();
				W3DAppend_Mesh(*model, mesh);
				for (std::size_t part = first; part < model->submeshes.size(); ++part) model->submeshes[part].name = attachment.object_name;
				for(std::size_t part=collision_first;part<model->collision.parts.size();++part) {
					auto& collision=model->collision.parts[part];collision.name=attachment.object_name;
					collision.bone=attachment.bone;collision.lod=attachment.lod;collision.attached=true;
				}
			}
		} else {
			bool found{}, no_draw{}; W3DParsedMesh mesh;
			if (name.find('.') != std::string::npos) {
				std::optional<W3DBoxDescription> box;
				if (!Mesh(bytes, name, mesh, found, no_draw,box)) return {};
				if (found && !no_draw) W3DAppend_Mesh(*model, mesh);
			} else if (!W3DVisit_Chunks(bytes, [&](const auto &chunk) {
				if (chunk.id != W3DChunkMesh) return true;
				if (!W3DParse_Mesh(chunk.payload, mesh, error)) return false;
				W3DAppend_Mesh(*model, mesh); found = true; return true;
			})) return {};
			if ((!found || no_draw) && !model->emitter) {error = "W3D composition has no drawable mesh: " + name; return {};}
		}
        if((hierarchical && !drawable_objects && !model->rig.bones.empty()) || model->emitter && model->vertices.empty()) {
            // Valid hierarchy/null/box assemblies are transform controllers
            // and attachment hosts. They need no GPU mesh; retain their rig
            // and collision descriptions with a finite point bound.
            model->bounds.minimum={};model->bounds.maximum={};
        } else if (model->vertices.empty() || model->indices.empty() || !model->bounds.Is_Valid()) {error = "W3D composition has no valid drawable geometry: " + name; return {};}
		if (model->skin_bone_count > model->rig.bones.size()) {error = "W3D skin indices exceed resolved skeleton"; return {};}
        const auto dependency=[&](const EmitterAssetDesc& emitter) {if(!emitter.texture_name.empty()) model->dependencies.push_back({AssetType::Texture,emitter.texture_name});};
        if(model->emitter) dependency(*model->emitter);for(const auto& emitter:model->emitters) dependency(emitter.description);
		return model;
	}
	std::string error;
private:
	W3DByteSpan Source(AssetType type, const std::string &file) {
		const auto key = std::to_string(static_cast<unsigned>(type)) + ":" + Canonicalize_Asset_Name(file);
		if (const auto found = m_sources.find(key); found != m_sources.end()) return found->second;
		return m_sources.emplace(key, m_source({type, Canonicalize_Asset_Name(file)})).first->second;
	}
	bool Skeleton(ModelRigDesc &rig) {
		if (rig.skeleton_name.empty() || !rig.bones.empty()) return true;
		const auto file = W3D_Request_Filename(W3DRequestKind::Skeleton, rig.skeleton_name);
		ModelRigDesc source;
		if (!file || !W3DRead_Model_Rig(Source(AssetType::Skeleton, *file), source, error) || source.bones.empty()) {
			error = "missing W3D assembly skeleton: " + rig.skeleton_name; return false;
		}
		if (Canonicalize_Asset_Name(source.skeleton_name) != Canonicalize_Asset_Name(rig.skeleton_name)) {error = "W3D assembly skeleton identity mismatch"; return false;}
		rig.bones = std::move(source.bones); return Validate_Model_Rig(rig, error);
	}
	bool Mesh(W3DByteSpan bytes, std::string_view name, W3DParsedMesh &result, bool &found, bool &no_draw,std::optional<W3DBoxDescription>& box_result,std::optional<EmitterAssetDesc>* emitter_result=nullptr) {
		if (bytes.empty() || !W3DValidate_Chunk_Tree(bytes)) {error = "missing or malformed W3D mesh source for " + std::string(name); return false;}
		return W3DVisit_Chunks(bytes, [&](const auto &chunk) {
			std::string identity;
			std::optional<W3DBoxDescription> decoded_box;
            std::optional<EmitterAssetDesc> decoded_emitter;
			if (chunk.id == W3DChunkMesh) {
				W3DMeshHeader header; bool decoded{};
				if (!W3DVisit_Chunks(chunk.payload, [&](const auto &child) {
					if (child.id == W3DChunkMeshHeader3) decoded = W3DRead_Mesh_Header(child.payload, header); return true;
				}) || !decoded) {error = "invalid W3D composed mesh header"; return false;}
				identity = header.container_name.empty() ? header.name : header.container_name + "." + header.name;
			} else if (chunk.id == W3DChunkBox) {
				W3DBoxDescription box; if (!W3DRead_Box(chunk.payload, box, error)) return false; identity = box.name;decoded_box=std::move(box);
			} else if (chunk.id == W3DChunkNullObject) {
				W3DNullDescription empty; if (!W3DRead_Null(chunk.payload, empty, error)) return false; identity = empty.name;
            } else if(chunk.id==W3DChunkEmitter) {
                EmitterAssetDesc emitter;if(!W3DRead_Emitter(chunk.payload,emitter,error)) return false;identity=emitter.name;decoded_emitter=std::move(emitter);
			} else return true;
			if (Canonicalize_Asset_Name(identity) != Canonicalize_Asset_Name(name)) return true;
			if (found) {error = "ambiguous W3D child object"; return false;} found = true;
			box_result=std::move(decoded_box);
            if(emitter_result) *emitter_result=std::move(decoded_emitter);
			no_draw = chunk.id != W3DChunkMesh;
			return no_draw || W3DParse_Mesh(chunk.payload, result, error);
		});
	}
	const AssetSource &m_source;
	std::map<std::string, std::vector<std::byte>> m_sources;
	std::set<std::string> m_active;
};
}

export namespace Assets::W3D
{
inline ModelImportResult W3DResolve_Model(const AssetIdentity &identity, W3DByteSpan bytes, const AssetSource &source)
{
	ResolvedDetail::Reader reader(source); auto model = reader.Load(identity.canonical_name, bytes);
	return {std::move(model), std::move(reader.error)};
}
}
