export module games.generalszh.hosts.game.shell_menu;
import std;

import games.generalszh.content.images.mapped_image_files;
import Engine.UI.WND;
import Engine.UI.WND.Document;
import Engine.UI.WND.Layout;
import Graphics.Renderer2D;
import engine.filesystem.core.virtual_file_system;
import engine.localization.model.string_table;

namespace generalszh::host::detail
{
inline std::u16string ToUtf16(std::string_view utf8)
{
	std::u16string out;
	for (std::size_t index = 0; index < utf8.size();)
	{
		const auto byte = static_cast<unsigned char>(utf8[index]);
		const auto next = [&](std::size_t offset) { return static_cast<std::uint32_t>(static_cast<unsigned char>(utf8[index + offset]) & 0x3Fu); };
		std::uint32_t code = byte;
		std::size_t length = 1;
		if (byte >= 0xF0 && index + 3 < utf8.size())
			code = ((byte & 0x07u) << 18) | (next(1) << 12) | (next(2) << 6) | next(3), length = 4;
		else if (byte >= 0xE0 && index + 2 < utf8.size())
			code = ((byte & 0x0Fu) << 12) | (next(1) << 6) | next(2), length = 3;
		else if (byte >= 0xC0 && index + 1 < utf8.size())
			code = ((byte & 0x1Fu) << 6) | next(1), length = 2;
		index += length;
		if (code >= 0x10000)
		{
			code -= 0x10000;
			out += static_cast<char16_t>(0xD800 + (code >> 10));
			out += static_cast<char16_t>(0xDC00 + (code & 0x3FF));
		}
		else
		{
			out += static_cast<char16_t>(code);
		}
	}
	return out;
}
}

export namespace generalszh::host
{
// GlobalLanguage::getResolutionFontSizeScale, its default (Strict) method: the smaller of the screen's
// width and height over 800 x 600, grown at `adjustment` of that rate (Language.ini's
// ResolutionFontAdjustment, 0.7), never past it.
inline float FontScale(std::uint32_t width, std::uint32_t height, float adjustment)
{
	const float scale = std::min(static_cast<float>(width) / 800.0f, static_cast<float>(height) / 600.0f);
	return std::min(1.0f + (scale - 1.0f) * adjustment, scale);
}

// A localized string (Generals.csf) for a view model's items.
inline std::u16string Localized(const engine::localization::StringTable &strings, std::string_view label)
{
	return detail::ToUtf16(strings.Text(label));
}

// Hosts one WND layout of the front end (e.g. Window/Menus/MainMenu.wnd):
// loads, localizes and draws it over the shell map. Screen state comes from
// a view model through WND bindings (MVVM); this class holds no menu logic.
// Each layout is drawn scaled from the resolution it was authored at (most at 800x600).
class ShellMenu
{
public:
	bool Load(const engine::filesystem::VirtualFileSystem &files, std::string_view wnd, const engine::localization::StringTable &strings,
		std::uint32_t width, std::uint32_t height, std::string &error, float fontScale = 1.0f)
	{
		using namespace Engine::UI::WND;
		// Mapped images in the original's load order; later definitions win.
		for (const std::string &path : content::MappedImageFiles(files))
		{
			const auto text = files.ReadText(path);
			if (!text || !Parse_Mapped_Image_INI(*text, m_catalog))
			{
				error = "mapped images: cannot parse " + path;
				return false;
			}
		}
		const auto source = files.ReadText(wnd);
		if (!source || !m_document.Parse(*source))
		{
			error = "cannot read " + std::string(wnd);
			return false;
		}
		m_document.Localize([&](std::string_view label) { return detail::ToUtf16(strings.Text(label)); });
		// W3DNoDraw windows draw nothing themselves; only their children show.
		for (WNDWindow &window : m_document.Mutable_Windows())
			if (window.draw_callback == "W3DNoDraw")
				window.flags |= static_cast<std::uint32_t>(WindowFlag::SeeThrough);
		WNDDocumentResolveReport report;
		m_document.Resolve_Images(m_catalog, report, false);
		if (!m_document.Resolve_Fonts(report, fontScale))
		{
			error = "fonts could not be resolved";
			return false;
		}
		m_scaleX = static_cast<float>(width) / static_cast<float>(AuthoredWidth());
		m_scaleY = static_cast<float>(height) / static_cast<float>(AuthoredHeight());
		if (!Rebuild())
		{
			error = "render list could not be built";
			return false;
		}
		m_loaded = true;
		return true;
	}

