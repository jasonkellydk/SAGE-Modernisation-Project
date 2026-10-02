module;

#define BOOST_TEST_MODULE EngineUIWNDDocumentTests

#include <boost/test/included/unit_test.hpp>

export module Engine.UI.WND.Document.Tests;
import std;

import Assets.Cache;
import Engine.UI.WND;
import Engine.UI.WND.Document;
import Engine.UI.WND.Tests.Support.GameData;

using namespace Engine::UI::WND;
using namespace Engine::UI::WND::Tests;

#ifndef ENGINE_UI_WND_GAME_DATA_ROOT
#define ENGINE_UI_WND_GAME_DATA_ROOT "."
#endif

namespace
{

std::filesystem::path Game_Data_Root()
{
	return std::filesystem::path(ENGINE_UI_WND_GAME_DATA_ROOT);
}

std::shared_ptr<const GameData> Load_Fixture(GameData &storage)
{
	storage = Load_Game_Data(Game_Data_Root());
	if (storage.root.empty() || storage.textures.empty())
		return {};
	return std::make_shared<const GameData>(storage);
}

bool Load_Menu(
	const GameData &data,
	std::string_view filename,
	WNDDocument &document,
	ImageCatalog &catalog)
{
	std::string source;
	if (!Load_WND_Source(data.root / "Window" / "Menus" / filename, source)
		|| !document.Parse(source)
		|| !Load_Game_Image_Catalog(data, catalog))
		return false;
	return true;
}

}

BOOST_AUTO_TEST_CASE(parses_generalsmd_wnd_hierarchy_and_authored_geometry)
{
	GameData data;
	const auto fixture = Load_Fixture(data);
	BOOST_REQUIRE(fixture != nullptr);

	WNDDocument document;
	ImageCatalog catalog;
	BOOST_REQUIRE(Load_Menu(*fixture, "MainMenu.wnd", document, catalog));
	BOOST_CHECK_GT(document.Size(), 20u);
	BOOST_CHECK_EQUAL(document.Creation_Width(), 800);
	BOOST_CHECK_EQUAL(document.Creation_Height(), 600);
	BOOST_REQUIRE(!document.Windows().empty());
	BOOST_CHECK(document.Windows()[0].type == WindowType::User);
	BOOST_CHECK_EQUAL(document.Windows()[0].authored_region.right, 800);
	BOOST_CHECK_EQUAL(document.Windows()[0].authored_region.bottom, 600);
	BOOST_CHECK(document.Windows()[0].first_child != Invalid_Node);
	BOOST_CHECK_EQUAL(document.Windows()[1].screen_region.left,
		document.Windows()[1].authored_region.left);
	BOOST_CHECK_EQUAL(document.Windows()[1].screen_region.top,
		document.Windows()[1].authored_region.top);
	BOOST_CHECK(catalog.Size() > 100u);
	BOOST_REQUIRE(catalog.Find("MainMenuBackdrop") != nullptr);
	BOOST_CHECK_EQUAL(
		catalog.Find("MainMenuBackdrop")->texture,
		"MainMenuBackdropuserinterface.tga");
}

BOOST_AUTO_TEST_CASE(resolves_generalsmd_wnd_images_and_fonts_through_asset_runtime)
{
	GameData data;
	const auto fixture = Load_Fixture(data);
	BOOST_REQUIRE(fixture != nullptr);
	BOOST_REQUIRE(Initialize_Game_Asset_Runtime(fixture));

	WNDDocument document;
	ImageCatalog catalog;
	BOOST_REQUIRE(Load_Menu(*fixture, "MainMenu.wnd", document, catalog));
	WNDDocumentResolveReport report;
	BOOST_REQUIRE_MESSAGE(document.Resolve_Images(catalog, report),
		"WND image references were not fully resolved: " << report.missing_images);
	BOOST_REQUIRE(document.Resolve_Fonts(report));
	BOOST_CHECK_GT(report.resolved_images, 0u);
	BOOST_CHECK_GT(report.built_fonts, 0u);
	BOOST_CHECK_EQUAL(report.missing_images, 0u);
	BOOST_CHECK_EQUAL(report.missing_fonts, 0u);

	BOOST_REQUIRE(!document.Windows().empty());
	const WNDDrawCell &cell = document.Windows()[0].draw_states[0].cells[0];
	BOOST_CHECK(cell.image.texture.Is_Valid());
	BOOST_CHECK_GT(cell.image_width, 0u);
	BOOST_CHECK_GT(cell.image_height, 0u);

	Assets::AssetCache *cache = Assets::Try_Get_Asset_Cache();
	BOOST_REQUIRE(cache != nullptr);
	cache->Wait(cell.image.texture);
	const Assets::TextureAsset *asset = cache->Try_Get_Texture(cell.image.texture);
	BOOST_REQUIRE(asset != nullptr);
	BOOST_CHECK(asset->Has_Pixels());
	BOOST_CHECK_GT(asset->Width(), 0u);
	BOOST_CHECK_GT(asset->Height(), 0u);
}

