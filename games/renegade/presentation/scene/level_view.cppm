export module games.renegade.presentation.scene.level_view;
import std;
import engine.camera.motion.anchor_transition;
import games.renegade.content.presentation.background_colors;
import games.renegade.presentation.scene.cinematic_assets;
import Graphics.Scene.AffineTransform;
import engine.time.frame_accumulator;
import Engine.Core.Math.AffineTransform3;
import Engine.Core.Math.FixedPresentation;
import games.renegade.session;
import games.renegade.content.levels.render_assets;
export import games.renegade.content.campaign.campaign_content;
import Assets.Cache;
import Assets.Handles;
import Assets.Identity;
import Assets.Models;
import Assets.Adapters.W3D.Geometry;
import Graphics.Scene.Props.AssetBinding;
import Graphics.Resources.Loading.Queue;
export import engine.gui.w3d.loading;
import engine.gui.w3d.model_backdrop_view;
import games.renegade.content.presentation.loading_screen;
import games.renegade.content.presentation.strings;
import engine.gui.text.font_face;
import Graphics.Renderer2D;
import Graphics.Scene.Props.Renderer;
import Graphics.Scene.Views.CameraState;
import Graphics.Scene.StaticDrawOrder;
import Graphics.Scene.Models.AssetPose;
import engine.filesystem.core.virtual_file_system;
import engine.level.adapters.model.model_collision;
import games.renegade.content.characters.actor_animation;
import games.renegade.content.levels.background;
import Graphics.Scene.Environment.Dome;
import games.renegade.content.levels.sounds;
import games.renegade.content.campaign.conversations;
import games.renegade.presentation.scene.speech_assets;
import games.renegade.content.audio.sound_preset;
import engine.audio.scene.emitter_player;
import engine.audio.decoders.ffmpeg.ffmpeg_decoder;
import Graphics.Scene.Particles.EmitterView;
import Graphics.Scene.Particles.EmitterAssetBinding;
import games.renegade.hud.help_text_view_model;
import games.renegade.hud.objective_view_model;
import games.renegade.hud.player_hud_view_model;
import games.renegade.presentation.player.input;
import engine.gui.images;
import engine.gui.w3d.text_view;

