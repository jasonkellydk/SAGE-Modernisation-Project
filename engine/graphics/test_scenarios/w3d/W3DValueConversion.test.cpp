#define BOOST_TEST_MODULE GraphicsW3DValueConversionTests

#include <boost/test/included/unit_test.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>

import Graphics.Backends.DX11;
import Graphics.RHI;
import Graphics.Scene.Props.Geometry;
import Graphics.Scene.Props.Material;
import Graphics.Scene.Props.Renderer;

#include "WW3D2/Light.h"
#include "WW3D2/W3DFile.h"
#include "WW3D2/W3DObsolete.h"
#include "WW3D2/VertMaterial.h"
#include "WWLib/RAMFILE.h"
#include "WWLib/chunkio.h"

#ifndef GRAPHICS_W3D_VALUE_CONVERSION_SHADER_DIRECTORY
#define GRAPHICS_W3D_VALUE_CONVERSION_SHADER_DIRECTORY "."
#endif

namespace
{

constexpr float kFloatTolerance = 0.0001f;

struct SerializedW3D final
{
	std::array<std::byte, 8192> bytes{};
	std::size_t size = 0;
};

float Byte_To_Float(std::uint8_t value) noexcept
{
	return static_cast<float>(value) / 255.0f;
}

float Byte_Product(std::uint8_t color, std::uint8_t coefficient) noexcept
{
	return Byte_To_Float(color) * Byte_To_Float(coefficient);
}

void Check_Vector(const Vector3 &actual, const Vector3 &expected)
{
	BOOST_CHECK_SMALL(actual.X - expected.X, kFloatTolerance);
	BOOST_CHECK_SMALL(actual.Y - expected.Y, kFloatTolerance);
	BOOST_CHECK_SMALL(actual.Z - expected.Z, kFloatTolerance);
}

W3dMaterial3Struct Test_Material3()
{
	W3dMaterial3Struct material;
	std::memset(&material, 0, sizeof(material));
	material.DiffuseColor.Set(static_cast<uint8>(128), static_cast<uint8>(127), static_cast<uint8>(254));
	material.DiffuseCoefficients.Set(static_cast<uint8>(128), static_cast<uint8>(127), static_cast<uint8>(0));
	material.SpecularColor.Set(static_cast<uint8>(255), static_cast<uint8>(254), static_cast<uint8>(0));
	material.SpecularCoefficients.Set(static_cast<uint8>(1), static_cast<uint8>(0), static_cast<uint8>(255));
	material.EmissiveCoefficients.Set(static_cast<uint8>(1), static_cast<uint8>(0), static_cast<uint8>(127));
	material.AmbientCoefficients.Set(static_cast<uint8>(128), static_cast<uint8>(1), static_cast<uint8>(0));
	material.Shininess = 17.0f;
	material.Opacity = 0.625f;
	return material;
}

void Configure_Test_Light(LightClass &light)
{
	light.Set_Intensity(1.0f);
	light.Set_Ambient(Vector3(0.5f, 0.25f, 0.75f));
	light.Set_Diffuse(Vector3(0.25f, 0.5f, 0.75f));
	light.Set_Specular(Vector3(0.75f, 0.5f, 0.25f));
	light.Enable_Shadows(true);
	light.Set_Spot_Direction(Vector3(0.0f, 0.0f, -1.0f));
	light.Set_Spot_Angle(0.75f);
	light.Set_Spot_Exponent(2.5f);
	light.Set_Near_Attenuation_Range(2.0, 5.0);
	light.Set_Far_Attenuation_Range(10.0, 20.0);
	light.Set_Flag(LightClass::FAR_ATTENUATION, true);
}

bool Serialize_Light(LightClass &light, SerializedW3D &serialized)
{
	RAMFileClass file(serialized.bytes.data(), static_cast<int>(serialized.bytes.size()));
	if (!file.Open(FileClass::WRITE))
		return false;

	ChunkSaveClass save(&file);
	const bool saved = light.Save_W3D(save) == WW3D_ERROR_OK;
	file.Close();
	serialized.size = static_cast<std::size_t>(file.Size());
	return saved && serialized.size != 0;
}

bool Write_Light_Chunk(ChunkSaveClass &save, std::uint32_t id, const void *data, std::size_t size)
{
	return save.Begin_Chunk(id)
		&& save.Write(data, static_cast<uint32>(size)) == size
		&& save.End_Chunk();
}

bool Write_Light_Load_Fixture(
	const W3dLightStruct &light_info,
	const W3dSpotLightStruct &spot_info,
	const W3dLightAttenuationStruct &near_attenuation,
	const W3dLightAttenuationStruct &far_attenuation,
	SerializedW3D &serialized)
{
	RAMFileClass file(serialized.bytes.data(), static_cast<int>(serialized.bytes.size()));
	if (!file.Open(FileClass::WRITE))
		return false;

	ChunkSaveClass save(&file);
	bool written = save.Begin_Chunk(W3D_CHUNK_LIGHT);
	if (written)
		written = Write_Light_Chunk(save, W3D_CHUNK_LIGHT_INFO, &light_info, sizeof(light_info));
	if (written)
		written = Write_Light_Chunk(save, W3D_CHUNK_SPOT_LIGHT_INFO, &spot_info, sizeof(spot_info));
	if (written)
		written = Write_Light_Chunk(save, W3D_CHUNK_NEAR_ATTENUATION, &near_attenuation, sizeof(near_attenuation));
	if (written)
		written = Write_Light_Chunk(save, W3D_CHUNK_FAR_ATTENUATION, &far_attenuation, sizeof(far_attenuation));
	if (written)
		written = save.End_Chunk();

	file.Close();
	serialized.size = static_cast<std::size_t>(file.Size());
	return written && serialized.size != 0;
}

bool Read_Light_Info(SerializedW3D &serialized, W3dLightStruct &light_info)
{
	RAMFileClass file(serialized.bytes.data(), static_cast<int>(serialized.size));
	if (!file.Open(FileClass::READ))
		return false;

	ChunkLoadClass load(&file);
	bool read = load.Open_Chunk() && load.Cur_Chunk_ID() == W3D_CHUNK_LIGHT;
	if (read)
		read = load.Open_Chunk() && load.Cur_Chunk_ID() == W3D_CHUNK_LIGHT_INFO;
	if (read)
		read = load.Cur_Chunk_Length() == sizeof(light_info)
			&& load.Read(&light_info, sizeof(light_info)) == sizeof(light_info);
	if (load.Cur_Chunk_Depth() > 1)
		load.Close_Chunk();
	if (load.Cur_Chunk_Depth() > 0)
		load.Close_Chunk();
	file.Close();
	return read;
}

bool Load_Light(SerializedW3D &serialized, LightClass &light)
{
	RAMFileClass file(serialized.bytes.data(), static_cast<int>(serialized.size));
	if (!file.Open(FileClass::READ))
		return false;

	ChunkLoadClass load(&file);
	bool loaded = load.Open_Chunk() && load.Cur_Chunk_ID() == W3D_CHUNK_LIGHT;
	if (loaded)
		loaded = light.Load_W3D(load) == WW3D_ERROR_OK;
	if (loaded)
		loaded = load.Close_Chunk();
	file.Close();
	return loaded;
}

void Check_Light_Color(const LightClass &light, const Vector3 &expected_ambient,
	const Vector3 &expected_diffuse, const Vector3 &expected_specular)
{
	Vector3 ambient;
	Vector3 diffuse;
	Vector3 specular;
	light.Get_Ambient(&ambient);
	light.Get_Diffuse(&diffuse);
	light.Get_Specular(&specular);
	Check_Vector(ambient, expected_ambient);
	Check_Vector(diffuse, expected_diffuse);
	Check_Vector(specular, expected_specular);
}

}