BOOST_AUTO_TEST_CASE(builds_a_modern_render_list_from_each_menu_document)
{
	GameData data;
	const auto fixture = Load_Fixture(data);
	BOOST_REQUIRE(fixture != nullptr);
	BOOST_REQUIRE(Initialize_Game_Asset_Runtime(fixture));

	for (const std::string_view filename : {
		std::string_view("MainMenu.wnd"),
		std::string_view("SinglePlayerMenu.wnd"),
		std::string_view("SkirmishGameOptionsMenu.wnd")}) {
		WNDDocument document;
		ImageCatalog catalog;
		BOOST_REQUIRE(Load_Menu(*fixture, filename, document, catalog));
		WNDDocumentResolveReport report;
		BOOST_REQUIRE(document.Resolve_Images(catalog, report));
		BOOST_REQUIRE(document.Resolve_Fonts(report));
		RenderList list(document.Size());
		BOOST_REQUIRE(document.Build_Render_List(list, 1.0f, 1.0f));
		BOOST_CHECK_EQUAL(list.Size(), document.Size());
		BOOST_CHECK(list.Root_Head() != Invalid_Node);
		BOOST_CHECK(list.Root_Tail() != Invalid_Node);
	}
}

BOOST_AUTO_TEST_CASE(parses_every_checked_in_generalsmd_wnd_fixture)
{
	GameData data;
	const auto fixture = Load_Fixture(data);
	BOOST_REQUIRE(fixture != nullptr);

	std::size_t checked = 0;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(
		fixture->root, std::filesystem::directory_options::skip_permission_denied)) {
		if (!entry.is_regular_file() || entry.path().extension() != ".wnd")
			continue;
		std::string source;
		BOOST_REQUIRE_MESSAGE(Load_WND_Source(entry.path(), source), entry.path().string());
		WNDDocument document;
		BOOST_REQUIRE_MESSAGE(document.Parse(source), entry.path().string());
		BOOST_CHECK_GT(document.Size(), 0u);
		++checked;
	}
	BOOST_REQUIRE_MESSAGE(checked >= 10u, "The checked-in fixture must cover the real WND menu set");
}

// Image.cpp, Image::parseImageCoords / Image::parseImageStatus (MappedImage block: Texture, TextureWidth,
// TextureHeight, Coords, Status). Coords set the size from the packed rectangle and the UVs over the texture size;
// Status ROTATED_90_CLOCKWISE swaps the size back. Shipped SCShellUserInterface512.INI: WatermarkGLA is packed at
// Left:391 Top:163 Right:487 Bottom:323 and rotated, so it is 160 wide and 96 high. ControlButtonsPro.ini's
// SSObserverUSA (Status = NONE, Left:54 Top:0 Right:144 Bottom:60) is 90 x 60 and replaces the generated
// SSUserInterface512.INI entry of the same name.
BOOST_AUTO_TEST_CASE(legacy_parity_shipped_mapped_images_parse_their_coords_and_rotation)
{
	GameData data;
	const auto fixture = Load_Fixture(data);
	BOOST_REQUIRE(fixture != nullptr);
	ImageCatalog catalog;
	BOOST_REQUIRE(Load_Game_Image_Catalog(*fixture, catalog));

	const ImageDefinition *watermark = catalog.Find("WatermarkGLA");
	BOOST_REQUIRE(watermark != nullptr);
	BOOST_CHECK_EQUAL(watermark->texture, "SCShellUserInterface512_001.tga");
	BOOST_CHECK_EQUAL(watermark->texture_width, 512u);
	BOOST_CHECK_EQUAL(watermark->texture_height, 512u);
	BOOST_CHECK(watermark->rotated);
	BOOST_CHECK_EQUAL(watermark->width, 160u);
	BOOST_CHECK_EQUAL(watermark->height, 96u);
	BOOST_CHECK_CLOSE(watermark->uv.left, 391.0f / 512.0f, 0.0001f);
	BOOST_CHECK_CLOSE(watermark->uv.top, 163.0f / 512.0f, 0.0001f);
	BOOST_CHECK_CLOSE(watermark->uv.right, 487.0f / 512.0f, 0.0001f);
	BOOST_CHECK_CLOSE(watermark->uv.bottom, 323.0f / 512.0f, 0.0001f);
	BOOST_CHECK(catalog.Resolve("WatermarkGLA").rotated);

	const ImageDefinition *observer = catalog.Find("SSObserverUSA");
	BOOST_REQUIRE(observer != nullptr);
	BOOST_CHECK_EQUAL(observer->texture, "ControlButtonsPro_512_512.tga");
	BOOST_CHECK(!observer->rotated);
	BOOST_CHECK_EQUAL(observer->width, 90u);
	BOOST_CHECK_EQUAL(observer->height, 60u);
	BOOST_CHECK(!catalog.Resolve("SSObserverUSA").rotated);

	ImageCatalog authored;
	BOOST_REQUIRE(Parse_Mapped_Image_INI(
		"MappedImage Turned\n  Texture = t.tga\n  TextureWidth = 256\n  TextureHeight = 128\n"
		"  Coords = Left:0 Top:0 Right:20 Bottom:50\n  Status = ROTATED_90_CLOCKWISE\nEnd\n",
		authored));
	BOOST_REQUIRE(authored.Find("Turned") != nullptr);
	BOOST_CHECK_EQUAL(authored.Find("Turned")->width, 50u);
	BOOST_CHECK_EQUAL(authored.Find("Turned")->height, 20u);
}
