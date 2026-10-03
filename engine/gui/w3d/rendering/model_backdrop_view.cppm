export module engine.gui.w3d.model_backdrop_view;
import std;
export import Graphics.Scene.Props.AssetBinding;
import Graphics.Scene.Views.CameraState;
import Assets.Models;
import Assets.Cache;
import Graphics.Renderer2D;
import engine.gui.w3d.text_view;

export namespace engine::gui::w3d
{
struct ModelBackdropCamera
{
	std::array<float,3> position;
	float horizontal_fov{},near_clip{},far_clip{};
	std::string bone;
	Graphics::RenderTransform bone_axes;
};
struct BackdropText
{
	std::u16string text;
	float x{},y{},wrap{};
	std::uint32_t color{};
	bool large{};
};
// Reusable W3D GUI model view. The host injects device, renderer and assets;
// content supplies the camera convention, localized labels and font roles.
class ModelBackdropView
{
public:
	bool BeginUpload(Graphics::Device& device,Graphics::PropRenderer& renderer,Assets::AssetCache& assets,
		Graphics::PreparedPropAsset prepared,const Assets::ModelRigDesc& rig,ModelBackdropCamera camera,std::string& error) {
		m_ready=false;
		if(!std::isfinite(camera.horizontal_fov) || camera.horizontal_fov<=0 || camera.horizontal_fov>=std::numbers::pi_v<float> ||
			!std::isfinite(camera.near_clip) || !std::isfinite(camera.far_clip) || camera.near_clip<=0 || camera.far_clip<=camera.near_clip) {
			error="invalid backdrop camera";return false;
		}
		m_camera_description=std::move(camera);
		if(!m_pose.Initialize(rig,error)) return false;
		if(rig.animations.empty() || !rig.animations.front().frame_count) {error="backdrop has no progress animation";return false;}
		m_last_frame=float(rig.animations.front().frame_count-1);
		const auto& position=m_camera_description.position;
		m_camera.Set_Position({position[0],position[1],position[2]});m_camera.Set_Clip_Planes(m_camera_description.near_clip,m_camera_description.far_clip);
		Graphics::RenderTransform bone;
		const auto index=m_pose.Bone_Index(m_camera_description.bone);
		if(index!=Graphics::Invalid_Bone_Index && m_pose.Bone_Transform(index,bone)) m_camera.Set_Transform(Graphics::Multiply_Affine(bone,m_camera_description.bone_axes));
		return m_binding.Begin_Load(device,renderer,assets,std::move(prepared),error);
	}
	Graphics::PropAssetLoadState AdvanceUpload(Assets::AssetCache& assets,std::chrono::steady_clock::time_point deadline,std::string& error) {
		const auto state=m_binding.Advance_Load(assets,1,deadline,error);m_ready=state==Graphics::PropAssetLoadState::Ready;return state;
	}
	bool Ready() const noexcept {return m_ready;}
	bool Draw(Graphics::CommandList& commands,unsigned width,unsigned height,float percentage,std::uint32_t milliseconds,const Graphics::PropParameters& lighting) {
		if(!m_ready || !width || !height || !m_pose.Evaluate(0,m_last_frame*std::clamp(percentage,0.f,1.f),false)) return false;
		m_camera.Set_View_Plane(m_camera_description.horizontal_fov,float(height)/width*m_camera_description.horizontal_fov);
		auto parameters=lighting;parameters.view=m_camera.Get_View_Matrix().values;parameters.view_projection={};
		const auto projection=m_camera.Get_Backend_Projection_Matrix().values;
		for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c) for(unsigned k=0;k<4;++k) parameters.view_projection[r*4+c]+=projection[r*4+k]*parameters.view[k*4+c];
		const Graphics::PropTextureMappingContext mapping{milliseconds,projection};
		for(std::size_t part=0;part<m_binding.Part_Count();++part) if(!m_binding.Draw_Part(commands,part,parameters,&m_pose,0,{},nullptr,&mapping)) return false;
		return true;
	}
	static bool DrawLabels(Graphics::Renderer2D& renderer,std::span<const BackdropText> lines,
		const text::FontFace& normal,const text::FontFace& large,float reference_width,float reference_height,unsigned width,unsigned height) {
		if(reference_width<=0 || reference_height<=0 || !width || !height) return false;
		const float x_scale=width/reference_width,y_scale=height/reference_height;
		for(const auto& line:lines) {
			const float x=std::trunc(line.x*x_scale),y=std::trunc(line.y*y_scale);
			text::TextStyle style;style.color=Graphics::Color2D::From_ARGB(line.color);style.drop_color={0,0,0,0};style.x_drop=style.y_drop=0;
			TextBoxOptions options;options.wrap=line.wrap>0;
			if(!Draw_Text_Box(line.large ? large : normal,renderer,line.text,{x,y,x+(options.wrap ? line.wrap*x_scale : float(width)),std::max(y,float(height))},style,options)) return false;
		}
		return true;
	}
private:
	Graphics::PropAssetBinding m_binding;
	Graphics::ModelAssetPose m_pose;
	Graphics::CameraState m_camera;
	ModelBackdropCamera m_camera_description;
	float m_last_frame{};
	bool m_ready{};
};
}