BOOST_AUTO_TEST_CASE(material3_conversion_preserves_channel_products_and_scalar_values)
{
	const W3dMaterial3Struct authored = Test_Material3();
	VertexMaterialClass material;
	material.Init_From_Material3(authored);

	Vector3 diffuse;
	Vector3 specular;
	Vector3 emissive;
	Vector3 ambient;
	material.Get_Diffuse(&diffuse);
	material.Get_Specular(&specular);
	material.Get_Emissive(&emissive);
	material.Get_Ambient(&ambient);
	Check_Vector(diffuse, Vector3(
		Byte_Product(128, 128), Byte_Product(127, 127), Byte_Product(254, 0)));
	Check_Vector(specular, Vector3(
		Byte_Product(255, 1), Byte_Product(254, 0), Byte_Product(0, 255)));
	Check_Vector(emissive, Vector3(
		Byte_To_Float(1), Byte_To_Float(0), Byte_To_Float(127)));
	Check_Vector(ambient, Vector3(
		Byte_To_Float(128), Byte_To_Float(1), Byte_To_Float(0)));
	BOOST_CHECK_SMALL(material.Get_Shininess() - 17.0f, kFloatTolerance);
	BOOST_CHECK_SMALL(material.Get_Opacity() - 0.625f, kFloatTolerance);

	const Graphics::PropMaterial &converted = material.Get_Material_Parameters();
	for (unsigned channel = 0; channel < 3; ++channel) {
		BOOST_CHECK_SMALL(converted.diffuse[channel] - diffuse[channel], kFloatTolerance);
		BOOST_CHECK_SMALL(converted.specular[channel] - specular[channel], kFloatTolerance);
		BOOST_CHECK_SMALL(converted.emissive[channel] - emissive[channel], kFloatTolerance);
		BOOST_CHECK_SMALL(converted.ambient[channel] - ambient[channel], kFloatTolerance);
	}
	BOOST_CHECK_SMALL(converted.shininess - 17.0f, kFloatTolerance);
	BOOST_CHECK_SMALL(converted.opacity - 0.625f, kFloatTolerance);
}

