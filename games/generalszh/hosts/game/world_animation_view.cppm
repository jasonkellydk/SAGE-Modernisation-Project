export module games.generalszh.hosts.game.world_animation_view;
import std;

import Engine.UI.WND;
import Engine.UI.WND.Document;
import Graphics.Renderer2D;
import engine.filesystem.core.virtual_file_system;
import games.generalszh.content.images.mapped_image_files;
import games.generalszh.hosts.game.game_client;

// Draws the world animations the overlay lists (InGameUI::updateAndDrawWorldAnimations): each image centred on its
// point, its own size scaled over the camera zoom, at its alpha. Images come from the mapped image INI files.
export namespace generalszh::host
{
class WorldAnimationView
{
public:
	bool Load(const engine::filesystem::VirtualFileSystem &files)
	{
		for (const std::string &path : content::MappedImageFiles(files))
		{
			const auto text = files.ReadText(path);
			if (!text || !Engine::UI::WND::Parse_Mapped_Image_INI(*text, m_catalog))
				return false;
		}
		return true;
	}

	bool Draw(const std::vector<OverlayImage> &images, Graphics::Renderer2D &renderer)
	{
		if (images.empty())
			return true;
		m_list.Clear();
		for (const OverlayImage &image : images)
		{
			const Engine::UI::WND::ImageDefinition *definition = m_catalog.Find(image.image);
			if (definition == nullptr)
				continue;
			if (image.placement != OverlayImage::Placement::Centred)
			{
				const int width = static_cast<int>(definition->width), height = static_cast<int>(definition->height);
				const auto rect = image.placement == OverlayImage::Placement::Veterancy
					? presentation::PlaceVeterancy(static_cast<int>(image.x), static_cast<int>(image.y), image.healthBoxWidth, image.zoom, width, height)
					: presentation::PlaceIcon(image.icon, image.region, width, height, image.iconScale);
				const float left = static_cast<float>(rect.x), top = static_cast<float>(rect.y);
				m_list.Add_Image(m_catalog.Resolve(image.image), {left, top, left + static_cast<float>(rect.width), top + static_cast<float>(rect.height)},
					{1.0f, 1.0f, 1.0f, image.alpha});
				continue;
			}
			const float width = static_cast<float>(definition->width) * image.scale;
			const float height = static_cast<float>(definition->height) * image.scale;
			const float left = std::floor(image.x - width / 2.0f), top = std::floor(image.y - height / 2.0f);
			m_list.Add_Image(m_catalog.Resolve(image.image), {left, top, left + width, top + height}, {1.0f, 1.0f, 1.0f, image.alpha});
		}
		return m_renderer.Render_Draw_List(m_list, renderer);
	}

private:
	Engine::UI::WND::ImageCatalog m_catalog;
	Engine::UI::WND::DrawList m_list;
	Engine::UI::WND::Renderer m_renderer;
};
}