export namespace renegade::presentation
{
// A presentation owner, like GeneralsZH's object view. Simulation state stays
// in archetype columns; GPU bindings consume the completed ECS snapshot.
struct LevelViewServices
{
    Graphics::Device& device;
    Assets::AssetCache& assets;
    const engine::filesystem::VirtualFileSystem& files;
    Graphics::PropRenderer& renderer;
    Graphics::PropSubmission& submission;
    Graphics::ResourceLoadQueue& loader;
    const content::StringCatalog& strings;
    engine::audio::Mixer& audio;
    bool startup_cinematics{true};
};
// Presentation layers also permit same-tick GPU comparisons. They never
// disable projectile simulation, collision or the particle lifetime clock.
struct LevelDrawLayers {
    bool projectile_models{true};
    bool projectile_emitters{true};
    std::optional<Engine::Math::Fixed> mechanism_frame;
    bool player_body{true};
    bool speech_animation{true};
};
struct LevelInput {
    std::function<HumanControl()> sample;
    std::function<Engine::Math::LookAngles()> orientation;
    std::function<void(Engine::Math::LookAngles)> set_orientation;
    std::function<PlayerWeaponCommand()> weapon;
};
class LevelView
{
public:
    explicit LevelView(LevelViewServices services):m_services(services) {}
    ~LevelView() {Cancel();if(m_cinematic_music) m_services.audio.Stop(m_cinematic_music);}
    LevelView(const LevelView&)=delete;
    LevelView& operator=(const LevelView&)=delete;
    bool BeginLoad(std::string map,int difficulty=1) {
        if(m_load || m_session) return false;
        m_load=std::make_shared<LoadState>();
        m_loading.Begin(u"Loading level",[this] {RequestCancel();});
        const auto state=m_load;
        const auto services=m_services;
        m_source=std::make_shared<const Graphics::ResourceLoadSource>([state,services,map=std::move(map),difficulty] {
            return std::make_unique<LoadJob>(state,services.assets,services.files,services.strings,map,difficulty,services.audio.SampleRate(),services.startup_cinematics);
        });
        if(!m_services.loader.Request(m_source,Graphics::ResourceLoadPriority::Background)) {
            m_error="background loading queue is unavailable";m_loading.Fail(Wide(m_error));return false;
        }
        return true;
    }
    // Poll never waits on I/O, futures or worker completion. A GPU operation is
    // indivisible, but at most one material batch is issued before rechecking
    // the frame deadline. CPU batching and texture filtering run in Decode.
    void Poll(std::chrono::nanoseconds budget=std::chrono::milliseconds(2)) {
        auto& device=m_services.device;auto& assets=m_services.assets;
        if(!m_load || Ready() || Failed() || m_load->cancelled.load(std::memory_order_relaxed)) return;
        const auto upload_deadline=std::chrono::steady_clock::now()+std::max(budget,std::chrono::nanoseconds::zero());
        if(!m_backdrop_ready) {
            if(!m_prepared_backdrop) m_prepared_backdrop=m_load->backdrop.exchange({},std::memory_order_acquire);
            if(m_prepared_backdrop) {
                std::string error;
                if(!m_backdrop_started) {
                    engine::gui::w3d::ModelBackdropCamera camera{{0,0,800},std::numbers::pi_v<float>/4,5,12000,"CAMERA",
                        Graphics::RenderTransform{{0,0,-1,0,-1,0,0,0,0,1,0,0,0,0,0,1}}};
                    if(!m_backdrop.BeginUpload(device,m_services.renderer,assets,std::move(m_prepared_backdrop->gpu),m_prepared_backdrop->rig,std::move(camera),error)) {
                        m_error=error;m_loading.Fail(Wide(error));return;
                    }
                    m_backdrop_started=true;
                }
                const auto status=m_backdrop.AdvanceUpload(assets,upload_deadline,error);
                if(status==Graphics::PropAssetLoadState::Failed) {m_error=error;m_loading.Fail(Wide(error));return;}
                if(status==Graphics::PropAssetLoadState::Ready) {
                    m_backdrop_ready=true;m_backdrop_text=std::move(m_prepared_backdrop->labels);
                    std::printf("retail loading screen: Backdrop%d, %s, %zu localized labels\n",m_prepared_backdrop->layout.number,m_prepared_backdrop->layout.model.c_str(),m_backdrop_text.size());
                    m_prepared_backdrop.reset();
                }
            }
        }
        if(!m_load->complete) {
            const auto progress=m_load->progress.load(std::memory_order_relaxed);
            m_loading.Report(progress<10 ? u"Reading retail level" : u"Loading game data",progress);
            return;
        }
        if(!m_prepared) {
            if(!m_load->prepared) {m_error=m_load->error.empty() ? "level preparation failed" : m_load->error;m_loading.Fail(Wide(m_error));return;}
            m_prepared=std::move(m_load->prepared);
            for(const auto& model:m_prepared->models) if(model.drawable) m_upload_total+=model.gpu.geometry.size();
        }
        const auto deadline=upload_deadline;
        while(m_model_cursor<m_prepared->models.size() && std::chrono::steady_clock::now()<deadline) {
            auto& model=m_prepared->models[m_model_cursor];
            if(model.drawable) {
                std::string error;
                if(!m_upload_started) {
                    if(!model.view->binding.Begin_Load(device,m_services.renderer,assets,std::move(model.gpu),error)) {
                        m_error=model.name+": "+error;m_loading.Fail(Wide(m_error));return;
                    }
                    m_upload_started=true;
                }
                const auto previous=model.view->binding.Uploaded_Parts();
                const auto status=model.view->binding.Advance_Load(assets,1,deadline,error);
                if(status==Graphics::PropAssetLoadState::Failed) {m_error=model.name+": "+error;m_loading.Fail(Wide(m_error));return;}
                m_uploaded+=model.view->binding.Uploaded_Parts()-previous;
                m_loading.Report(u"Preparing scene",70+static_cast<unsigned>(29*m_uploaded/std::max<std::size_t>(1,m_upload_total)));
                if(status==Graphics::PropAssetLoadState::Pending) continue;
                model.drawable=false; // completed upload; particles may need another frame
            }
            while(model.emitter_cursor<model.emitters.size() && std::chrono::steady_clock::now()<deadline) {
                const auto& prepared=model.emitters[model.emitter_cursor];auto binding=std::make_shared<Graphics::EmitterAssetBinding>();
                if(!binding->Initialize(device,prepared.asset)) {m_error="emitter texture upload failed";m_loading.Fail(Wide(m_error));return;}
                model.view->emitters.push_back({prepared.asset.description,std::move(binding),prepared.bone,prepared.lod});++model.emitter_cursor;
            }
            if(model.emitter_cursor<model.emitters.size()) continue;
            m_models.emplace(model.id,std::move(model.view));++m_model_cursor;m_upload_started=false;
        }
        if(m_model_cursor!=m_prepared->models.size()) return;
        m_sky=std::make_unique<Graphics::DomeRenderer>(m_services.renderer);
        if(!m_sky->Initialize(m_prepared->sky)) {m_error="sky geometry upload failed";m_loading.Fail(Wide(m_error));return;}
        m_objective_textures=std::move(m_prepared->objective_textures);
        m_hud_settings=m_prepared->hud_settings;
        m_sound_catalog=std::move(m_prepared->sounds);m_sound_library=std::move(m_prepared->sound_library);
        m_audio=std::make_unique<engine::audio::EmitterPlayer>(m_services.audio,m_sound_library);
        m_camera=std::move(m_prepared->camera);m_profile=m_prepared->profile;m_first_profile=m_prepared->profile;m_third_profile=m_prepared->third_profile;m_ambient=m_prepared->ambient;m_pose_library=std::make_shared<const std::vector<Graphics::ModelAssetPose>>(std::move(m_prepared->pose_library));m_session=std::move(m_prepared->session);
        m_cinematic_clock=m_prepared->cinematic_clock;m_cinematic_slots=std::move(m_prepared->cinematic_slots);
        if(m_prepared->cinematic_music) {
            engine::audio::VoiceStart voice;voice.buffer=m_prepared->cinematic_music;voice.bus=engine::audio::Bus::Music;voice.loop=true;
            voice.fadeInFrames=std::uint32_t(std::uint64_t(m_prepared->music_fade_ms)*m_services.audio.SampleRate()/1000);
            m_cinematic_music=m_services.audio.Play(std::move(voice));
        }
        UpdateCamera({m_session->World().Get<engine::gameplay::Transform>(m_session->World().Resource<SceneState>().player)->facing,{}});
        const auto& state=m_session->World().Resource<SceneState>();
        std::printf("retail level loaded: %s, %zu statics, %zu models (%zu collision boxes), player spawner %u, preset %u, startup %s\n",
            m_prepared->static_file.c_str(),m_prepared->statics,m_models.size(),m_prepared->collision_boxes,state.start.spawner,state.start.definition,state.start_script.c_str());
        std::printf("retail authored collision: %zu fixed triangles in shared level BVH\n",m_session->World().Resource<engine::gameplay::CollisionGeometry>().scene->TriangleCount());
        std::printf("retail actor animation: %zu immutable pose definitions bound by SoA components\n",m_pose_library->size());
        m_prepared.reset();m_loading.Complete();
    }
    bool LoadingScreenReady() const noexcept {return m_backdrop_ready;}
    bool DrawLoading(Graphics::CommandList& commands,Graphics::Renderer2D& ui,const engine::gui::text::FontFace& normal,
        const engine::gui::text::FontFace& large,unsigned width,unsigned height,std::uint32_t milliseconds) {
        if(!m_backdrop_ready) return true;
        m_animation_drawn=content::AdvanceLoadingAnimation(m_animation_drawn,m_loading.progress.Get()/100.f);
        Graphics::PropParameters lighting;lighting.scene_ambient={1,1,1,0};lighting.light_direction[0]={0,0,1,1};lighting.light_diffuse[0]={1,1,1,0};
        lighting.light_specular[0]={1,1,1,0};lighting.light_position[0]={0,0,15000,1};lighting.light_attenuation[0]={1,0,0,0};
        return m_backdrop.Draw(commands,width,height,m_animation_drawn,milliseconds,lighting) &&
            engine::gui::w3d::ModelBackdropView::DrawLabels(ui,m_backdrop_text,normal,large,640,480,width,height);
    }
    void Cancel() {if(m_loading.state.Get()==engine::gui::w3d::LoadingState::Loading) m_loading.cancel.Execute();}
    bool Ready() const noexcept {return m_session!=nullptr;}
    bool Failed() const noexcept {return !m_error.empty();}
    const std::string& Error() const noexcept {return m_error;}
    const engine::gui::w3d::LoadingModel& Loading() const noexcept {return m_loading;}
	void Step() {
        if(!m_session) return;m_session->Step();
        UpdateObjectives();
        const auto player=m_session->World().Resource<SceneState>().player;
        if(const auto* health=m_session->World().Get<engine::gameplay::Health>(player))
            if(const auto* shield=m_session->World().Get<engine::gameplay::Shield>(player))
                m_player_hud.Update(*health,*shield,Engine::Math::Fixed::FromRatio(1,Session::TicksPerSecond));
        const auto selected=m_session->World().Get<engine::gameplay::InventorySelection>(player)->selected;
        const auto* item=m_session->World().Get<engine::gameplay::InventoryItem>(selected);const auto* stock=m_session->World().Get<engine::gameplay::Magazine>(selected);
        const auto* equipped=item ? m_session->World().Resource<WeaponDefinitions>().catalog.Find(item->definition) : nullptr;
        m_player_hud.UpdateWeapon(equipped ? equipped->id : 0,equipped ? m_services.strings.Lookup(equipped->icon_text) : std::u16string{},stock,Engine::Math::Fixed::FromRatio(1,Session::TicksPerSecond));
        if(const auto* state=m_session->World().Get<HumanState>(player);state && state->ladder && state->velocity.z!=Engine::Math::Fixed{}) {
            ++m_ladder_motion_ticks;
            if(const auto* clip=m_session->World().Get<engine::gameplay::ClipPlayback>(player)) m_last_ladder_motion_clip=clip->clip;
        }
        if(!AdvanceProjectileEffects()) throw std::runtime_error("projectile emitter pose/timing failed");
        m_session->World().Resource<ConversationLineEvents>().ForEach([&](const auto& line) {
            if(line.finished) {++m_conversations_finished;std::printf("retail conversation ended: tick=%llu reason=%u\n",static_cast<unsigned long long>(m_session->Tick()),line.reason);}
            else {++m_conversation_lines;std::printf("retail conversation line: tick=%llu text=%u sound=%u\n",static_cast<unsigned long long>(m_session->Tick()),line.text,line.sound);}
        });
        using Engine::Math::Fixed;const auto now=Fixed::FromRatio(m_session->Tick(),Session::TicksPerSecond);
        std::vector<engine::gameplay::BehaviorEmission> requests;m_session->World().Resource<MissionRequests>().AppendTo(requests);
        std::ranges::sort(requests,{},[](const auto& request) {return std::pair{request.tick,request.order};});
        for(const auto& request:requests) {
            if(request.command.name!="Set_HUD_Help_Text") continue;
            const auto& arguments=request.command.arguments;
            if(arguments.size()!=4 || !std::ranges::all_of(arguments,[](const auto& argument) {return std::holds_alternative<std::string>(argument);}))
                throw std::invalid_argument("invalid HUD help request");
            std::array<Fixed,3> color;
            for(unsigned i=0;i<3;++i) {const auto value=Fixed::ParseDecimal(std::get<std::string>(arguments[i+1]));if(!value) throw std::invalid_argument("invalid HUD help color");color[i]=*value;}
            const auto& descriptor=std::get<std::string>(arguments[0]);auto text=m_services.strings.Lookup(descriptor);
            if(text.empty()) throw std::invalid_argument("missing HUD help translation: "+descriptor);
            m_help.Show(std::move(text),color,now);
            std::printf("retail HUD help: %s, tick=%llu, characters=%zu\n",descriptor.c_str(),static_cast<unsigned long long>(m_session->Tick()),m_help.text.Get().size());
        }
        std::vector<CameraLookRequest> camera_requests;m_session->World().Resource<CameraLookRequests>().AppendTo(camera_requests);
        std::ranges::sort(camera_requests,{},&CameraLookRequest::order);
        for(const auto& request:camera_requests) if(const auto point=ResolveCameraLook(m_session->World(),request)) m_camera_look_target=*point;
        m_help.Update(now);
    }
    const hud::HelpTextViewModel& HelpText() const noexcept {return m_help;}
    bool DrawHelp(Graphics::Renderer2D& renderer,const engine::gui::text::FontFace& font,unsigned width,unsigned height) {
        m_help_drawn=false;if(m_help.text.Get().empty() || CinematicActive()) return true;
        const auto size=engine::gui::w3d::Measure_Text(font,m_help.text.Get());if(!size) return false;
        const float x=(float(width)-(*size)[0])*0.5f,y=height*0.5f-(*size)[1]-height/30.f;
        const auto& color=m_help.color.Get();const auto alpha=Engine::Math::ToFloat(m_help.opacity.Get());
        m_help_drawn=engine::gui::w3d::Draw_Text(font,renderer,m_help.text.Get(),x,y,{0,0,0,alpha}) &&
            engine::gui::w3d::Draw_Text(font,renderer,m_help.text.Get(),x-1,y-1,{Engine::Math::ToFloat(color[0]),Engine::Math::ToFloat(color[1]),Engine::Math::ToFloat(color[2]),alpha});
        return m_help_drawn;
    }
    bool HelpTextDrawn() const noexcept {return m_help_drawn;}
    const hud::PlayerHudViewModel& PlayerHud() const noexcept {return m_player_hud;}
    bool PlayerHudDrawn() const noexcept {return m_player_hud_drawn;}
    bool DrawPlayerHud(Graphics::Renderer2D& renderer,const engine::gui::text::FontFace& health_font,
        const engine::gui::text::FontFace& shield_font,unsigned width,unsigned height) {
        m_player_hud_drawn=false;if(!m_session || CinematicActive()) return true;
        // Combat/hud.cpp's authored pixel atlas and lower-left anchoring.
        // GUI draw coordinates are a presentation boundary; ECS data and
        // MVVM fractions stay fixed-point and pause with the simulation.
        const auto image=[&](std::string_view name) {
            const auto found=m_objective_textures.find(name);return found==m_objective_textures.end() ? Graphics::Renderer2DTexture{} : engine::gui::images::Resolve_Texture(m_services.assets,found->second,renderer);
        };
        const auto atlas=image("HUD_MAIN.TGA");if(!atlas.index.Is_Valid()) return false;
        const auto color=[](Engine::Math::FixedVector3 value,float alpha=1) {return Graphics::Color2D{Engine::Math::ToFloat(value.x),Engine::Math::ToFloat(value.y),Engine::Math::ToFloat(value.z),alpha};};
        auto health_color=color(m_hud_settings.health_high);const auto fraction=m_player_hud.healthColorFraction.Get();
        if(fraction<=Engine::Math::Fixed::FromRatio(1,2)) health_color=color(m_hud_settings.health_medium);
        if(fraction<=Engine::Math::Fixed::FromRatio(1,4)) health_color=color(m_hud_settings.health_low);
        const auto quad=[&](Graphics::Rect2D uv,float x,float y,Graphics::Color2D tint=Graphics::Color2D{1,1,1,1},float crop=1) {
            const float w=(uv.right-uv.left)*crop,h=uv.bottom-uv.top;uv.right=uv.left+w;
            uv.left/=256;uv.top/=256;uv.right/=256;uv.bottom/=256;
            return renderer.Add_Quad(Graphics::Rect2D{x,y,x+w,y+h},uv,atlas,tint);
        };
        const float base_x=7,base_y=float(height)-179;
        struct Frame {Graphics::Rect2D uv;float x,y;};
        constexpr std::array<Frame,6> frames{{{{96,105,214,255},-3,-1},{{215,125,255,192},114,57},{{218,192,255,201},154,115},
            {{216,200,255,255},191,115},{{80,203,100,258},230,116},{{216,101,240,125},74,149}}};
        for(const auto& frame:frames) if(!quad(frame.uv,base_x+frame.x,base_y+frame.y)) return false;
        const auto gradient=[&](float x,float y,float alpha) {
            return renderer.Add_Quad(Graphics::Rect2D{x+77,y+124,x+163,y+150},{3.f/256,135.f/256,44.f/256,144.f/256},atlas,{1,1,1,alpha});
        };
        if(!renderer.Add_Quad(Graphics::Rect2D{base_x+98,base_y+122,base_x+224,base_y+168},{183.f/256,241.f/256,186.f/256,248.f/256},atlas,{1,1,1,1}) ||
            !quad({94,52,249,100},base_x+73,base_y+121,health_color,Engine::Math::ToFloat(m_player_hud.healthFraction.Get())) || !gradient(base_x,base_y,1)) return false;
        const auto cross=[&](float x,float y,float alpha) {
            const float flash=Engine::Math::ToFloat(m_player_hud.crossFlash.Get());auto a=health_color,b=a;a.alpha=flash*alpha;b.alpha=(1-flash)*alpha;
            // The alternate UV deliberately uses the first cross's draw size.
            return quad({33,199,63,226},x+77,y+124,a) && renderer.Add_Quad(Graphics::Rect2D{x+77,y+124,x+107,y+151},{33.f/256,228.f/256,63.f/256,258.f/256},atlas,b) &&
                engine::gui::w3d::Draw_Text(health_font,renderer,m_player_hud.healthText.Get(),x+111,y+128,{health_color.red,health_color.green,health_color.blue,alpha});
        };
        if(!cross(base_x,base_y,1)) return false;
        const float center_alpha=Engine::Math::ToFloat(m_player_hud.centerHealthAlpha.Get());
        if(center_alpha>0) {const float x=width*.25f-77,y=height*.5f-13.5f-124;if(!gradient(x,y,center_alpha) || !cross(x,y,center_alpha)) return false;}
        if(m_player_hud.shieldVisible.Get()) {
            const float shield=Engine::Math::ToFloat(m_player_hud.shieldFraction.Get());
            for(unsigned i=0;i<10 && i*.1f<shield;++i) if(!quad({66,97,96,132},base_x+211-float(int(i*.1f*80)),base_y+140)) return false;
            const float x=base_x+211-float(int(shield*80));
            if(!quad({66,97,96,132},x,base_y+140) || !engine::gui::w3d::Draw_Text(shield_font,renderer,m_player_hud.shieldText.Get(),x+4,base_y+144,{1,1,1,1})) return false;
        }
        const float weapon_x=float(width)-100,weapon_y=float(height)-110;
        if(!quad({0,0,95,95},weapon_x,weapon_y)) return false;
        if(m_player_hud.weaponDefinition.Get()) {
            if(!engine::gui::w3d::Draw_Text(health_font,renderer,m_player_hud.clipText.Get(),weapon_x+15,weapon_y+27,{1,1,1,1}) ||
                !engine::gui::w3d::Draw_Text(shield_font,renderer,m_player_hud.reserveText.Get(),weapon_x+65,weapon_y+34,{1,1,1,1})) return false;
            const auto& name=m_player_hud.weaponName.Get();const auto text=engine::gui::w3d::Measure_Text(shield_font,name);if(!text) return false;
            if(!engine::gui::w3d::Draw_Text(shield_font,renderer,name,float(width)-(*text)[0],float(height)-(*text)[1],{1,1,1,1})) return false;
            const auto& weapon=*m_session->World().Resource<WeaponDefinitions>().catalog.Find(m_player_hud.weaponDefinition.Get());
            if(!weapon.icon_texture.empty()) {
                const auto icon=image(WeaponIconName(weapon.icon_texture));const auto& uv=weapon.icon_uv;
                const auto handle=m_objective_textures.at(WeaponIconName(weapon.icon_texture));const auto* texture=m_services.assets.Try_Get_Texture(handle);
                const float scale=1.f/texture->Width(),x=weapon_x+Engine::Math::ToFloat(weapon.icon_offset[0]),y=weapon_y+Engine::Math::ToFloat(weapon.icon_offset[1]);
                if(!icon.index.Is_Valid() || !renderer.Add_Quad(Graphics::Rect2D{x,y,x+Engine::Math::ToFloat(uv[2]-uv[0]),y+Engine::Math::ToFloat(uv[3]-uv[1])},
                    {Engine::Math::ToFloat(uv[0])*scale,Engine::Math::ToFloat(uv[1])*scale,Engine::Math::ToFloat(uv[2])*scale,Engine::Math::ToFloat(uv[3])*scale},icon,{1,1,1,1})) return false;
            }
            const auto alpha=Engine::Math::ToFloat(m_player_hud.centerClipAlpha.Get());
            if(alpha>0) {const float x=width*.75f-20,y=height*.5f-30;
                if(!quad({2,211,13,255},x,y,{1,1,1,alpha}) || !engine::gui::w3d::Draw_Text(health_font,renderer,m_player_hud.clipText.Get(),x+15,y+15,{1,1,1,alpha})) return false;
            }
        }
        if(InputAllowed()) {
            const auto reticle=image("HD_reticle.tga");if(!reticle.index.Is_Valid() || !renderer.Add_Quad(Graphics::Rect2D{width*.45f,height*(.5f-32.f/480),width*.55f,height*(.5f+32.f/480)},{0,0,1,1},reticle,color(m_hud_settings.neutral))) return false;
        }
        m_player_hud_drawn=true;return true;
    }
    const hud::ObjectiveViewModel& Objectives() const noexcept {return m_objectives;}
    bool CycleObjective() {return m_objectives.cycle.Execute();}
    bool ObjectivesDrawn() const noexcept {return m_objectives_drawn;}
    bool DrawObjectives(Graphics::Renderer2D& renderer,const engine::gui::text::FontFace& font,unsigned width,unsigned height) {
        m_objectives_drawn=false;const auto* current=m_objectives.Current();if(!current || CinematicActive()) return true;
        const auto& vocabulary=m_session->World().Resource<ObjectiveVocabulary>();
        const auto texture=[&](std::string_view name) {
            const auto found=m_objective_textures.find(name);return found==m_objective_textures.end() ? Graphics::Renderer2DTexture{} : engine::gui::images::Resolve_Texture(m_services.assets,found->second,renderer);
        };
        for(const auto& icon:m_objectives.Icons(width,height)) {
            const auto image=texture(vocabulary.tokens.at(icon.token));
            if(!image.index.Is_Valid() || !renderer.Add_Quad(Graphics::Rect2D{Engine::Math::ToFloat(icon.left),Engine::Math::ToFloat(icon.top),Engine::Math::ToFloat(icon.right),Engine::Math::ToFloat(icon.bottom)},{0,0,1,1},image,{1,1,1,1})) return false;
            if(icon.fly>Engine::Math::Fixed{}) {
                const auto fly=Engine::Math::ToFloat(icon.fly);
                const auto star=texture("HUD_STAR.TGA");const float x=40+(width*.425f-40)*fly,y=height*.8f+(height*.5875f-height*.8f)*fly,size=32*fly;
                if(!star.index.Is_Valid() || !renderer.Add_Quad(Graphics::Rect2D{x-size,y-size,x+size,y+size},{0,0,1,1},star,{0,1,0,1})) return false;
            }
        }
        const auto* player=m_session->World().Get<engine::gameplay::Transform>(m_session->World().Resource<SceneState>().player);
        const auto delta=current->position-player->position;const auto angle=Engine::Math::ToFloat(Engine::Math::Radians(Engine::Math::Atan2(delta.y,delta.x)-player->facing));
        const auto a=std::sin(angle+std::numbers::pi_v<float>*1.25f),b=std::cos(angle+std::numbers::pi_v<float>*1.25f);
        std::array<Graphics::Point2D,4> vertices{{{a,b},{b,-a},{-b,a},{-a,-b}}};
        const float x=width-48+35*std::cos(-angle-std::numbers::pi_v<float>*.5f),y=40+35*std::sin(-angle-std::numbers::pi_v<float>*.5f);
        for(auto& vertex:vertices) {vertex.x=vertex.x*(8/.7071067811865475244f)+x;vertex.y=vertex.y*(8/.7071067811865475244f)+y;}
        const auto arrow=texture("HUD_obje_arrow.TGA");if(!arrow.index.Is_Valid() || !renderer.Add_Quad(vertices,Graphics::Rect2D{0,0,1,1},arrow,{1,1,1,1})) return false;
        auto range=m_services.strings.Lookup(12639u);const auto placeholder=range.find(u"%d");if(placeholder==std::u16string::npos) return false;
        range.replace(placeholder,2,Wide(std::to_string(hud::ObjectiveViewModel::Range(*current,player->position))));
        const auto label=m_services.strings.Lookup(current->value.hud_message);
        for(const auto& line:std::array{std::pair{label,57.f},std::pair{range,72.f}}) {
            const auto measured=engine::gui::w3d::Measure_Text(font,line.first);if(!measured) return false;
            if(!engine::gui::w3d::Draw_Text(font,renderer,line.first,width-48-(*measured)[0]*.5f,line.second,{1,1,1,1})) return false;
        }
        m_objectives_drawn=true;return true;
    }
    bool InputAllowed() const {
        if(!m_session || CinematicActive()) return false;
        const auto player=m_session->World().Resource<SceneState>().player;
        const auto* permission=m_session->World().Get<engine::gameplay::InputPermission>(player);
        return !permission || permission->enabled;
    }
    void QueueBehaviorMessage(engine::gameplay::BehaviorMessage message) {
        if(!m_session) throw std::logic_error("mission event before level publication");
        m_session->World().Resource<engine::gameplay::BehaviorInbox>().messages.push_back(message);
    }
    std::size_t InitializedBehaviors(ecs::Entity subject,std::string_view name) const {
        if(!m_session) return 0;
        const auto& programs=m_session->World().Resource<engine::gameplay::BehaviorPrograms>().programs;
        ecs::Query<ecs::Read<engine::gameplay::LuaBehavior>> query(m_session->World());std::size_t count{};
        query.ForEachChunk([&](auto chunk) {
            for(const auto& binding:chunk.template Get<engine::gameplay::LuaBehavior>())
                if(binding.subject==subject && binding.initialized && binding.definition<programs.size() && programs[binding.definition].name==name) ++count;
        });return count;
    }
    void Advance(std::chrono::nanoseconds elapsed,const LevelInput& input) {
        if(!m_session || m_paused) return;
        if(!input.sample || !input.orientation || !input.set_orientation) throw std::invalid_argument("missing injected level input port");
        m_accumulator.AddElapsed(std::max(elapsed,std::chrono::nanoseconds{}));
        // Bound catch-up work without dropping ticks. Loading time never
        // enters this clock because it starts at scene publication.
        for(unsigned count=0;count<8 && m_accumulator.StepDue();++count) {
            const auto player=m_session->World().Resource<SceneState>().player;
            auto control=input.sample();if(CinematicActive()) {control.enabled=0;control.forward={};control.left={};control.jump=0;}
            *m_session->World().Get<HumanControl>(player)=control;
            if(input.weapon) {
                auto weapon=input.weapon();const auto at=m_camera.Get_Position(),forward=m_camera.Get_Forward_Dir();
                const auto fixed=[](float value) {return Engine::Math::Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(value));};
                weapon.input.origin={fixed(at.x),fixed(at.y),fixed(at.z)};
                weapon.input.target=weapon.input.origin+Engine::Math::FixedVector3{fixed(forward.x),fixed(forward.y),fixed(forward.z)}*Engine::Math::Fixed::FromInt(1000);
                weapon.input.permitted=weapon.input.permitted && InputAllowed() && !m_ladder_view;
                *m_session->World().Get<WeaponInput>(player)=weapon.input;*m_session->World().Get<engine::gameplay::InventoryControl>(player)=weapon.inventory;
            }
            Step();m_accumulator.ConsumeStep();
            const auto& human=*m_session->World().Get<HumanState>(player);
            const bool ladder_view=human.transition ? human.transition>=3 : human.ladder!=0;
            if(human.transition_sequence!=m_ladder_transition_sequence || ladder_view!=m_ladder_view) {
                // transition.cpp Start requests 1s; CombatManager's mode
                // change overrides it with .5s when entering/leaving ladders.
                m_transition_profile=m_camera_profile;
                m_anchor_transition.Begin(m_camera_anchor,m_camera_heading,ladder_view!=m_ladder_view ? .5f : 1.f);
                m_ladder_transition_sequence=human.transition_sequence;
                if(ladder_view!=m_ladder_view) {
                    m_ladder_view=ladder_view;m_profile=m_ladder_view || !m_first_person ? m_third_profile : m_first_profile;
                    ecs::CommandBuffer commands;
                    if(m_first_person && !m_ladder_view) commands.Add<engine::gameplay::DrawHidden>(player);
                    else commands.Remove<engine::gameplay::DrawHidden>(player);
                    m_session->World().Commit(commands);
                }
            }
            if(m_ladder_view) {auto angles=input.orientation();angles.heading=m_session->World().Get<engine::gameplay::Transform>(player)->facing;input.set_orientation(angles);}
            if(m_camera_look_target) {
                using namespace Engine::Math;const auto at=m_camera.Get_Position();
                const auto fixed=[](float value) {return Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(value));};
                const FixedVector3 origin{fixed(at.x),fixed(at.y),fixed(at.z)};
                if(auto angles=CameraLookOrientation(origin,*m_camera_look_target,m_profile.view_tilt_degrees)) {
                    input.set_orientation(*angles);m_last_camera_look_target=*m_camera_look_target;m_last_camera_look_tick=m_session->Tick();++m_camera_looks;
                    UpdateCamera(input.orientation());
                    std::printf("retail script camera look: tick=%llu target.raw=(%lld,%lld,%lld) heading=%u pitch.raw=%lld\n",static_cast<unsigned long long>(m_session->Tick()),static_cast<long long>(m_camera_look_target->x.Raw()),static_cast<long long>(m_camera_look_target->y.Raw()),static_cast<long long>(m_camera_look_target->z.Raw()),angles->heading.units,static_cast<long long>(angles->pitch.Raw()));
                }
                m_camera_look_target.reset();
            }
        }
        UpdateCamera(input.orientation());
        m_anchor_transition.Advance(float(std::chrono::duration<double>(std::max(elapsed,std::chrono::nanoseconds{})).count()));
        const auto at=m_camera.Get_Position();const auto pose=m_camera.Get_Transform();
        m_audio->Update(m_session->World().Resource<engine::gameplay::SoundEmitterSnapshots>(),m_sound_catalog,
            {{at.x,at.y,at.z},{pose.matrix[0],pose.matrix[4],pose.matrix[8]}});
    }
    Session& Simulation() {return *m_session;}
	const Session &Simulation() const { return *m_session; }
    void ToggleFirstPerson() {
        if(!m_session) return;
        m_first_person=!m_first_person;m_profile=m_ladder_view || !m_first_person ? m_third_profile : m_first_profile;
        const auto player=m_session->World().Resource<SceneState>().player;ecs::CommandBuffer commands;
        if(m_first_person && !m_ladder_view) commands.Add<engine::gameplay::DrawHidden>(player);
        else commands.Remove<engine::gameplay::DrawHidden>(player);
        m_session->World().Commit(commands);
    }
    bool FirstPerson() const noexcept {return m_first_person;}
    bool LadderView() const noexcept {return m_ladder_view;}
    float CameraTransitionWeight() const noexcept {return m_anchor_transition.PreviousWeight();}
    std::optional<std::array<float,3>> PlayerBonePosition(unsigned bone) const {
        if(!m_session) return {};
        const auto player=m_session->World().Resource<SceneState>().player;
        const auto* binding=m_session->World().Get<engine::gameplay::PoseBinding>(player);
        const auto* clip=m_session->World().Get<engine::gameplay::ClipPlayback>(player);
        if(!binding || !clip || !clip->clip) return {};
        auto pose=m_pose_library->at(binding->definition-1);Graphics::RenderTransform socket;
        if(!pose.Evaluate(clip->clip-1,Engine::Math::ToFloat(clip->frame),clip->mode==engine::gameplay::ClipMode::Loop) || !pose.Bone_Transform(bone,socket)) return {};
        const auto& fixed=m_session->World().Get<engine::gameplay::AffinePose>(player)->transform;auto world=Graphics::Affine_Identity();
        std::ranges::transform(fixed.elements,world.matrix.begin(),[](auto value){return Engine::Math::ToFloat(value);});
        const auto rendered=Graphics::Multiply_Affine(world,socket);return std::array{rendered.matrix[3],rendered.matrix[7],rendered.matrix[11]};
    }
    void SetPaused(bool paused) {m_paused=paused;if(m_audio) m_audio->SetPaused(paused);if(m_cinematic_music) m_services.audio.Pause(m_cinematic_music,paused);}
    bool CinematicActive() const {return m_session && m_cinematic_clock && m_session->World().Get<CinematicDirector>(*m_cinematic_clock)->camera>=0;}
    Engine::Math::Fixed CinematicTime() const {return m_cinematic_clock ? m_session->World().Get<engine::gameplay::TimelinePlayback>(*m_cinematic_clock)->elapsed : Engine::Math::Fixed{};}
    bool CinematicFinished() const {return m_cinematic_clock && m_session->World().Get<engine::gameplay::TimelinePlayback>(*m_cinematic_clock)->complete;}
    unsigned CinematicCommandsSeen() const {return m_cinematic_clock ? m_session->World().Get<CinematicDirector>(*m_cinematic_clock)->commands_seen : 0;}
    std::size_t CinematicActors() const noexcept {return m_cinematic_slots.size();}
    std::array<float,3> CameraPosition() const {const auto position=m_camera.Get_Position();return {position.x,position.y,position.z};}
    bool DrawCinematicOverlay(Graphics::Renderer2D& ui,unsigned width,unsigned height) const {
        if(!m_cinematic_clock) return true;
        const auto& state=*m_session->World().Get<CinematicDirector>(*m_cinematic_clock);const auto now=CinematicTime();
        const auto opacity=Engine::Math::ToFloat(engine::time::Sample(state.fade,now));
        if(opacity>0 && !ui.Add_Rect({0,0,float(width),float(height)},
            {Engine::Math::ToFloat(state.fade_color[0]),Engine::Math::ToFloat(state.fade_color[1]),Engine::Math::ToFloat(state.fade_color[2]),opacity})) return false;
        // ScreenFadeManager::LETTERBOX_SIZE: each bar is 1/8 screen height.
        const auto size=height*.125f*Engine::Math::ToFloat(engine::time::Sample(state.letterbox,now));
        return size<=0 || (ui.Add_Rect({0,0,float(width),size},{0,0,0,1}) && ui.Add_Rect({0,height-size,float(width),float(height)},{0,0,0,1}));
    }
    std::size_t BodyPartsDrawn() const noexcept {return m_body_parts;}
    std::size_t LadderMotionTicks() const noexcept {return m_ladder_motion_ticks;}
    std::uint32_t LastLadderMotionClip() const noexcept {return m_last_ladder_motion_clip;}
    float CameraForwardZ() const noexcept {return -m_camera.Get_Transform().matrix[10];}
    std::array<float,3> CameraForward() const {const auto forward=m_camera.Get_Forward_Dir();return {forward.x,forward.y,forward.z};}
    std::size_t ScriptCameraLooks() const noexcept {return m_camera_looks;}
    Engine::Math::FixedVector3 LastCameraLookTarget() const noexcept {return m_last_camera_look_target;}
    std::uint64_t LastCameraLookTick() const noexcept {return m_last_camera_look_tick;}
    std::size_t PlayingSounds() const noexcept {return m_audio ? m_audio->Playing() : 0;}
    std::size_t ConversationLines() const noexcept {return m_conversation_lines;}
    std::size_t ConversationsFinished() const noexcept {return m_conversations_finished;}
    std::size_t PreparedConversationLines(std::string_view name={}) const {
        if(!m_session) return 0;std::size_t count{};
        for(const auto& definition:m_session->World().Resource<ConversationLibrary>().definitions)
            if(name.empty() || definition.name==name) count+=definition.lines.size();
        return count;
    }
    std::size_t ObjectsDrawn() const noexcept {return m_objects_drawn;}
    std::size_t ObjectsCulled() const noexcept {return m_objects_culled;}
    std::size_t ParticleCount() const noexcept {return m_particle_count;}
    std::size_t ProjectileParticleCount() const noexcept {return m_projectile_particle_count;}
    std::size_t ProjectilePartsDrawn() const noexcept {return m_projectile_parts;}
    std::uint64_t PendingTicks() const noexcept {return m_accumulator.PendingSteps();}
	bool Draw(Graphics::CommandList &commands, unsigned width, unsigned height, std::uint32_t milliseconds, LevelDrawLayers layers={})
	{
		if (!m_session || !width || !height) return false;
        m_body_parts=0;m_objects_drawn=0;m_objects_culled=0;m_projectile_parts=0;
		m_camera.Set_Aspect_Ratio_Value(float(width) / height);
		Graphics::PropParameters parameters;
		parameters.view = m_camera.Get_View_Matrix().values;
		const auto projection = m_camera.Get_Backend_Projection_Matrix().values;
		for (unsigned r = 0; r < 4; ++r) for (unsigned c = 0; c < 4; ++c) for (unsigned k = 0; k < 4; ++k)
			parameters.view_projection[r * 4 + c] += projection[r * 4 + k] * parameters.view[k * 4 + c];
		parameters.scene_ambient = m_ambient;
		const auto position = m_camera.Get_Position(); parameters.camera_position = {position.x, position.y, position.z, 1};
        if(!m_sky->Draw(commands,parameters)) return false;
		const Graphics::PropTextureMappingContext mapping{milliseconds, projection};
		struct Part {const ModelView *view; std::size_t part; Graphics::PropParameters parameters;const Graphics::ModelAssetPose* pose;};
		std::vector<Part> parts; std::vector<std::int32_t> levels;
        std::deque<Graphics::ModelAssetPose> evaluated_poses;
        const auto particle_time=std::uint32_t(m_session->Tick()*1000/Session::TicksPerSecond);m_particle_count=0;m_projectile_particle_count=0;
        std::vector<engine::gameplay::ProjectileVisual> projectile_states;
        m_session->World().Resource<engine::gameplay::ProjectileVisuals>().AppendTo(projectile_states);
        for(auto& [key,particle]:m_particles) particle.used=false;
        struct WheelControls {ecs::Entity parent;std::vector<std::pair<std::size_t,Graphics::RenderTransform>> bones;};
        std::vector<WheelControls> wheel_controls;
        m_session->World().Resource<engine::gameplay::SuspensionSnapshots>().ForEach([&](const auto& wheel) {
            auto found=std::ranges::find(wheel_controls,wheel.parent,&WheelControls::parent);
            if(found==wheel_controls.end()) {wheel_controls.push_back({wheel.parent,{}});found=std::prev(wheel_controls.end());}
            auto& controls=found->bones;
            auto suspension=Graphics::Affine_Identity();suspension.matrix[11]=Engine::Math::ToFloat(wheel.displacement);controls.emplace_back(wheel.position_bone,suspension);
            if(wheel.rotation_bone!=~0u) {auto rotation=Graphics::Affine_Identity();const auto angle=Engine::Math::ToFloat(wheel.rotation);rotation.matrix[0]=rotation.matrix[5]=std::cos(angle);rotation.matrix[1]=-std::sin(angle);rotation.matrix[4]=std::sin(angle);controls.emplace_back(wheel.rotation_bone,rotation);}
        });
        bool animation_failed=false;
        m_session->World().Resource<engine::gameplay::AffineVisibleObjects>().ForEach([&](const auto &object) {
            if(!layers.player_body && object.entity==m_session->World().Resource<SceneState>().player) return;
            if(m_session->World().Get<engine::gameplay::DrawHidden>(object.entity)) return;
			const auto &view = *m_models.at(object.model); auto state = parameters;
            std::ranges::transform(object.transform.elements,state.world.begin(),[](auto value) {return Engine::Math::ToFloat(value);});
            state.world[12]=state.world[13]=state.world[14]=0;state.world[15]=1;
            const auto* pose_binding=m_session->World().Get<engine::gameplay::PoseBinding>(object.entity);
            if(view.cull_static && !pose_binding) {
                Graphics::RenderTransform world;world.matrix=state.world;
                if(m_camera.Cull_Box(Graphics::Transform_Camera_Box(view.bounds,world))) {++m_objects_culled;return;}
            }
            ++m_objects_drawn;
            const Graphics::ModelAssetPose* pose=view.pose.Bone_Count() ? &view.pose : nullptr;
            const auto key=(std::uint64_t(object.entity.generation)<<32)|object.entity.index;
            if(pose_binding) {
                auto& evaluated=evaluated_poses.emplace_back(m_pose_library->at(pose_binding->definition-1));
                const auto* playback=m_session->World().Get<engine::gameplay::ClipPlayback>(object.entity);
                if(!playback) {animation_failed=true;return;}
                const auto* transition=m_session->World().Get<engine::gameplay::ClipTransition>(object.entity);
                const bool blended=transition && transition->duration>Engine::Math::Fixed{} && transition->elapsed<transition->duration && transition->previous.clip;
                const auto override=layers.mechanism_frame && m_session->World().Get<engine::gameplay::KinematicCollider>(object.entity) ? layers.mechanism_frame : std::nullopt;
                const bool ready=override && playback->clip ? evaluated.Evaluate(playback->clip-1,Engine::Math::ToFloat(*override)) : !playback->clip ? evaluated.Rest() : blended ? evaluated.Evaluate_Blended(transition->previous.clip-1,Engine::Math::ToFloat(transition->previous.frame),playback->clip-1,Engine::Math::ToFloat(playback->frame),
                    Engine::Math::ToFloat(transition->elapsed/transition->duration),transition->previous.mode==engine::gameplay::ClipMode::Loop,playback->mode==engine::gameplay::ClipMode::Loop) :
                    evaluated.Evaluate(playback->clip-1,Engine::Math::ToFloat(playback->frame),playback->mode==engine::gameplay::ClipMode::Loop);
                if(!ready) {animation_failed=true;return;}
                if(layers.speech_animation) if(const auto* speech=m_session->World().Get<SpeechAnimation>(object.entity);speech && speech->playback.clip)
                    if(!evaluated.Evaluate_Layer(speech->playback.clip-1,Engine::Math::ToFloat(speech->playback.frame),speech->first_bone,speech->bone_count)) {animation_failed=true;return;}
                if(const auto controls=std::ranges::find(wheel_controls,object.entity,&WheelControls::parent);controls!=wheel_controls.end() && !evaluated.Evaluate_Controlled(&evaluated,controls->bones)) {animation_failed=true;return;}
                pose=&evaluated;
            }
            const auto* fired=m_session->World().Get<engine::gameplay::FireSequence>(object.entity);const bool flash=fired && fired->fired;
            const auto projectile_state=std::ranges::find(projectile_states,object.entity,&engine::gameplay::ProjectileVisual::entity);const bool projectile=projectile_state!=projectile_states.end();
			for (std::size_t i = 0; i < view.binding.Part_Count(); ++i) if ((!projectile || layers.projectile_models && !projectile_state->expired) && (view.visible[i] || flash && i<view.muzzle_parts.size() && view.muzzle_parts[i])) {
                if(object.entity==m_session->World().Resource<SceneState>().player) ++m_body_parts;
                if(projectile) ++m_projectile_parts;
				parts.push_back({&view, i, state,pose}); levels.push_back(view.binding.Part_Sort_Level(i));
			}
            for(std::size_t index=0;index<view.emitters.size();++index) {
                const auto& emitter=view.emitters[index];if(emitter.lod!=view.lod) continue;auto world=Graphics::RenderTransform{state.world};bool active=true;
                if(emitter.bone!=~0u) {if(!pose) {animation_failed=true;return;}Graphics::RenderTransform socket;if(!pose->Bone_Transform(emitter.bone,socket)) {animation_failed=true;return;}world=Graphics::Multiply_Affine(world,socket);active=pose->Visible(emitter.bone);}
                auto [found,inserted]=m_particles.try_emplace({key,index});auto& instance=found->second;
                if(inserted) {instance.view=std::make_unique<Graphics::EmitterView>(emitter.description,key+index);instance.binding=emitter.binding;instance.projectile=projectile;}
                instance.world=world;instance.used=true;
                // Projectile emission already advanced for every fixed tick,
                // including births and impacts between two rendered frames.
                if(!projectile && !instance.view->Advance(particle_time,world,active)) {animation_failed=true;return;}
            }
            if(const auto* attachment=m_session->World().Get<engine::gameplay::AttachedModel>(object.entity);attachment && pose) {
                Graphics::RenderTransform bone;if(!pose->Bone_Transform(attachment->bone,bone)) {animation_failed=true;return;}
                const auto& gun=*m_models.at(attachment->model);auto attached=state;attached.world=Graphics::Multiply_Affine(Graphics::RenderTransform{state.world},bone).matrix;
                for(std::size_t i=0;i<gun.binding.Part_Count();++i) if(gun.visible[i] || flash && i<gun.muzzle_parts.size() && gun.muzzle_parts[i]) {parts.push_back({&gun,i,attached,gun.pose.Bone_Count() ? &gun.pose : nullptr});levels.push_back(gun.binding.Part_Sort_Level(i));}
            }
		});
        if(animation_failed) return false;
		auto &submission = m_services.submission; submission.Clear();
		const auto &camera = parameters.view; const std::array<float, 4> depth{-camera[8], -camera[9], -camera[10], -camera[11]};
		for (const auto &part : parts) if (!part.view->binding.Part_Sort_Level(part.part)) {
			if (part.view->binding.Part_Requires_Transparency_Sorting(part.part)) {
                if (!part.view->binding.Submit_Part(submission, part.part, part.parameters, Graphics::PropDrawPhase::Transparent, depth, part.pose, part.view->lod, &mapping)) return false;
            } else if (!part.view->binding.Draw_Part(commands, part.part, part.parameters, part.pose, part.view->lod, {}, nullptr, &mapping)) return false;
		}
		std::vector<std::size_t> ordered; if (!Graphics::Build_Static_Draw_Order(levels, ordered)) return false;
		for (const auto index : ordered) { const auto &part = parts[index];
            if (!part.view->binding.Draw_Part(commands, part.part, part.parameters, part.pose, part.view->lod, {}, nullptr, &mapping)) return false;
		}
        Graphics::EmitterDrawInput particle_input;particle_input.projection.values=projection;particle_input.view.values=parameters.view;particle_input.sync_time=milliseconds;
        for(auto it=m_particles.begin();it!=m_particles.end();) {
            auto& instance=it->second;if(!instance.used && !instance.view->Advance(particle_time,instance.world,false)) return false;
            m_particle_count+=instance.view->Count();if(instance.projectile) m_projectile_particle_count+=instance.view->Count();
            if((!instance.projectile || layers.projectile_emitters) && !instance.view->Submit(m_services.device,m_services.renderer,submission,particle_input,instance.binding->Texture())) return false;
            if(!instance.used && !instance.view->Count()) it=m_particles.erase(it);else ++it;
        }
		return submission.Flush_Transparent();
	}