BOOST_AUTO_TEST_CASE(light_w3d_roundtrip_preserves_encoded_colors_and_authored_parameters)
{
	LightClass source(LightClass::SPOT);
	Configure_Test_Light(source);

	SerializedW3D saved;
	BOOST_REQUIRE(Serialize_Light(source, saved));

	W3dLightStruct light_info;
	std::memset(&light_info, 0, sizeof(light_info));
	BOOST_REQUIRE(Read_Light_Info(saved, light_info));
	BOOST_CHECK_EQUAL(light_info.Attributes,
		static_cast<uint32>(W3D_LIGHT_ATTRIBUTE_SPOT | W3D_LIGHT_ATTRIBUTE_CAST_SHADOWS));
	BOOST_CHECK_EQUAL(static_cast<unsigned>(light_info.Ambient.R), 127u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(light_info.Ambient.G), 63u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(light_info.Ambient.B), 191u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(light_info.Ambient.pad), 0u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(light_info.Diffuse.R), 63u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(light_info.Diffuse.G), 127u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(light_info.Diffuse.B), 191u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(light_info.Diffuse.pad), 0u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(light_info.Specular.R), 191u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(light_info.Specular.G), 127u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(light_info.Specular.B), 63u);
	BOOST_CHECK_EQUAL(static_cast<unsigned>(light_info.Specular.pad), 0u);
	BOOST_CHECK_SMALL(light_info.Intensity - 1.0f, kFloatTolerance);

	LightClass loaded(LightClass::POINT);
	BOOST_REQUIRE(Load_Light(saved, loaded));
	BOOST_CHECK(loaded.Get_Type() == LightClass::SPOT);
	BOOST_CHECK(loaded.Are_Shadows_Enabled());
	BOOST_CHECK_SMALL(loaded.Get_Intensity() - 1.0f, kFloatTolerance);
	Check_Light_Color(loaded,
		Vector3(Byte_To_Float(127), Byte_To_Float(63), Byte_To_Float(191)),
		Vector3(Byte_To_Float(63), Byte_To_Float(127), Byte_To_Float(191)),
		Vector3(Byte_To_Float(191), Byte_To_Float(127), Byte_To_Float(63)));
	Vector3 direction;
	loaded.Get_Spot_Direction(direction);
	Check_Vector(direction, Vector3(0.0f, 0.0f, -1.0f));
	BOOST_CHECK_SMALL(loaded.Get_Spot_Angle() - 0.75f, kFloatTolerance);
	BOOST_CHECK_SMALL(loaded.Get_Spot_Exponent() - 2.5f, kFloatTolerance);
	BOOST_CHECK(loaded.Get_Flag(LightClass::FAR_ATTENUATION));
	double far_start = 0.0;
	double far_end = 0.0;
	loaded.Get_Far_Attenuation_Range(far_start, far_end);
	BOOST_CHECK_SMALL(static_cast<float>(far_start - 10.0), kFloatTolerance);
	BOOST_CHECK_SMALL(static_cast<float>(far_end - 20.0), kFloatTolerance);

	W3dSpotLightStruct spot_info;
	std::memset(&spot_info, 0, sizeof(spot_info));
	spot_info.SpotDirection.X = 0.0f;
	spot_info.SpotDirection.Y = 0.0f;
	spot_info.SpotDirection.Z = -1.0f;
	spot_info.SpotAngle = 0.75f;
	spot_info.SpotExponent = 2.5f;
	W3dLightAttenuationStruct near_attenuation{2.0f, 5.0f};
	W3dLightAttenuationStruct far_attenuation{10.0f, 20.0f};
	SerializedW3D fixture;
	BOOST_REQUIRE(Write_Light_Load_Fixture(
		light_info, spot_info, near_attenuation, far_attenuation, fixture));
	LightClass loaded_fixture(LightClass::POINT);
	BOOST_REQUIRE(Load_Light(fixture, loaded_fixture));
	double near_start = 0.0;
	double near_end = 0.0;
	loaded_fixture.Get_Near_Attenuation_Range(near_start, near_end);
	loaded_fixture.Get_Far_Attenuation_Range(far_start, far_end);
	BOOST_CHECK_SMALL(static_cast<float>(near_start - 2.0), kFloatTolerance);
	BOOST_CHECK_SMALL(static_cast<float>(near_end - 5.0), kFloatTolerance);
	BOOST_CHECK_SMALL(static_cast<float>(far_start - 10.0), kFloatTolerance);
	BOOST_CHECK_SMALL(static_cast<float>(far_end - 20.0), kFloatTolerance);
}

