module;

#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <cmath>

export module Graphics.Frame.RenderSettings;

namespace Graphics
{

export enum class RenderPrelitMode : std::uint8_t
{
	Vertex,
	LightmapMultiPass,
	LightmapMultiTexture,
};

export class RenderSettings final
{
public:
	RenderSettings() noexcept {
		const char* pbr = std::getenv("GRAPHICS_PBR");
		m_pbr_enabled = !pbr || pbr[0] != '0';
		const char* flip = std::getenv("GRAPHICS_PBR_NORMAL_FLIP_GREEN");
		m_pbr_normal_flip_green = flip && flip[0] == '1';
        const auto value = [](const char* key, float fallback, float limit) {
            const char* text = std::getenv(key);
            if (!text) return fallback;
            char* end = nullptr;
            const float parsed = std::strtof(text, &end);
            return end != text && *end == 0 && std::isfinite(parsed) ? std::clamp(parsed,0.f,limit) : fallback;
        };
        m_parallax_scale = value("GRAPHICS_PBR_PARALLAX",1.f,4.f);
        m_displacement_scale = value("GRAPHICS_PBR_DISPLACEMENT",.6f,2.f);
        m_terrain_subdivisions = static_cast<unsigned>(value("GRAPHICS_PBR_TERRAIN_SUBDIVISIONS",2.f,4.f));
        m_terrain_subdivisions = std::max(1u,m_terrain_subdivisions);
	}
    float PBR_Parallax_Scale() const noexcept { return m_pbr_enabled ? m_parallax_scale : 0; }
    float PBR_Displacement_Scale() const noexcept { return m_pbr_enabled ? m_displacement_scale : 0; }
    unsigned PBR_Terrain_Subdivisions() const noexcept { return m_pbr_enabled && m_displacement_scale > 0 ? m_terrain_subdivisions : 1; }
	bool PBR_Normal_Flip_Green() const noexcept { return m_pbr_normal_flip_green; }
	bool PBR_Enabled() const noexcept { return m_pbr_enabled; }
	void Set_PBR_Normal_Flip_Green(bool flip) noexcept { m_pbr_normal_flip_green = flip; }

	bool Is_Texturing_Enabled() const noexcept
	{
		return m_texturing_enabled;
	}

	void Set_Texturing_Enabled(bool enabled) noexcept
	{
		m_texturing_enabled = enabled;
	}

	bool Is_Coloring_Enabled() const noexcept
	{
		return m_coloring_enabled;
	}

	void Set_Coloring_Enabled(bool enabled) noexcept
	{
		m_coloring_enabled = enabled;
	}

	bool Is_Sorting_Enabled() const noexcept
	{
		return m_sorting_enabled;
	}

	void Set_Sorting_Enabled(bool enabled) noexcept
	{
		m_sorting_enabled = enabled;
	}

	bool Is_Munge_Sort_On_Load_Enabled() const noexcept
	{
		return m_munge_sort_on_load;
	}

	void Set_Munge_Sort_On_Load_Enabled(bool enabled) noexcept
	{
		m_munge_sort_on_load = enabled;
	}

	bool Is_Overbright_Modify_On_Load_Enabled() const noexcept
	{
		return m_overbright_modify_on_load;
	}

	void Set_Overbright_Modify_On_Load_Enabled(bool enabled) noexcept
	{
		m_overbright_modify_on_load = enabled;
	}

	RenderPrelitMode Get_Prelit_Mode() const noexcept
	{
		return m_prelit_mode;
	}

	void Set_Prelit_Mode(RenderPrelitMode mode) noexcept
	{
		m_prelit_mode = mode;
	}

	bool Get_Preserve_FPU() const noexcept
	{
		return m_preserve_fpu;
	}

	void Set_Preserve_FPU(bool preserve) noexcept
	{
		m_preserve_fpu = preserve;
	}

private:
    float m_parallax_scale = 1;
    float m_displacement_scale = .6f;
    unsigned m_terrain_subdivisions = 2;
	bool m_pbr_enabled = true;
	bool m_pbr_normal_flip_green = false;
	bool m_texturing_enabled = true;
	bool m_coloring_enabled = false;
	bool m_sorting_enabled = true;
	bool m_munge_sort_on_load = false;
	bool m_overbright_modify_on_load = false;
	RenderPrelitMode m_prelit_mode = RenderPrelitMode::LightmapMultiPass;
	bool m_preserve_fpu = false;
};

// The game-facing adapter and scene traversal share this instance.
export RenderSettings &Get_Render_Settings() noexcept
{
	static RenderSettings settings;
	return settings;
}

}