	// The layout, for binding a view model to it (see Engine.UI.WND.Bindings).
	Engine::UI::WND::WNDDocument &Document() noexcept { return m_document; }

	// Layout units to screen pixels.
	float ScaleX() const noexcept { return m_scaleX; }
	float ScaleY() const noexcept { return m_scaleY; }

	// The resolution the layout was authored at (its CREATIONRESOLUTION; 800x600 if it names none).
	int AuthoredWidth() const noexcept { return m_document.Creation_Width() > 0 ? m_document.Creation_Width() : 800; }
	int AuthoredHeight() const noexcept { return m_document.Creation_Height() > 0 ? m_document.Creation_Height() : 600; }

	// An image by name: a mapped image (MappedImages INI) or, named by its path, a texture file.
	Engine::UI::WND::ImageRef Image(std::string_view name) const
	{
		if (name.find_first_of("/\\") != std::string_view::npos)
			return Engine::UI::WND::Resolve_Image_Reference(name);
		return m_catalog.Resolve(name);
	}

	// A mapped image's width on screen (its own pixels, as winSetSize takes them), in the layout's units; 0 if unknown.
	int ImageWidth(std::string_view name) const
	{
		const Engine::UI::WND::ImageDefinition *definition = m_catalog.Find(name);
		if (definition == nullptr || m_scaleX <= 0.0f)
			return 0;
		return static_cast<int>(static_cast<float>(definition->width) / m_scaleX + 0.5f);
	}

	// A mapped image's own size in pixels (its MappedImages Coords); none if unknown.
	std::optional<std::pair<int, int>> ImageSize(std::string_view name) const
	{
		const Engine::UI::WND::ImageDefinition *definition = m_catalog.Find(name);
		if (definition == nullptr)
			return std::nullopt;
		return std::pair{static_cast<int>(definition->width), static_cast<int>(definition->height)};
	}

	// Rebuilds the render list after bound state changed.
	bool Refresh() { return Rebuild(); }

	// Lays the layout out at one scale keeping its aspect, placed by the anchors in the screen (the in-game bar:
	// Fit_Viewport centred and at the bottom): its top-level windows shift by the leftover room, in layout units.
	bool Fit(std::uint32_t width, std::uint32_t height, Engine::UI::WND::LayoutAnchor horizontal, Engine::UI::WND::LayoutAnchor vertical)
	{
		const auto transform = Engine::UI::WND::Fit_Viewport(AuthoredWidth(), AuthoredHeight(), static_cast<int>(width), static_cast<int>(height),
			horizontal, vertical);
		if (transform.scale <= 0.0)
			return false;
		m_scaleX = m_scaleY = static_cast<float>(transform.scale);
		const int dx = static_cast<int>(std::lround(transform.x / transform.scale));
		const int dy = static_cast<int>(std::lround(transform.y / transform.scale));
		std::vector<std::string> tops;
		for (Engine::UI::WND::NodeIndex node = m_document.Root(); node != Engine::UI::WND::Invalid_Node && node < m_document.Windows().size();
			node = m_document.Windows()[node].next_sibling)
			tops.push_back(m_document.Windows()[node].name);
		for (const std::string &name : tops)
			if (const auto *window = m_document.Find_Window(name))
				m_document.Move_Window(name, window->screen_region.left + dx, window->screen_region.top + dy);
		return Rebuild();
	}

	// A screen point in the layout's units.
	std::pair<float, float> ToLayout(float x, float y) const noexcept { return {x / m_scaleX, y / m_scaleY}; }

	bool Draw(Graphics::Renderer2D &renderer) noexcept
	{
		return !m_loaded || m_renderer.Render(m_list, renderer);
	}

private:
	bool Rebuild()
	{
		m_list = Engine::UI::WND::RenderList(m_document.Size());
		return m_document.Build_Render_List(m_list, m_scaleX, m_scaleY);
	}

	float m_scaleX{1};
	float m_scaleY{1};
	Engine::UI::WND::ImageCatalog m_catalog;
	Engine::UI::WND::WNDDocument m_document;
	Engine::UI::WND::RenderList m_list;
	Engine::UI::WND::Renderer m_renderer;
	bool m_loaded{false};
};
}