private:
    struct ModelEmitterView {Assets::EmitterAssetDesc description;std::shared_ptr<Graphics::EmitterAssetBinding> binding;std::uint32_t bone{},lod{};};
	struct ModelView {Graphics::PropAssetBinding binding; Graphics::ModelAssetPose pose; std::vector<bool> visible,muzzle_parts; std::uint32_t lod{};Graphics::CameraAxisAlignedBox bounds;bool cull_static{};std::vector<ModelEmitterView> emitters;};
    struct PreparedModel {std::uint32_t id{};std::string name;std::unique_ptr<ModelView> view;Graphics::PreparedPropAsset gpu;bool drawable{};std::vector<Graphics::PreparedModelEmitter> emitters;std::size_t emitter_cursor{};};
    struct ParticleInstance {std::unique_ptr<Graphics::EmitterView> view;std::shared_ptr<Graphics::EmitterAssetBinding> binding;Graphics::RenderTransform world;bool used{},projectile{};};
    bool AdvanceProjectileEffects() {
        const auto time=std::uint32_t(m_session->Tick()*1000/Session::TicksPerSecond);
        for(auto& [key,particle]:m_particles) if(particle.projectile) particle.used=false;
        bool valid=true;
        m_session->World().Resource<engine::gameplay::ProjectileVisuals>().ForEach([&](const auto& projectile) {
            const auto found_model=m_models.find(projectile.model);if(found_model==m_models.end()) {valid=false;return;}
            const auto& model=*found_model->second;
            const auto transform=[](const auto& fixed) {auto result=Graphics::Affine_Identity();for(unsigned index=0;index<12;++index) result.matrix[index]=Engine::Math::ToFloat(fixed.elements[index]);return result;};
            const auto initial=transform(projectile.birth_pose),current=transform(projectile.pose);
            const auto key=(std::uint64_t(projectile.entity.generation)<<32)|projectile.entity.index;
            for(std::size_t index=0;index<model.emitters.size();++index) {
                const auto& emitter=model.emitters[index];if(emitter.lod!=model.lod) continue;
                auto birth=initial,world=current;bool active=true;
                if(emitter.bone!=~0u) {
                    Graphics::RenderTransform socket;if(!model.pose.Bone_Transform(emitter.bone,socket)) {valid=false;return;}
                    birth=Graphics::Multiply_Affine(birth,socket);world=Graphics::Multiply_Affine(world,socket);active=model.pose.Visible(emitter.bone);
                }
                auto [found,inserted]=m_particles.try_emplace({key,index});auto& instance=found->second;
                if(inserted) {
                    instance.view=std::make_unique<Graphics::EmitterView>(emitter.description,key+index);instance.binding=emitter.binding;instance.projectile=true;
                    if(!instance.view->Advance(std::uint32_t(projectile.birth_tick*1000/Session::TicksPerSecond),birth,active)) {valid=false;return;}
                }
                instance.world=world;instance.used=true;
                if(!instance.view->Advance(time,world,active) || projectile.expired && !instance.view->Advance(time,world,false)) {valid=false;return;}
            }
        });
        for(auto& [key,particle]:m_particles) if(particle.projectile && !particle.used && !particle.view->Advance(time,particle.world,false)) valid=false;
        return valid;
    }
    struct PreparedScene {
        std::unique_ptr<Session> session;
        std::vector<PreparedModel> models;
        Graphics::CameraState camera;
        Graphics::DomeGeometry sky;
        engine::audio::PreparedSoundLibrary sound_library;
        std::vector<engine::audio::SoundEventDefinition> sounds;
        content::CameraProfile profile;
        content::CameraProfile third_profile;
        std::vector<Graphics::ModelAssetPose> pose_library;
        std::map<std::string,Assets::TextureAssetHandle,std::less<>> objective_textures;
        content::HudSettings hud_settings;
        std::optional<ecs::Entity> cinematic_clock;std::map<std::int32_t,ecs::Entity> cinematic_slots;
        std::shared_ptr<const engine::audio::PcmBuffer> cinematic_music;std::uint32_t music_fade_ms{};
        std::array<float,4> ambient{0.5f,0.5f,0.5f,0};
        std::string static_file;
        std::size_t statics{},collision_boxes{};
    };
    struct PreparedBackdrop {
        content::LoadingScreen layout;
        Graphics::PreparedPropAsset gpu;
        Assets::ModelRigDesc rig;
        std::vector<engine::gui::w3d::BackdropText> labels;
    };
    struct LoadState {
        std::atomic<bool> cancelled{};
        std::atomic<unsigned> progress{};
        std::atomic<std::shared_ptr<PreparedBackdrop>> backdrop;
        // Only published on the owning thread by ResourceLoadJob::Complete.
        bool complete{};
        std::unique_ptr<PreparedScene> prepared;
        std::string error;
    };
    static std::u16string Wide(std::string_view value) {return {value.begin(),value.end()};}
    static std::string WeaponIconName(std::string_view value) {const auto slash=value.find_last_of("/\\");return std::string(slash==std::string_view::npos ? value : value.substr(slash+1));}
    void RequestCancel() {
        if(m_load) m_load->cancelled.store(true,std::memory_order_relaxed);
        if(m_source) m_services.loader.Cancel(m_source);
    }
    struct LoadJob final:Graphics::ResourceLoadJob {
        std::shared_ptr<LoadState> state;
        Assets::AssetCache& assets;
        const engine::filesystem::VirtualFileSystem& files;
        const content::StringCatalog& strings;
        std::string map;
        int difficulty{};
        std::uint32_t sample_rate{};
        bool startup_cinematics{};
        LoadJob(std::shared_ptr<LoadState> value,Assets::AssetCache& cache,const engine::filesystem::VirtualFileSystem& source,const content::StringCatalog& catalog,std::string name,int level_difficulty,std::uint32_t rate,bool startup)
            :state(std::move(value)),assets(cache),files(source),strings(catalog),map(std::move(name)),difficulty(level_difficulty),sample_rate(rate),startup_cinematics(startup) {}
        bool Prepare() override {return !state->cancelled.load(std::memory_order_relaxed);}
        void Cancel() noexcept override {state->cancelled.store(true,std::memory_order_relaxed);}
        bool Decode() override {
            try {
                auto loaded=PrepareScene(assets,files,strings,map,difficulty,sample_rate,startup_cinematics,*state);
                if(!loaded) {state->error=std::move(loaded.error());return false;}
                state->prepared=std::move(*loaded);return true;
            } catch(const std::exception& exception) {state->error=exception.what();return false;}
        }
        void Complete(bool success) noexcept override {
            if(!success || state->cancelled.load(std::memory_order_relaxed)) state->prepared.reset();
            state->complete=true;
        }
    };
    static std::expected<std::unique_ptr<PreparedScene>,std::string> PrepareScene(Assets::AssetCache& assets,
        const engine::filesystem::VirtualFileSystem& files,const content::StringCatalog& strings,std::string_view map,int difficulty,std::uint32_t sample_rate,bool startup_cinematics,LoadState& progress) {
        const auto cancelled=[&] {return progress.cancelled.load(std::memory_order_relaxed);};
        if(cancelled()) return std::unexpected("loading cancelled");
        auto backdrop=std::make_shared<PreparedBackdrop>();
        auto layout=content::LoadLoadingScreen(files,map);if(!layout) return std::unexpected(layout.error());backdrop->layout=std::move(*layout);
        auto backdrop_file=std::filesystem::path(backdrop->layout.model);backdrop_file.replace_extension(".w3d");
        backdrop->gpu.model=assets.Request_Model(backdrop_file.generic_string());assets.Wait(backdrop->gpu.model);
        if(cancelled()) return std::unexpected("loading cancelled");
        const auto* model=assets.Try_Get_Model(backdrop->gpu.model);if(!model) return std::unexpected(assets.Get_Error(backdrop->gpu.model));
        backdrop->rig=model->Rig();std::string backdrop_error;
        const auto animation=assets.Load_Rig(Assets::AssetType::Animation,backdrop->layout.model+"."+backdrop->layout.model,backdrop_error);
        if(!animation || animation->animations.empty()) return std::unexpected("loading backdrop animation missing: "+backdrop_error);
        backdrop->rig.animations=animation->animations;
        auto backdrop_textures=std::make_shared<Graphics::PreparedPropTextures>();
        if(!Graphics::Build_Prop_Asset_Geometry(*model,backdrop->gpu.geometry,backdrop_error) ||
            !Graphics::Prepare_Prop_Textures(assets,backdrop->gpu.model,*backdrop_textures,backdrop_error)) return std::unexpected(backdrop_error);
        backdrop->gpu.textures=std::move(backdrop_textures);
        for(const auto& line:backdrop->layout.texts) {
            auto value=line.literal ? Wide(line.text) : strings.Lookup(line.text);
            if(value.empty()) return std::unexpected("loading text translation missing: "+line.text);
            backdrop->labels.push_back({std::move(value),line.x,line.y,line.wrap,line.color,line.large});
        }
        progress.backdrop.store(std::move(backdrop),std::memory_order_release);
        const auto configuration=content::LoadCampaignContent(files);
        if(!configuration) return std::unexpected(configuration.error());
        const auto level=content::LoadLevelScene(files,map,configuration->definitions);
        if(!level) return std::unexpected(level.error());
        const auto background=content::ReadBackground(level->static_data);if(!background) return std::unexpected(background.error());
        if(cancelled()) return std::unexpected("loading cancelled");
        auto result=std::make_unique<PreparedScene>();result->session=std::make_unique<Session>(4);
        result->hud_settings=configuration->hud;
        for(const auto& [id,entry]:strings.entries) result->session->World().Resource<ObjectiveVocabulary>().translations.emplace(entry.descriptor,id);
        const auto colors=content::BackgroundColors(*background);
        // Original HazeClass's angular bands and 16 azimuth segments; the
        // extra polar cap carries the original background clear color.
        const std::array<Graphics::DomeBand,5> bands{{{0,colors.sky},{1.22f,colors.sky},{1.55f,colors.horizon},{1.57f,colors.horizon},{1.92f,colors.horizon}}};
        auto dome=Graphics::Build_Dome(bands,16,100);if(!dome) return std::unexpected(dome.error());result->sky=std::move(*dome);
        std::printf("retail background: %02u:%02u, cover %.3f, gloom %.3f, typed sky/haze\n",background->hours,background->minutes,Engine::Math::ToFloat(background->cloud_cover),Engine::Math::ToFloat(background->gloominess));
        const auto loaded=result->session->LoadScene(*level,configuration->definitions,configuration->armor,difficulty);
        if(!loaded) return std::unexpected(loaded.error());
        const auto startup=content::LoadCinematicStartup(files,level->level,1);
        if(!startup) return std::unexpected(startup.error());
        std::vector<engine::level::BehaviorBinding> timeline_scripts;
        if(*startup && startup_cinematics) {
            const auto text=files.ReadText((**startup).control_file);if(!text) return std::unexpected("missing cinematic control file");
            const auto definition=content::ReadCinematic(*text);if(!definition) return std::unexpected(definition.error());
            for(unsigned index=0;index<definition->actions.size();++index) {
                const auto& action=definition->actions[index];
                if(action.opcode==content::CinematicOpcode::Script && Assets::Canonicalize_Asset_Name(action.asset)!="m00_cinematic_attack_command_dls")
                    timeline_scripts.push_back({index,std::uint64_t(action.slot),action.asset,action.parameters});
            }
        }
        const auto behaviors=content::LoadBehaviorPrograms(files,level->level,0,level->start_script,timeline_scripts);
        if(!behaviors) return std::unexpected(behaviors.error());
        const auto bound=result->session->InstallBehaviors(*behaviors);
        if(!bound) return std::unexpected(bound.error());
        for(const auto& name:behaviors->objective_textures) {
            const auto handle=assets.Request_Texture(name);assets.Wait(handle);
            const auto* image=assets.Try_Get_Texture(handle);if(!image || !image->Has_Pixels()) return std::unexpected("objective texture: "+name+": "+assets.Get_Error(handle));
            result->objective_textures.emplace(name,handle);
        }
        for(const auto name:{"HUD_MAIN.TGA","HD_reticle.tga","HD_reticle_hit.tga"}) {
            const auto handle=assets.Request_Texture(name);assets.Wait(handle);
            const auto* image=assets.Try_Get_Texture(handle);if(!image || !image->Has_Pixels()) return std::unexpected(std::string("player HUD texture: ")+name+": "+assets.Get_Error(handle));
            result->objective_textures.emplace(name,handle);
        }
        // Prepared off the frame thread. The immutable catalog also covers
        // later Lua grants without synchronous I/O on selection.
        for(const auto& [id,weapon]:configuration->weapons.weapons) if(!weapon.icon_texture.empty()) {
            const auto name=WeaponIconName(weapon.icon_texture);if(result->objective_textures.contains(name)) continue;
            const auto handle=assets.Request_Texture(name);assets.Wait(handle);const auto* image=assets.Try_Get_Texture(handle);
            if(!image || !image->Has_Pixels()) return std::unexpected("weapon HUD icon: "+name+": "+assets.Get_Error(handle));result->objective_textures.emplace(name,handle);
        }
        std::printf("retail mission callbacks: %zu Lua bindings, %zu bindings still awaiting script ports\n",behaviors->bindings.size(),behaviors->unported.size());
        auto& state=result->session->World().Resource<SceneState>();
        const auto sounds=content::ReadLevelSounds(level->static_data);if(!sounds) return std::unexpected(sounds.error());
        ecs::CommandBuffer emitters;
        for(const auto& sound:*sounds) {
            if(cancelled()) return std::unexpected("loading cancelled");
            auto& buffers=result->sound_library.buffers;
            if(!buffers.contains(sound.filename)) {
                const auto path=content::ResolvePresetModel(files,sound.filename);if(!path) return std::unexpected(path.error());
                auto bytes=files.Read(*path);if(!bytes) return std::unexpected("missing map sound "+*path);
                auto pcm=engine::audio::DecodeAll(std::move(*bytes),sample_rate);if(!pcm) return std::unexpected("map audio decode failed: "+*path);
                buffers.emplace(sound.filename,std::make_shared<const engine::audio::PcmBuffer>(std::move(*pcm)));
            }
            engine::audio::SoundEventDefinition definition;definition.name="map-sound-"+std::to_string(sound.id);definition.sounds={sound.filename};
            constexpr std::array buses{engine::audio::Bus::Music,engine::audio::Bus::Ambient,engine::audio::Bus::Speech,engine::audio::Bus::Cinematic};definition.bus=buses.at(sound.type);
            definition.type=sound.positional ? engine::audio::sound_type::World : engine::audio::sound_type::Interface;
            definition.volume=Engine::Math::ToFloat(sound.volume);definition.pitchMin=definition.pitchMax=Engine::Math::ToFloat(sound.pitch);
            definition.minRange=Engine::Math::ToFloat(sound.minimum_range);definition.maxRange=Engine::Math::ToFloat(sound.maximum_range);
            if(sound.loops!=1) {definition.control=engine::audio::sound_control::Loop;definition.loopCount=sound.loops;}
            result->sounds.push_back(std::move(definition));const auto entity=emitters.Create();
            emitters.Add<engine::gameplay::SoundEmitter>(entity,engine::gameplay::SoundEmitter{static_cast<std::uint32_t>(result->sounds.size()),sound.state==1 ? 1u : 0u});
            emitters.Add<engine::gameplay::Transform>(entity,engine::gameplay::Transform{sound.position,{}});
        }
        result->session->World().Commit(emitters);
        const auto conversations=content::ReadLevelConversations(level->dynamic_data);
        if(!conversations) return std::unexpected(conversations.error());
        auto& conversation_library=result->session->World().Resource<ConversationLibrary>();
        auto& timelines=result->session->World().Resource<engine::gameplay::TimelineLibrary>();
        std::map<std::uint32_t,std::pair<std::uint32_t,Engine::Math::Fixed>> speech_clips;
        std::vector<SpeechAssetLine> speech_lines;
        for(const auto& name:behaviors->conversations) {
            const auto* source=conversations->Find(name);
            if(!source) {
                if(std::ranges::find(behaviors->optional_conversations,name)!=behaviors->optional_conversations.end()) {std::printf("retail conversation unavailable: name=%s; original optional script cue retained without a substitute\n",name.c_str());continue;}
                return std::unexpected("missing authored conversation "+name);
            }
            ConversationPlaybackDefinition playback;playback.name=name;engine::level::Timeline timeline;Engine::Math::Fixed time;
            for(const auto& remark:source->remarks) {
                if(cancelled()) return std::unexpected("loading cancelled");
                if(remark.orator>1) return std::unexpected("conversation requires additional participant bindings: "+name);
                const auto text=strings.entries.find(remark.text);if(text==strings.entries.end()) return std::unexpected("conversation text is missing: "+std::to_string(remark.text));
                std::uint32_t clip{};auto duration=Engine::Math::Fixed::FromInt(2);
                if(text->second.sound && text->second.sound!=0xffffffffu) {
                    const auto found=speech_clips.find(text->second.sound);
                    if(found!=speech_clips.end()) {clip=found->second.first;duration=found->second.second;}
                    else {
                        const auto* definition=configuration->definitions.Find(text->second.sound);if(!definition) return std::unexpected("conversation sound definition is missing");
                        const auto sound=content::ReadSoundPreset(*definition);if(!sound) return std::unexpected(sound.error());
                        auto& buffers=result->sound_library.buffers;
                        if(!buffers.contains(sound->filename)) {
                            const auto path=content::ResolvePresetModel(files,sound->filename);if(!path) return std::unexpected(path.error());
                            auto bytes=files.Read(*path);if(!bytes) return std::unexpected("missing conversation sound "+*path);
                            auto pcm=engine::audio::DecodeAll(std::move(*bytes),sample_rate);if(!pcm) return std::unexpected("conversation sound decode failed: "+*path);
                            buffers.emplace(sound->filename,std::make_shared<const engine::audio::PcmBuffer>(std::move(*pcm)));
                        }
                        const auto& pcm=*buffers.at(sound->filename);duration=Engine::Math::Fixed::FromRatio(pcm.Frames(),pcm.sampleRate);
                        engine::audio::SoundEventDefinition event;event.name="dialogue-"+std::to_string(sound->id);event.sounds={sound->filename};
                        constexpr std::array buses{engine::audio::Bus::Music,engine::audio::Bus::Effects,engine::audio::Bus::Speech,engine::audio::Bus::Cinematic};event.bus=buses.at(sound->type);
                        event.type=sound->positional ? engine::audio::sound_type::World : engine::audio::sound_type::Interface;
                        event.volume=Engine::Math::ToFloat(sound->volume);event.pitchMin=event.pitchMax=Engine::Math::ToFloat(sound->pitch);
                        event.minRange=Engine::Math::ToFloat(sound->minimum_range);event.maxRange=Engine::Math::ToFloat(sound->maximum_range);
                        result->sounds.push_back(std::move(event));clip=std::uint32_t(result->sounds.size());speech_clips.emplace(sound->id,std::pair{clip,duration});
                        std::printf("retail dialogue prepared: %s, sound=%u, frames=%zu, bus=%u\n",sound->filename.c_str(),sound->id,pcm.Frames(),unsigned(sound->type));
                    }
                }
                speech_lines.push_back({std::uint32_t(speech_lines.size()+1),remark.text,text->second.animation,text->second.english,duration});
                timeline.cues.push_back({time,std::uint32_t(playback.lines.size())});playback.lines.push_back({remark.orator,clip,remark.text,speech_lines.back().id});time+=duration;
                std::printf("retail conversation remark prepared: name=%s index=%zu text=%u source_sound=%u clip=%u duration.raw=%lld\n",name.c_str(),playback.lines.size()-1,remark.text,text->second.sound,clip,static_cast<long long>(duration.Raw()));
            }
            timeline.cues.push_back({time,std::uint32_t(playback.lines.size())});timelines.timelines.push_back(std::move(timeline));playback.timeline=std::uint32_t(timelines.timelines.size());
            conversation_library.definitions.push_back(std::move(playback));
            std::printf("retail conversation prepared: %s, lines=%zu duration.raw=%lld\n",name.c_str(),source->remarks.size(),static_cast<long long>(time.Raw()));
        }
        std::printf("retail map audio: %zu authored emitters, %zu decoded clips prepared off the frame thread\n",sounds->size(),result->sound_library.buffers.size());
        progress.progress.store(10,std::memory_order_relaxed);
        std::map<std::string,std::string> resolved;
        for(const auto& placement:level->statics) {
            if(cancelled()) return std::unexpected("loading cancelled");
            const auto source=content::ResolveRenderAsset(files,placement,configuration->definitions);
            if(!source) return std::unexpected(source.error());
            if(!source->empty()) resolved.emplace(placement.model,*source);
        }
        for(const auto& placement:level->dynamic.physics) {
            if(std::ranges::find(state.models,placement.model)==state.models.end()) continue;
            const auto source=content::ResolveRenderAsset(files,placement,configuration->definitions);
            if(!source) return std::unexpected(source.error());
            if(!source->empty()) resolved.emplace(placement.model,*source);
        }
        std::map<std::string,std::vector<std::byte>> containers;
        std::map<std::uint32_t,engine::level::ModelCollision3> collision_models;
        auto textures=std::make_shared<Graphics::PreparedPropTextures>();
        for(std::size_t i=1;i<state.models.size();++i) {
            if(cancelled()) return std::unexpected("loading cancelled");
            const auto source=resolved.find(state.models[i]);
            if(source==resolved.end()) return std::unexpected("unresolved render model "+state.models[i]);
            const auto& request=source->second;const auto file=request.substr(0,request.find("::"));
            if(!containers.contains(file)) {
                auto bytes=files.Read(file);if(!bytes) return std::unexpected("missing W3D container "+file);
                containers.emplace(file,std::move(*bytes));
            }
            const auto box=content::IsCollisionBox(containers.at(file),state.models[i]);
            if(!box) return std::unexpected(box.error());
            PreparedModel item;item.id=static_cast<std::uint32_t>(i);item.name=state.models[i];item.view=std::make_unique<ModelView>();
            if(*box) ++result->collision_boxes;
            else {
                item.gpu.model=assets.Request_Model(request);
                // Wait belongs exclusively to the decoder worker. Cancellation
                // returns the menu immediately even if the cache is still busy.
                assets.Wait(item.gpu.model);
                if(cancelled()) return std::unexpected("loading cancelled");
                const auto* model=assets.Try_Get_Model(item.gpu.model);
                if(!model) return std::unexpected(item.name+": "+assets.Get_Error(item.gpu.model));
                std::string emitter_error;if(!Graphics::Prepare_Model_Emitters(assets,item.gpu.model,item.emitters,emitter_error)) return std::unexpected(emitter_error);
                const auto& bounds=model->Bounds();
                item.view->bounds={{(bounds.minimum.x+bounds.maximum.x)*.5f,(bounds.minimum.y+bounds.maximum.y)*.5f,(bounds.minimum.z+bounds.maximum.z)*.5f},
                    {(bounds.maximum.x-bounds.minimum.x)*.5f,(bounds.maximum.y-bounds.minimum.y)*.5f,(bounds.maximum.z-bounds.minimum.z)*.5f}};
                // Animated hierarchies need pose-dependent bounds; this slice
                // culls only flat static models with authored finite bounds.
                item.view->cull_static=model->Rig().skeleton_name.empty();
                auto collision=engine::level::PrepareModelCollision(*model);
                if(!collision) return std::unexpected(item.name+": "+collision.error());
                collision_models.emplace(item.id,std::move(*collision));
                for(const auto& attachment:model->Rig().attachments) item.view->lod=std::max(item.view->lod,attachment.lod);
                std::string error;
                if(!model->Rig().skeleton_name.empty() && !item.view->pose.Initialize(model->Rig(),error)) return std::unexpected(item.name+": "+error);
                for(const auto& part:model->Submeshes()) item.view->visible.push_back((part.source_attributes&Assets::W3D::W3DMeshAttributeHidden)==0);
                item.drawable=std::ranges::any_of(item.view->visible,[](bool visible) {return visible;});
                if(item.drawable) {
                    if(!Graphics::Build_Prop_Asset_Geometry(*model,item.gpu.geometry,error) || !Graphics::Prepare_Prop_Textures(assets,item.gpu.model,*textures,error))
                        return std::unexpected(item.name+": "+error);
                    item.gpu.textures=textures;
                }
            }
            result->models.push_back(std::move(item));
            progress.progress.store(10+static_cast<unsigned>(60*i/std::max<std::size_t>(1,state.models.size()-1)),std::memory_order_relaxed);
        }
        std::vector<std::pair<std::string,Assets::ModelAnimationDesc>> script_clips;
        for(const auto& name:behaviors->animations) {
            if(cancelled()) return std::unexpected("loading cancelled");
            std::string error;const auto source=assets.Load_Rig(Assets::AssetType::Animation,name,error);
            if(!source || source->animations.size()!=1) return std::unexpected("mission animation "+name+": "+error);
            script_clips.emplace_back(name,source->animations.front());
        }
        ecs::CommandBuffer animations;
        auto& collision_library=result->session->World().Resource<engine::gameplay::KinematicCollisionLibrary>();
        auto& animation_library=result->session->World().Resource<MissionAnimationLibrary>();
        for(const auto& actor:level->dynamic.actors) {
            if(actor.pending_delete) continue;
            const auto authored=content::ReadActorAnimation(actor);if(!authored) return std::unexpected(authored.error());
            const auto entity=state.authored_entities.at(actor.LevelId());const auto* model_id=result->session->World().Get<engine::gameplay::ModelOverride>(entity);if(!model_id) continue;
            const auto prepared=std::ranges::find(result->models,model_id->model,&PreparedModel::id);
            if(prepared==result->models.end()) {if(*authored) return std::unexpected("animated actor model is missing");continue;}
            const auto* model=assets.Try_Get_Model(prepared->gpu.model);if(!model) continue;
            std::vector<Assets::ModelAnimationDesc> clips;std::vector<std::pair<std::string,std::uint32_t>> names;
            engine::gameplay::ClipPlayback playback;
            std::string error;
            if(*authored) {
                if(model->Rig().skeleton_name.size()<3) return std::unexpected("animated actor hierarchy is missing");
                const auto& definition=**authored;auto name=definition.clip;
                if(name.empty()) name=content::StandingAnimation(static_cast<char>(std::toupper(static_cast<unsigned char>(model->Rig().skeleton_name[2]))),definition.hold_style);
                auto source=assets.Load_Rig(Assets::AssetType::Animation,name,error);
                if(!source && definition.clip.empty() && model->Rig().skeleton_name[2]!='A' && model->Rig().skeleton_name[2]!='a') {
                    name=content::StandingAnimation('A',definition.hold_style);source=assets.Load_Rig(Assets::AssetType::Animation,name,error);
                }
                if(!source || source->animations.size()!=1) return std::unexpected("actor animation "+name+": "+error);
                clips.push_back(source->animations.front());playback.frame=definition.frame;playback.target=definition.target;
                constexpr std::array modes{engine::gameplay::ClipMode::Once,engine::gameplay::ClipMode::Loop,engine::gameplay::ClipMode::Hold,engine::gameplay::ClipMode::Target};playback.mode=modes.at(definition.mode);
            }
            for(const auto& [name,clip]:script_clips) {
                if(Assets::Canonicalize_Asset_Name(clip.skeleton_name)!=Assets::Canonicalize_Asset_Name(model->Rig().skeleton_name)) continue;
                clips.push_back(clip);names.emplace_back(name,std::uint32_t(clips.size()));
            }
            if(clips.empty()) continue;
            playback.clip=1;playback.frame_count=clips.front().frame_count;
            playback.frames_per_second=Engine::Math::Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(clips.front().frame_rate));
            playback.loop_period=Engine::Math::Fixed::FromInt(playback.frame_count>1 ? playback.frame_count-1 : 1);
            if(*authored && result->session->World().Get<HumanGoto>(entity)) {
                const auto boxes=engine::level::PrepareModelBoxBounds(*model);if(!boxes) return std::unexpected(boxes.error());
                std::optional<engine::gameplay::SweptHull> hull;
                for(const auto& box:*boxes) if(Assets::Canonicalize_Asset_Name(box.name).find("worldbox")!=std::string::npos) {
                    if(hull) return std::unexpected("soldier model contains ambiguous WORLDBOX proxies");
                    engine::gameplay::SweptHull value;value.offset=box.bounds.center;value.extent=box.bounds.extent;
                    value.categories=1;value.maximum_contacts=4;value.clip_bias=Engine::Math::Fixed::FromRatio(101,100);
                    value.support_normal_z=Engine::Math::Cos(Engine::Math::TurnFromRadians(result->session->World().Get<HumanMovement>(entity)->definition.slide_angle));
                    value.support_clearance=Engine::Math::Fixed::FromRatio(1,50);value.blocking_clearance=Engine::Math::Fixed::Half();hull=value;
                }
                if(!hull) return std::unexpected("retail soldier WORLDBOX proxy is missing");
                animations.Add<engine::gameplay::SweptHull>(entity,*hull);animations.Add<engine::gameplay::SweepContacts>(entity);
                const auto locomotion=content::HumanAnimations(static_cast<char>(std::toupper(static_cast<unsigned char>(model->Rig().skeleton_name[2]))),(**authored).hold_style);
                std::array<engine::gameplay::ClipPlayback,HumanLocomotionClips> bank;
                for(std::size_t i=0;i<locomotion.size();++i) {
                    if(cancelled()) return std::unexpected("loading cancelled");
                    auto source=assets.Load_Rig(Assets::AssetType::Animation,locomotion[i],error);
                    if(!source && model->Rig().skeleton_name[2]!='A' && model->Rig().skeleton_name[2]!='a') {
                        const auto fallback=content::HumanAnimations('A',(**authored).hold_style);
                        source=assets.Load_Rig(Assets::AssetType::Animation,fallback[i],error);
                    }
                    if(!source || source->animations.size()!=1) return std::unexpected("soldier locomotion "+locomotion[i]+": "+error);
                    clips.push_back(source->animations.front());const auto& clip=clips.back();auto& entry=bank[i];entry.clip=std::uint32_t(clips.size());entry.frame_count=clip.frame_count;
                    entry.frames_per_second=Engine::Math::Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(clip.frame_rate));
                    entry.mode=i>=14 && i<=18 || i>=24 ? engine::gameplay::ClipMode::Once : engine::gameplay::ClipMode::Loop;
                    entry.loop_period=Engine::Math::Fixed::FromInt(clip.frame_count>1 ? clip.frame_count-1 : 1);
                }
                auto& catalog=result->session->World().Resource<HumanAnimationCatalog>();catalog.banks.push_back(bank);
                animations.Add<HumanAnimation>(entity,HumanAnimation{std::uint32_t(catalog.banks.size()),1,0,result->session->World().Get<engine::gameplay::Transform>(entity)->facing.units});
                animations.Add<engine::gameplay::ClipTransition>(entity);
            }
            auto face=PrepareSpeech(assets,model->Rig(),speech_lines,clips);if(!face) return std::unexpected(face.error());
            if(*face) {
                auto& banks=result->session->World().Resource<SpeechAnimations>().banks;banks.push_back(std::move((**face).bank));(**face).state.bank=std::uint32_t(banks.size());
                animations.Add<SpeechAnimation>(entity,(**face).state);
            }
            Graphics::ModelAssetPose pose;if(!pose.Initialize(model->Rig(),std::span<const Assets::ModelAnimationDesc>(clips),error)) return std::unexpected("actor animation bank: "+error);
            for(const auto& [name,index]:names) {
                engine::gameplay::ClipPlayback prototype;prototype.clip=index;prototype.frame_count=clips[index-1].frame_count;
                prototype.frames_per_second=Engine::Math::Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(clips[index-1].frame_rate));
                prototype.loop_period=Engine::Math::Fixed::FromInt(prototype.frame_count>1 ? prototype.frame_count-1 : 1);
                animation_library.bindings.push_back({entity,name,prototype});
            }
            if(!names.empty() && !actor.smart && actor.physics_token) {
                engine::gameplay::KinematicCollisionDefinition collision;collision.rest=collision_models.at(model_id->model);
                std::vector<Engine::Math::FixedAffineTransform3> bones(model->Rig().bones.size());
                for(std::size_t index=0;index<clips.size();++index) {
                    // Vertex samples exactly reproduce linearly translated
                    // mechanisms. Rotating collision needs a bone-space track
                    // before those mission dependencies can be advertised.
                    for(const auto& channel:clips[index].channels) if(channel.component==Assets::ModelChannelComponent::Rotation &&
                        std::ranges::any_of(channel.samples,[&](const auto& sample) {return sample!=channel.samples.front();}))
                        return std::unexpected("rotating mission collision track is not implemented: "+clips[index].name);
                    engine::level::ModelCollisionTrack3 track;
                    for(std::uint32_t frame=0;frame<clips[index].frame_count;++frame) {
                        if(cancelled()) return std::unexpected("loading cancelled");
                        if(!pose.Evaluate(index,float(frame))) return std::unexpected("mission collision pose evaluation failed");
                        for(std::size_t bone=0;bone<bones.size();++bone) {
                            Graphics::RenderTransform transform;if(!pose.Bone_Transform(bone,transform)) return std::unexpected("mission collision bone is missing");
                            for(std::size_t element=0;element<12;++element) bones[bone].elements[element]=Engine::Math::Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(transform.matrix[element]));
                        }
                        auto geometry=engine::level::PrepareModelCollision(*model,{},bones);if(!geometry) return std::unexpected(geometry.error());
                        track.frames.push_back(std::move(*geometry));
                    }collision.clips.push_back(std::move(track));
                }
                const auto definition=std::uint32_t(collision_library.models.size());collision_library.models.push_back(std::move(collision));
                const auto* identity=result->session->World().Get<PhysicsIdentity>(entity);
                animations.Add<engine::gameplay::KinematicCollider>(entity,engine::gameplay::KinematicCollider{actor.LevelId(),definition,0xffffffffu,identity && (identity->flags&0xf0000000u) ? 0u : 1u});
                std::printf("retail mission mechanism prepared: subject=%llu, model=%s, clips=%zu, triangles=%zu\n",static_cast<unsigned long long>(actor.LevelId()),model->Rig().skeleton_name.c_str(),clips.size(),collision_library.models.back().rest.first.size());
            }
            result->pose_library.push_back(std::move(pose));animations.Add<engine::gameplay::PoseBinding>(entity,engine::gameplay::PoseBinding{std::uint32_t(result->pose_library.size())});
            animations.Add<engine::gameplay::ClipPlayback>(entity,playback);
        }
        result->session->World().Commit(animations);
        auto collision_scene=std::make_shared<engine::level::CollisionScene3>();
        for(const auto entity:state.statics) {
            if(cancelled()) return std::unexpected("loading cancelled");
            const auto* identity=result->session->World().Get<PhysicsIdentity>(entity);
            // PhysClass's ignore counter belongs to game composition; the
            // shared level adapter only sees geometry and instance transforms.
            if(identity && (identity->flags&0xf0000000u)) continue;
            const auto* model=result->session->World().Get<engine::gameplay::ModelOverride>(entity);
            const auto* pose=result->session->World().Get<engine::gameplay::AffinePose>(entity);
            const auto* id=result->session->World().Get<engine::gameplay::LevelIdentity>(entity);
            if(!model || !pose || !id) return std::unexpected("static collision placement is incomplete");
            if(const auto found=collision_models.find(model->model);found!=collision_models.end())
                engine::level::AppendModelCollision(*collision_scene,found->second,pose->transform,id->value);
        }
        collision_scene->Prepare();result->session->World().Resource<engine::gameplay::CollisionGeometry>().scene=collision_scene;
        if(!collision_library.models.empty()) collision_library.static_scene=std::move(collision_scene);
        // Use the shared asset/cache and level adapters for the authored
        // player proxy; its dimensions are not guessed by the controller.
        const auto player_source=content::ResolvePresetModel(files,state.start.model);
        if(!player_source) return std::unexpected(player_source.error());
        const auto player_handle=assets.Request_Model(*player_source);assets.Wait(player_handle);
        if(cancelled()) return std::unexpected("loading cancelled");
        const auto* player_model=assets.Try_Get_Model(player_handle);
        if(!player_model) return std::unexpected("player collision model: "+assets.Get_Error(player_handle));
        const auto boxes=engine::level::PrepareModelBoxBounds(*player_model);
        if(!boxes) return std::unexpected(boxes.error());
        std::optional<engine::gameplay::SweptHull> player_hull;
        for(const auto& box:*boxes) if(Assets::Canonicalize_Asset_Name(box.name).find("worldbox")!=std::string::npos) {
            if(player_hull) return std::unexpected("player model contains ambiguous WORLDBOX proxies");
            engine::gameplay::SweptHull hull;hull.offset=box.bounds.center;hull.extent=box.bounds.extent;
            hull.categories=1;hull.maximum_contacts=4;hull.clip_bias=Engine::Math::Fixed::FromRatio(101,100);
            hull.support_normal_z=Engine::Math::Cos(Engine::Math::TurnFromRadians(result->session->World().Get<HumanMovement>(state.player)->definition.slide_angle));
            hull.support_clearance=Engine::Math::Fixed::FromRatio(1,50);hull.blocking_clearance=Engine::Math::Fixed::Half();player_hull=hull;
        }
        if(!player_hull) return std::unexpected("retail player WORLDBOX proxy is missing");
        PreparedModel player_view;player_view.id=static_cast<std::uint32_t>(state.models.size());player_view.name=state.start.model;player_view.view=std::make_unique<ModelView>();player_view.gpu.model=player_handle;
        state.models.push_back(state.start.model);
        for(const auto& attachment:player_model->Rig().attachments) player_view.view->lod=std::max(player_view.view->lod,attachment.lod);
        for(const auto& part:player_model->Submeshes()) player_view.view->visible.push_back((part.source_attributes&Assets::W3D::W3DMeshAttributeHidden)==0);
        std::string player_error;
        if(!player_view.view->pose.Initialize(player_model->Rig(),player_error) || !Graphics::Build_Prop_Asset_Geometry(*player_model,player_view.gpu.geometry,player_error) ||
            !Graphics::Prepare_Prop_Textures(assets,player_handle,*textures,player_error)) return std::unexpected("player body: "+player_error);
        player_view.drawable=true;player_view.gpu.textures=textures;
        const auto locomotion=content::HumanAnimations(static_cast<char>(std::toupper(static_cast<unsigned char>(player_model->Rig().skeleton_name.at(2)))),7);
        std::vector<Assets::ModelAnimationDesc> player_clips;std::array<engine::gameplay::ClipPlayback,HumanLocomotionClips> player_bank;
        for(std::size_t i=0;i<locomotion.size();++i) {
            if(cancelled()) return std::unexpected("loading cancelled");
            const auto source=assets.Load_Rig(Assets::AssetType::Animation,locomotion[i],player_error);
            if(!source || source->animations.size()!=1) return std::unexpected("player locomotion "+locomotion[i]+": "+player_error);
            const auto& clip=source->animations.front();auto& playback=player_bank[i];playback.clip=static_cast<std::uint32_t>(i+1);playback.frame_count=clip.frame_count;
            playback.frames_per_second=Engine::Math::Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(clip.frame_rate));playback.mode=i>=14 && i<=18 ? engine::gameplay::ClipMode::Once : engine::gameplay::ClipMode::Loop;
                if(i>=24) playback.mode=engine::gameplay::ClipMode::Once;
                playback.loop_period=Engine::Math::Fixed::FromInt(clip.frame_count>1 ? clip.frame_count-1 : 1);player_clips.push_back(clip);
        }
        result->session->World().Resource<HumanAnimationCatalog>().banks.push_back(player_bank);
        Graphics::ModelAssetPose player_pose;if(!player_pose.Initialize(player_model->Rig(),std::span<const Assets::ModelAnimationDesc>(player_clips),player_error)) return std::unexpected(player_error);
        result->pose_library.push_back(std::move(player_pose));
        const auto player_clip=player_bank[0];
        ecs::CommandBuffer bodies;bodies.Add<engine::gameplay::SweptHull>(state.player,*player_hull);bodies.Add<engine::gameplay::SweepContacts>(state.player);
        bodies.Add<engine::gameplay::PoseBinding>(state.player,engine::gameplay::PoseBinding{std::uint32_t(result->pose_library.size())});
        bodies.Add<engine::gameplay::ModelOverride>(state.player,engine::gameplay::ModelOverride{player_view.id});bodies.Add<engine::gameplay::DrawHidden>(state.player);bodies.Add<engine::gameplay::ClipPlayback>(state.player,player_clip);
        bodies.Add<engine::gameplay::ClipTransition>(state.player);bodies.Add<HumanAnimation>(state.player,HumanAnimation{std::uint32_t(result->session->World().Resource<HumanAnimationCatalog>().banks.size()),1,0,result->session->World().Get<engine::gameplay::Transform>(state.player)->facing.units});
        result->session->World().Commit(bodies);result->models.push_back(std::move(player_view));
        const auto* first_person=configuration->cameras.Find("first_person");
        if(!first_person) return std::unexpected("retail First_Person camera profile is missing");
        result->profile=*first_person;
        result->third_profile=*configuration->cameras.Find("default");
        // combat.cpp COMBAT_MODE_FIRST_PERSON; soldier.cpp Get_Weapon_Height.
        result->profile.distance={};result->profile.height=Engine::Math::Fixed::FromRatio(81,50);
        const auto& profile=result->profile;
        const float height=Engine::Math::ToFloat(profile.height),fov=Engine::Math::ToFloat(profile.fov_degrees);
        const float tilt=Engine::Math::ToFloat(profile.view_tilt_degrees),distance=Engine::Math::ToFloat(profile.distance);
        std::array<float,12> start;
        std::ranges::transform(state.start.transform.elements,start.begin(),[](auto value) {return Engine::Math::ToFloat(value);});
        const float radians=tilt*std::numbers::pi_v<float>/180.f;
        Engine::Math::Vector3 forward{start[0]*std::cos(radians),start[4]*std::cos(radians),-std::sin(radians)};
        Engine::Math::Vector3 position{start[3]-forward.x*distance,start[7]-forward.y*distance,start[11]+height-forward.z*distance};
        const auto transform=Engine::Math::AffineTransform3::Look_Along(position,forward);
        Graphics::RenderTransform camera;
        std::copy(transform.elements.begin(),transform.elements.end(),camera.matrix.begin());
        camera.matrix[12]=camera.matrix[13]=camera.matrix[14]=0;camera.matrix[15]=1;
        result->camera.Set_Transform(camera);result->camera.Set_View_Plane(fov*std::numbers::pi_v<float>/180.f);result->camera.Set_Clip_Planes(0.26f,300.f);
        const auto physics=content::persist::One(level->static_data,0x20000);
        if(physics) if(const auto scene=content::persist::One(physics->payload,0x04433220))
            if(const auto vars=content::persist::One(scene->payload,0x4820)) if(const auto fields=content::persist::Micros(vars->payload))
                for(const auto& field:*fields) if(field.id==0 && field.payload.size()==12)
                    for(unsigned channel=0;channel<3;++channel) if(const auto value=content::persist::Scalar(field.payload.subspan(channel*4,4))) result->ambient[channel]=Engine::Math::ToFloat(*value);
        if(*startup && startup_cinematics) {
            auto cinematic=PrepareCinematic(assets,files,configuration->definitions,**startup,sample_rate,cancelled);
            if(!cinematic) return std::unexpected(cinematic.error());
            auto& world=result->session->World();world.Resource<engine::gameplay::TimelineLibrary>().timelines.push_back(cinematic->library.definition.timeline);
            ecs::CommandBuffer director;const auto pending=director.Create();engine::gameplay::TimelinePlayback clock;clock.timeline=std::uint32_t(world.Resource<engine::gameplay::TimelineLibrary>().timelines.size());
            director.Add<engine::gameplay::TimelinePlayback>(pending,clock);director.Add<CinematicDirector>(pending);world.Commit(director);
            result->cinematic_clock=director.Resolve(pending);
            ecs::CommandBuffer actors;std::vector<std::pair<std::int32_t,ecs::DeferredEntity>> pending_actors;
            auto& tracks=world.Resource<engine::gameplay::PoseTrackLibrary>();
            for(auto& source:cinematic->actors) {
                const auto entity=actors.Create();pending_actors.emplace_back(source.slot,entity);
                const auto model_id=std::uint32_t(state.models.size());state.models.push_back(source.name);
                PreparedModel model;model.id=model_id;model.name=source.name;model.gpu=std::move(source.gpu);model.view=std::make_unique<ModelView>();
                const auto* asset=assets.Try_Get_Model(model.gpu.model);std::string error;
                const auto existing=source.pose.Animations();std::vector<Assets::ModelAnimationDesc> clips(existing.begin(),existing.end());
                auto face=PrepareSpeech(assets,asset->Rig(),speech_lines,clips);if(!face) return std::unexpected(face.error());
                if(*face) {
                    auto& banks=world.Resource<SpeechAnimations>().banks;banks.push_back(std::move((**face).bank));(**face).state.bank=std::uint32_t(banks.size());
                    actors.Add<SpeechAnimation>(entity,(**face).state);
                    if(!source.pose.Initialize(asset->Rig(),std::span<const Assets::ModelAnimationDesc>(clips),error)) return std::unexpected("cinematic speech pose: "+error);
                    std::printf("retail cinematic face prepared: slot=%d bones=%u..%u clips=%zu\n",source.slot,(**face).state.first_bone,(**face).state.first_bone+(**face).state.bone_count,banks.back().size());
                }
                if(!Graphics::Prepare_Model_Emitters(assets,model.gpu.model,model.emitters,error)) return std::unexpected(error);
                if(!asset->Rig().bones.empty() && !model.view->pose.Initialize(asset->Rig(),error)) return std::unexpected(error);
                for(const auto& attachment:asset->Rig().attachments) model.view->lod=std::max(model.view->lod,attachment.lod);
                // PhysicalGameObj::Hide_Muzzle_Flashes hides these source
                // subobjects until the weapon reports a shot; rest rigs expose them.
                for(const auto& part:asset->Submeshes()) {
                    const auto name=Assets::Canonicalize_Asset_Name(part.name);
                    const bool muzzle=name.find("muzzleflash")!=std::string::npos || name.find("mz")!=std::string::npos;model.view->muzzle_parts.push_back(muzzle);
                    model.view->visible.push_back((part.source_attributes&Assets::W3D::W3DMeshAttributeHidden)==0 && !muzzle);
                }
                model.drawable=std::ranges::any_of(model.view->visible,[](bool visible) {return visible;});result->models.push_back(std::move(model));
                actors.Add<CinematicActor>(entity,CinematicActor{*result->cinematic_clock,source.slot,0});actors.Add<engine::gameplay::DrawHidden>(entity);
                actors.Add<engine::gameplay::ModelOverride>(entity,engine::gameplay::ModelOverride{model_id});actors.Add<engine::gameplay::AffinePose>(entity);
                tracks.tracks.push_back(std::move(source.motion));actors.Add<engine::gameplay::SampledPose>(entity,engine::gameplay::SampledPose{*result->cinematic_clock,std::uint32_t(tracks.tracks.size())});
                actors.Add<engine::gameplay::ClipPlayback>(entity,source.initial_clip);
                actors.Add<engine::gameplay::ClipTransition>(entity);actors.Add<engine::gameplay::FireTrigger>(entity);
                engine::gameplay::FireSequence fire;
                if(source.weapon && source.weapon->rate>Engine::Math::Fixed{}) {fire.period=Engine::Math::Fixed::One()/source.weapon->rate;fire.charge=source.weapon->charge;fire.reload=source.weapon->reload;fire.capacity=source.weapon->clip_size;fire.rounds=fire.capacity;}
                actors.Add<engine::gameplay::FireSequence>(entity,fire);
                if(source.held_weapon.model.Is_Valid()) {
                    const auto gun_id=std::uint32_t(state.models.size());state.models.push_back(source.weapon->model);
                    PreparedModel gun;gun.id=gun_id;gun.name=source.weapon->model;gun.gpu=std::move(source.held_weapon);gun.view=std::make_unique<ModelView>();
                    const auto* asset=assets.Try_Get_Model(gun.gpu.model);if(!asset->Rig().bones.empty() && !gun.view->pose.Initialize(asset->Rig(),error)) return std::unexpected(error);
                    for(const auto& attachment:asset->Rig().attachments) gun.view->lod=std::max(gun.view->lod,attachment.lod);
                    for(const auto& part:asset->Submeshes()) {const auto name=Assets::Canonicalize_Asset_Name(part.name);const bool muzzle=name.find("muzzleflash")!=std::string::npos || name.find("mz")!=std::string::npos;gun.view->muzzle_parts.push_back(muzzle);gun.view->visible.push_back((part.source_attributes&Assets::W3D::W3DMeshAttributeHidden)==0 && !muzzle);}
                    gun.drawable=true;result->models.push_back(std::move(gun));actors.Add<engine::gameplay::AttachedModel>(entity,engine::gameplay::AttachedModel{gun_id,std::uint32_t(source.pose.Bone_Index("GUNBONE"))});
                }
            }
            world.Commit(actors);
            ecs::CommandBuffer observers;
            auto& invocations=world.Resource<engine::gameplay::BehaviorInvocations>();
            for(const auto& script:timeline_scripts) {
                const auto program=std::ranges::find(behaviors->library.programs,script.program,&engine::gameplay::BehaviorProgram::name);
                if(program==behaviors->library.programs.end()) continue;
                const auto actor=std::ranges::find(pending_actors,std::int32_t(script.subject),&std::pair<std::int32_t,ecs::DeferredEntity>::first);
                if(actor==pending_actors.end()) return std::unexpected("cinematic script has no actor");
                const auto subject=actors.Resolve(actor->second);
                // Runtime-created objects have generated identities, outside
                // the saved level-ID namespace. Preserve actor associations.
                const auto authored=0x100000000ull+std::uint64_t(script.subject);
                state.authored_entities.emplace(authored,subject);
                engine::gameplay::LuaBehavior binding{subject,authored,invocations.next_order++,std::uint32_t(program-behaviors->library.programs.begin())};
                binding.enabled=0;binding.invocation=std::uint32_t(invocations.arguments.size());
                invocations.arguments.push_back({std::int64_t(authored),script.parameters});
                const auto observer=observers.Create();observers.Add<engine::gameplay::LuaBehavior>(observer,binding);observers.Add<engine::gameplay::LuaBehaviorState>(observer);
                observers.Add<engine::gameplay::TimelineBehavior>(observer,engine::gameplay::TimelineBehavior{*result->cinematic_clock,std::uint32_t(script.id)});
            }
            world.Commit(observers);
            ecs::CommandBuffer wheels;
            ecs::CommandBuffer projectiles;std::vector<std::pair<ecs::Entity,ecs::DeferredEntity>> launcher_muzzles;std::map<std::string,std::uint32_t> projectile_models;
            for(std::size_t i=0;i<pending_actors.size();++i) {
                const auto entity=actors.Resolve(pending_actors[i].second);result->cinematic_slots.emplace(pending_actors[i].first,entity);
                auto& source=cinematic->actors[i];
                if(source.weapon && source.projectile.model.Is_Valid() && !source.muzzle.samples.empty()) {
                    const auto identity=Assets::Canonicalize_Asset_Name(source.weapon->projectile);std::uint32_t model_id{};
                    if(const auto found=projectile_models.find(identity);found!=projectile_models.end()) model_id=found->second;
                    else {
                        model_id=std::uint32_t(state.models.size());state.models.push_back(source.weapon->projectile);projectile_models.emplace(identity,model_id);
                        PreparedModel model;model.id=model_id;model.name=source.weapon->projectile;model.gpu=std::move(source.projectile);model.view=std::make_unique<ModelView>();const auto* asset=assets.Try_Get_Model(model.gpu.model);std::string error;
                        if(!asset->Rig().bones.empty() && !model.view->pose.Initialize(asset->Rig(),error)) return std::unexpected(error);
                        for(const auto& attachment:asset->Rig().attachments) model.view->lod=std::max(model.view->lod,attachment.lod);
                        for(const auto& part:asset->Submeshes()) model.view->visible.push_back((part.source_attributes&Assets::W3D::W3DMeshAttributeHidden)==0);
                        model.drawable=!model.gpu.geometry.empty();if(!Graphics::Prepare_Model_Emitters(assets,model.gpu.model,model.emitters,error)) return std::unexpected(error);result->models.push_back(std::move(model));
                    }
                    const auto muzzle=projectiles.Create();tracks.tracks.push_back(std::move(source.muzzle));projectiles.Add<engine::gameplay::SampledPose>(muzzle,engine::gameplay::SampledPose{*result->cinematic_clock,std::uint32_t(tracks.tracks.size())});projectiles.Add<engine::gameplay::AffinePose>(muzzle);launcher_muzzles.emplace_back(entity,muzzle);
                    // SoldierGameObj::Get_Muzzle accepts a target within 20
                    // degrees of its animated gun, otherwise keeping the gun
                    // axis. Vehicles fire down their authored muzzle axis.
                    engine::gameplay::ProjectileLauncher launcher;launcher.model=model_id;launcher.speed=source.weapon->velocity;launcher.range=source.weapon->range;launcher.categories=source.weapon->collision_categories;
                    launcher.aim=source.pose.Bone_Index("GUNBONE")<source.pose.Bone_Count() ? engine::gameplay::ProjectileAim::ConstrainedTarget : engine::gameplay::ProjectileAim::MuzzleForward;
                    launcher.target_cone_cosine=Engine::Math::Cos(Engine::Math::TurnFromDegrees(20));projectiles.Add<engine::gameplay::ProjectileLauncher>(entity,launcher);
                }
                const bool animated=std::ranges::any_of(cinematic->library.definition.actions,[&](const auto& action) {return action.opcode==content::CinematicOpcode::Animation && action.slot==pending_actors[i].first;});
                for(auto wheel:cinematic->actors[i].wheels) {wheel.parent=entity;const auto pending=wheels.Create();wheels.Add<engine::gameplay::Suspension>(pending,wheel);wheels.Add<engine::gameplay::SuspensionState>(pending);}
                if(animated || cinematic->actors[i].initial_clip.clip || !cinematic->actors[i].wheels.empty()) {
                    result->pose_library.push_back(std::move(cinematic->actors[i].pose));wheels.Add<engine::gameplay::PoseBinding>(entity,engine::gameplay::PoseBinding{std::uint32_t(result->pose_library.size())});
                }
            }
            world.Commit(wheels);
            world.Commit(projectiles);for(const auto& [actor,muzzle]:launcher_muzzles) world.Get<engine::gameplay::ProjectileLauncher>(actor)->muzzle=projectiles.Resolve(muzzle);
            std::printf("retail cinematic prepared: Lua %s subject=%llu, %s, %zu commands, %zu actors, shared fixed motion/timeline; attached mission scripts and combat effects remain incomplete\n",
                (**startup).program.c_str(),static_cast<unsigned long long>((**startup).subject),(**startup).control_file.c_str(),cinematic->library.definition.actions.size(),cinematic->actors.size());
            world.Resource<CinematicLibrary>()=std::move(cinematic->library);result->cinematic_music=std::move(cinematic->music);result->music_fade_ms=(**startup).music_fade_ms;
        }
        result->static_file=level->static_file;result->statics=level->statics.size();
        result->session->Step();return result;
    }
    LevelViewServices m_services;
    std::unique_ptr<Graphics::DomeRenderer> m_sky;
    std::vector<engine::audio::SoundEventDefinition> m_sound_catalog;
    engine::audio::PreparedSoundLibrary m_sound_library;
    std::unique_ptr<engine::audio::EmitterPlayer> m_audio;
    std::optional<ecs::Entity> m_cinematic_clock;std::map<std::int32_t,ecs::Entity> m_cinematic_slots;engine::audio::VoiceId m_cinematic_music{};
    void UpdateCamera(Engine::Math::LookAngles angles) {
        if(CinematicActive()) {
            const auto slot=m_session->World().Get<CinematicDirector>(*m_cinematic_clock)->camera;const auto entity=m_cinematic_slots.at(slot);
            const auto binding=m_session->World().Get<engine::gameplay::PoseBinding>(entity);auto pose=m_pose_library->at(binding->definition-1);
            const auto& clip=*m_session->World().Get<engine::gameplay::ClipPlayback>(entity);
            Graphics::RenderTransform bone;if(!pose.Evaluate(clip.clip-1,Engine::Math::ToFloat(clip.frame),false) || !pose.Bone_Transform(pose.Bone_Index("CAMERA"),bone)) {m_error="cinematic camera pose failed";return;}
            auto world=Graphics::Affine_Identity();const auto& source=m_session->World().Get<engine::gameplay::AffinePose>(entity)->transform;
            std::ranges::transform(source.elements,world.matrix.begin(),[](auto value) {return Engine::Math::ToFloat(value);});
            m_camera.Set_Transform(Graphics::Multiply_Affine(world,bone));m_camera.Set_View_Plane(75.f*std::numbers::pi_v<float>/180.f);return;
        }
        const auto player=m_session->World().Resource<SceneState>().player;
        const auto* pose=m_session->World().Get<engine::gameplay::Transform>(player);
        const auto yaw=Engine::Math::ToFloat(Engine::Math::Radians(angles.heading));
        const auto weight=Engine::Math::Fixed::FromBinary32Bits(std::bit_cast<std::uint32_t>(m_anchor_transition.PreviousWeight()));
        m_camera_profile=content::BlendCameraProfile(m_profile,m_transition_profile,weight);
        m_camera_heading=m_anchor_transition.Heading(yaw);
        m_camera_anchor=m_anchor_transition.Anchor({Engine::Math::ToFloat(pose->position.x),Engine::Math::ToFloat(pose->position.y),Engine::Math::ToFloat(pose->position.z)});
        const auto tilt=-Engine::Math::ToFloat(angles.pitch)+Engine::Math::ToFloat(m_camera_profile.view_tilt_degrees)*std::numbers::pi_v<float>/180.f;
        const Engine::Math::Vector3 forward{std::cos(m_camera_heading)*std::cos(tilt),std::sin(m_camera_heading)*std::cos(tilt),-std::sin(tilt)};
        const auto distance=Engine::Math::ToFloat(m_camera_profile.distance);
        const Engine::Math::Vector3 position{m_camera_anchor.x-forward.x*distance,m_camera_anchor.y-forward.y*distance,m_camera_anchor.z+Engine::Math::ToFloat(m_camera_profile.height)-forward.z*distance};
        const auto transform=Engine::Math::AffineTransform3::Look_Along(position,forward);Graphics::RenderTransform camera;
        std::copy(transform.elements.begin(),transform.elements.end(),camera.matrix.begin());camera.matrix[12]=camera.matrix[13]=camera.matrix[14]=0;camera.matrix[15]=1;
        m_camera.Set_Transform(camera);
        m_camera.Set_View_Plane(Engine::Math::ToFloat(m_camera_profile.fov_degrees)*std::numbers::pi_v<float>/180.f);
    }
    engine::gui::w3d::ModelBackdropView m_backdrop;
    std::shared_ptr<PreparedBackdrop> m_prepared_backdrop;
    std::vector<engine::gui::w3d::BackdropText> m_backdrop_text;
    float m_animation_drawn{};
    bool m_backdrop_started{},m_backdrop_ready{};
    engine::gui::w3d::LoadingModel m_loading;
    std::shared_ptr<LoadState> m_load;
    std::shared_ptr<const Graphics::ResourceLoadSource> m_source;
    std::unique_ptr<PreparedScene> m_prepared;
    std::size_t m_model_cursor{},m_upload_total{},m_uploaded{};
    bool m_upload_started{};
    std::string m_error;
	std::unique_ptr<Session> m_session;
	std::map<std::uint32_t, std::unique_ptr<ModelView>> m_models;
    std::map<std::pair<std::uint64_t,std::size_t>,ParticleInstance> m_particles;std::size_t m_particle_count{},m_projectile_particle_count{};
	Graphics::CameraState m_camera;
    std::shared_ptr<const std::vector<Graphics::ModelAssetPose>> m_pose_library;
    content::CameraProfile m_profile;
    content::CameraProfile m_first_profile,m_third_profile;
    content::CameraProfile m_transition_profile,m_camera_profile;
    engine::camera::AnchorTransition m_anchor_transition;Engine::Math::Vector3 m_camera_anchor{};float m_camera_heading{};
    std::map<std::string,Assets::TextureAssetHandle,std::less<>> m_objective_textures;
    hud::ObjectiveViewModel m_objectives;bool m_objectives_drawn{};
    hud::PlayerHudViewModel m_player_hud;content::HudSettings m_hud_settings;bool m_player_hud_drawn{};
    void UpdateObjectives() {
        using namespace engine::gameplay;std::vector<hud::ObjectiveRow> rows;
        ecs::Query<ecs::Read<MissionObjective>,ecs::Read<SimulationAge>,ecs::Read<TrackedPosition>> query(m_session->World());
        query.ForEachChunk([&](auto chunk) {
            const auto entities=chunk.Entities();const auto values=chunk.template Get<MissionObjective>();const auto ages=chunk.template Get<SimulationAge>();const auto positions=chunk.template Get<TrackedPosition>();
            for(std::size_t row=0;row<values.size();++row) rows.push_back({(std::uint64_t(entities[row].generation)<<32)|entities[row].index,values[row],ages[row].Seconds(Session::TicksPerSecond),positions[row].resolved});
        });m_objectives.Update(std::move(rows));
    }
    std::uint32_t m_ladder_transition_sequence{};bool m_ladder_view{};
    bool m_first_person{true};
    bool m_paused{};
    std::size_t m_body_parts{};
    std::size_t m_ladder_motion_ticks{};std::uint32_t m_last_ladder_motion_clip{};
    std::size_t m_objects_drawn{},m_objects_culled{},m_projectile_parts{};
    std::size_t m_conversation_lines{},m_conversations_finished{};
    std::optional<Engine::Math::FixedVector3> m_camera_look_target;
    Engine::Math::FixedVector3 m_last_camera_look_target;std::uint64_t m_last_camera_look_tick{};std::size_t m_camera_looks{};
    hud::HelpTextViewModel m_help;bool m_help_drawn{};
    engine::time::FrameAccumulator m_accumulator{engine::time::FixedStep{Session::TicksPerSecond}};
	std::array<float, 4> m_ambient{0.5f, 0.5f, 0.5f, 0};
};
}