BOOST_AUTO_TEST_CASE(converted_w3d_material_and_light_values_reach_prop_pixels)
{
	VertexMaterialClass legacy_material;
	legacy_material.Init_From_Material3(Test_Material3());
	legacy_material.Set_Lighting(true);
	const Graphics::PropMaterial converted_material = legacy_material.Get_Material_Parameters();

	LightClass source(LightClass::SPOT);
	Configure_Test_Light(source);
	SerializedW3D saved_light;
	BOOST_REQUIRE(Serialize_Light(source, saved_light));
	LightClass loaded_light(LightClass::POINT);
	BOOST_REQUIRE(Load_Light(saved_light, loaded_light));

	Vector3 ambient;
	Vector3 diffuse;
	Vector3 specular;
	Vector3 direction;
	loaded_light.Get_Ambient(&ambient);
	loaded_light.Get_Diffuse(&diffuse);
	loaded_light.Get_Specular(&specular);
	loaded_light.Get_Spot_Direction(direction);
	double attenuation_start = 0.0;
	double attenuation_end = 0.0;
	loaded_light.Get_Far_Attenuation_Range(attenuation_start, attenuation_end);

	Graphics::DX11Device device({true});
	BOOST_REQUIRE(device.Is_Valid());
	Graphics::PropRenderer renderer;
	BOOST_REQUIRE(renderer.Initialize(device,
		std::filesystem::path(GRAPHICS_W3D_VALUE_CONVERSION_SHADER_DIRECTORY)));
	const auto target = device.Create_Texture({
		8, 8, 1, Graphics::RHITextureFormat::RGBA8_UNorm,
		static_cast<std::uint32_t>(Graphics::RHITextureUsage::RenderTarget)});
	const auto depth = device.Create_Texture({
		8, 8, 1, Graphics::RHITextureFormat::D32_Float,
		static_cast<std::uint32_t>(Graphics::RHITextureUsage::DepthStencil)});
	BOOST_REQUIRE(target.Is_Valid());
	BOOST_REQUIRE(depth.Is_Valid());

	std::array<Graphics::PropVertex, 4> vertices{};
	vertices[0].position = {-1.0f, -1.0f, 0.5f};
	vertices[1].position = {1.0f, -1.0f, 0.5f};
	vertices[2].position = {1.0f, 1.0f, 0.5f};
	vertices[3].position = {-1.0f, 1.0f, 0.5f};
	for (auto &vertex : vertices) {
		vertex.normal = {0.0f, 0.0f, 1.0f};
		Graphics::Apply_Prop_Material(vertex, converted_material);
	}
	const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
	const auto mesh = renderer.Create_Mesh(vertices, indices);
	BOOST_REQUIRE(mesh.Is_Valid());

	Graphics::PropParameters parameters;
	parameters.view_projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	parameters.camera_position = {0.0f, 0.0f, 5.0f, 1.0f};
	parameters.textured = 0.0f;
	parameters.secondary_gradient = 1.0f;
	parameters.light_direction[0] = {direction.X, direction.Y, direction.Z, 1.0f};
	parameters.light_diffuse[0] = {diffuse.X, diffuse.Y, diffuse.Z, 0.0f};
	parameters.light_specular[0] = {specular.X, specular.Y, specular.Z, 0.0f};
	parameters.light_ambient[0] = {ambient.X, ambient.Y, ambient.Z, 0.0f};
	parameters.light_position[0] = {0.0f, 0.0f, 10.0f,
		loaded_light.Get_Type() == LightClass::SPOT ? 2.0f : 0.0f};
	parameters.light_attenuation[0] = {
		1.0f, 0.0f, 0.0f, static_cast<float>(attenuation_end)};
	parameters.light_spot[0] = {
		loaded_light.Get_Spot_Exponent(), 0.5f * loaded_light.Get_Spot_Angle(),
		loaded_light.Get_Spot_Angle(), 0.0f};

	Graphics::PropStyle style;
	style.blend = Graphics::RHIBlendMode::Disabled;
	style.depth_test = false;
	style.depth_write = false;
	style.cull = Graphics::RHICullMode::None;
	auto &commands = device.Immediate_Command_List();
	BOOST_REQUIRE(commands.Set_Render_Targets(target, depth));
	BOOST_REQUIRE(commands.Set_Viewport({0, 0, 8, 8}));
	BOOST_REQUIRE(commands.Clear({0, 0, 0, 0}, 1.0f));
	BOOST_REQUIRE(renderer.Draw(commands, mesh, style, parameters, {}));

	std::array<std::byte, 8 * 8 * 4> pixels{};
	BOOST_REQUIRE(device.Readback_Texture(target, pixels, 8 * 4));
	const std::size_t center = (4 * 8 + 4) * 4;
	const std::array<int, 4> expected{81, 32, 127, 159};
	for (unsigned channel = 0; channel < expected.size(); ++channel)
		BOOST_CHECK_SMALL(std::to_integer<int>(pixels[center + channel]) - expected[channel], 2);

	renderer.Destroy_Mesh(mesh);
	renderer.Shutdown();
	device.Destroy_Texture(target);
	device.Destroy_Texture(depth);
}
