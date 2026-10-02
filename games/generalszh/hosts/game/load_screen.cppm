module;
#include <cstdio>

export module games.generalszh.hosts.game.load_screen;
import std;

import games.generalszh.shell.intro.load_screen_bitmap;
import Graphics.Frame.Runtime;
import Graphics.Renderer2D;
import Engine.UI.WND;

// WinMain's load screen: Install_Final.bmp from the install's root, blitted unscaled at the window's corner as the window
// is made (WM_PAINT), staying on screen while the game loads until the engine draws its first frame. Here: one frame of
// it, presented before the content loads (nothing when the install has none, as LoadImage failing leaves the window
// unpainted).
export namespace generalszh::host
{
inline void PresentLoadScreen(const std::filesystem::path &install)
{
	std::ifstream file(install / "Install_Final.bmp", std::ios::binary);
	if (!file)
		return;
	std::vector<char> raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	const auto bitmap = shell::DecodeLoadScreenBitmap(std::as_bytes(std::span(raw)));
	if (!bitmap)
	{
		std::fprintf(stderr, "load screen: Install_Final.bmp could not be read\n");
		return;
	}
	if (!Graphics::Graphics_Begin_Frame())
		return;
	Graphics::Renderer2D &renderer = Graphics::Get_Renderer2D();
	Engine::UI::WND::ImageRef picture;
	picture.generated = renderer.Register_Texture(
		{Graphics::TextureHandle(0x7AD1002u, 1), bitmap->width, bitmap->height, bitmap->width * 4, 1, std::span<const std::byte>(bitmap->rgba)});
	const auto [width, height] = shell::LoadScreenBlitSize(*bitmap);
	// BitBlt copies the bitmap's top-left part, unscaled: its texture coordinates cut to what is drawn.
	picture.uv = {0.0f, 0.0f, static_cast<float>(width) / static_cast<float>(bitmap->width), static_cast<float>(height) / static_cast<float>(bitmap->height)};
	Engine::UI::WND::DrawList list;
	list.Add_Image(picture, {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)});
	Engine::UI::WND::Renderer draw;
	draw.Render_Draw_List(list, renderer);
	if (!(Graphics::Graphics_Execute_Queued_Draws() && Graphics::Graphics_End_Frame() && Graphics::Graphics_Present()))
		Graphics::Graphics_Abort_Frame();
}
}
