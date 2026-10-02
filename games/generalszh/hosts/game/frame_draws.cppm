module;
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <cstring>

export module games.generalszh.hosts.game.frame_draws;
import std;

import Graphics.Frame.Runtime;
import Graphics.Frame.SceneRenderers;
import Graphics.Frame.RenderServices;
import Graphics.Capture.FrameCapture;
import Graphics.Scene.Screen.FullscreenOverlay;
import Graphics.Renderer2D;

// What the host draws after the scene each frame (the graphics frame API's executor): the script's screen fade, the
// recorded 2D overlay, and the frame captured to a PNG when asked.
export namespace generalszh::host
{
// What the renderer's frame callbacks need. The graphics frame API takes
// plain function pointers without user data, so the host keeps this one
// pointer for the duration of the run (a limitation of that API).
struct FrameCapture
{
	std::filesystem::path shaders;
	bool captureNextFrame{false};
	std::filesystem::path captureFile;
	bool captured{false};
	// The screen fade over this frame's view (W3DStatusCircle's full-screen blend): 0 none, 1 add, 2 subtract,
	// 3 saturate, 4 multiply; its value.
	std::uint8_t fade{0};
	float fadeValue{0.0f};
};
}

namespace generalszh::host
{
namespace
{
FrameCapture *g_frameCapture = nullptr;

bool InitializeRenderers(Graphics::Device &device)
{
	return Graphics::Initialize_Scene_Renderers(device, g_frameCapture->shaders) && Graphics::Get_Render_Services().Initialize();
}

bool ExecuteFrameDraws(Graphics::Device &device, Graphics::CommandList &, const Graphics::FrameTargets &targets) noexcept
{
	FrameCapture &scene = *g_frameCapture;
	// The screen fade over the view, under the interface (W3DStatusCircle): the value's grey added, taken away, twice
	// colour-multiplied (saturate) or multiplied.
	if (scene.fade != 0)
	{
		const float intensity = std::clamp(scene.fadeValue, 0.0f, 1.0f);
		Graphics::FullscreenOverlayDescription overlay;
		overlay.color = {intensity, intensity, intensity, 1.0f};
		switch (scene.fade)
		{
		default:
		case 1: overlay.blend_mode = Graphics::RHIBlendMode::Additive; break;
		case 2:
			overlay.blend_mode = Graphics::RHIBlendMode::Additive;
			overlay.blend_operation = Graphics::RHIBlendOperation::ReverseSubtract;
			break;
		case 3:
			overlay.blend_mode = Graphics::RHIBlendMode::ColorMultiply;
			overlay.draw_count = 2;
			break;
		case 4: overlay.blend_mode = Graphics::RHIBlendMode::Multiply; break;
		}
		Graphics::FullscreenOverlayRenderer &fades = Graphics::GetFullscreenOverlayRenderer();
		if (fades.Set_Overlay(overlay))
			fades.Render(device.Immediate_Command_List(), targets.backbuffer.texture, {0, 0, targets.backbuffer.width, targets.backbuffer.height, 0, 1});
	}
	// 2D overlay (shell menus) recorded this frame.
	if (!Graphics::Get_Renderer2D().Execute(device, device.Immediate_Command_List(), targets.backbuffer.texture, targets.depth.texture,
			{0, 0, targets.backbuffer.width, targets.backbuffer.height, 0, 1}))
		return false;
	if (scene.captureNextFrame)
	{
		scene.captureNextFrame = false;
		Graphics::FrameCapture capture;
		// The swap chain uses the device's RGBA8 backbuffer format.
		const auto frame = capture.Read(device, targets.backbuffer.texture, targets.backbuffer.width,
			targets.backbuffer.height, Graphics::RHITextureFormat::RGBA8_UNorm);
		if (frame.Is_Valid())
		{
			std::vector<std::uint8_t> rgba(frame.pixels.size());
			std::memcpy(rgba.data(), frame.pixels.data(), rgba.size());
			for (std::size_t pixel = 3; pixel < rgba.size(); pixel += 4)
				rgba[pixel] = 255;
			if (scene.captureFile.extension() == ".bmp")
			{
				// W3DDisplay::takeScreenShot's CreateBMPFile: 24-bit colour, no alpha.
				std::vector<std::uint8_t> rgb;
				rgb.reserve(static_cast<std::size_t>(frame.width) * frame.height * 3);
				for (std::uint32_t y = 0; y < frame.height; ++y)
					for (std::uint32_t x = 0; x < frame.width; ++x)
					{
						const std::uint8_t *pixel = rgba.data() + static_cast<std::size_t>(y) * frame.row_pitch + static_cast<std::size_t>(x) * 4;
						rgb.insert(rgb.end(), {pixel[0], pixel[1], pixel[2]});
					}
				scene.captured = stbi_write_bmp(scene.captureFile.string().c_str(), static_cast<int>(frame.width), static_cast<int>(frame.height), 3,
									 rgb.data()) != 0;
			}
			else
				scene.captured = stbi_write_png(scene.captureFile.string().c_str(), static_cast<int>(frame.width),
					static_cast<int>(frame.height), 4, rgba.data(), static_cast<int>(frame.row_pitch)) != 0;
		}
	}
	return true;
}
}
}

export namespace generalszh::host
{
// The frame device's draws for this run: the scene renderers made with `capture`'s shaders, then each frame its fade,
// the 2D overlay and its capture (`capture` outlives the run's frames). False: the device would not take them.
bool RegisterFrameDraws(FrameCapture &capture)
{
	g_frameCapture = &capture;
	return Graphics::Register_Frame_Draw_Executor(&InitializeRenderers, &ExecuteFrameDraws);
}

void ReleaseFrameDraws() noexcept { g_frameCapture = nullptr; }
}
