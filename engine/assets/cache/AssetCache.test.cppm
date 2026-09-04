module;

#define BOOST_TEST_MODULE GeneralsAssetsCacheTests

#include <boost/test/included/unit_test.hpp>

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

export module Assets.Tests.AssetCache;

import Assets.Adapters.W3D;
import Assets.Cache;
import Assets.Handles;
import Assets.Identity;
import Assets.Importers.Models;
import Assets.Models;
import Assets.States;

namespace
{

using Byte = std::byte;

void Append_U32(std::vector<Byte> &bytes, std::uint32_t value)
{
	bytes.push_back(static_cast<Byte>(value & 0xFF));
	bytes.push_back(static_cast<Byte>((value >> 8) & 0xFF));
	bytes.push_back(static_cast<Byte>((value >> 16) & 0xFF));
	bytes.push_back(static_cast<Byte>((value >> 24) & 0xFF));
}

void Append_F32(std::vector<Byte> &bytes, float value)
{
	Append_U32(bytes, std::bit_cast<std::uint32_t>(value));
}

void Write_U32(std::vector<Byte> &bytes, std::size_t offset, std::uint32_t value)
{
	bytes[offset + 0] = static_cast<Byte>(value & 0xFF);
	bytes[offset + 1] = static_cast<Byte>((value >> 8) & 0xFF);
	bytes[offset + 2] = static_cast<Byte>((value >> 16) & 0xFF);
	bytes[offset + 3] = static_cast<Byte>((value >> 24) & 0xFF);
}

void Write_F32(std::vector<Byte> &bytes, std::size_t offset, float value)
{
	Write_U32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

void Write_Fixed_String(std::vector<Byte> &bytes, std::size_t offset, std::size_t length, std::string_view value)
{
	for (std::size_t index = 0; index < length; ++index)
		bytes[offset + index] = index < value.size() ? static_cast<Byte>(value[index]) : Byte{0};
}

void Append_Vector3(std::vector<Byte> &bytes, float x, float y, float z)
{
	Append_F32(bytes, x);
	Append_F32(bytes, y);
	Append_F32(bytes, z);
}

void Append_Chunk(std::vector<Byte> &bytes, std::uint32_t id, const std::vector<Byte> &payload, bool children)
{
	Append_U32(bytes, id);
	Append_U32(bytes, static_cast<std::uint32_t>(payload.size()) | (children ? 0x80000000u : 0u));
	bytes.insert(bytes.end(), payload.begin(), payload.end());
}

std::vector<Byte> Make_Static_W3D()
{
	std::vector<Byte> header(116, Byte{0});
	Write_U32(header, 0, 0x00030000);
	Write_Fixed_String(header, 8, 16, "cache_triangle");
	Write_Fixed_String(header, 24, 16, "cache_container");
	Write_U32(header, 40, 1);
	Write_U32(header, 44, 3);
	Write_U32(header, 48, 0);
	Write_U32(header, 68, 0x00000007);
	Write_U32(header, 72, 0x00000001);
	Write_F32(header, 76, 0.0f);
	Write_F32(header, 80, 0.0f);
	Write_F32(header, 84, 0.0f);
	Write_F32(header, 88, 1.0f);
	Write_F32(header, 92, 1.0f);
	Write_F32(header, 96, 0.0f);
	Write_F32(header, 100, 0.333f);
	Write_F32(header, 104, 0.333f);
	Write_F32(header, 108, 0.0f);
	Write_F32(header, 112, 1.0f);

	std::vector<Byte> mesh;
	Append_Chunk(mesh, 0x1F, header, false);
	std::vector<Byte> vertices;
	Append_Vector3(vertices, 0.0f, 0.0f, 0.0f);
	Append_Vector3(vertices, 1.0f, 0.0f, 0.0f);
	Append_Vector3(vertices, 0.0f, 1.0f, 0.0f);
	Append_Chunk(mesh, 0x02, vertices, false);
	std::vector<Byte> triangles;
	Append_U32(triangles, 0);
	Append_U32(triangles, 1);
	Append_U32(triangles, 2);
	Append_U32(triangles, 0);
	Append_Vector3(triangles, 0.0f, 0.0f, 1.0f);
	Append_F32(triangles, 0.0f);
	Append_Chunk(mesh, 0x20, triangles, false);

	std::vector<Byte> source;
	Append_Chunk(source, 0x00, mesh, true);
	return source;
}

}

class DependencyModelAdapter final : public Assets::IModelAdapter
{
public:
	bool Can_Import(const Assets::AssetIdentity &identity, std::span<const std::byte>) const noexcept override
	{
		return identity.type == Assets::AssetType::Model && identity.canonical_name == "chain.model";
	}

	Assets::ModelImportResult Import(
		const Assets::AssetIdentity &identity,
		std::span<const std::byte>) const override
	{
		auto description = std::make_unique<Assets::ModelAssetDesc>();
		description->name = identity.canonical_name;
		description->source_format = "dependency-test";
		description->bounds = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
		description->vertices = {{{0.0f, 0.0f, 0.0f}}, {{1.0f, 0.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}};
		description->indices = {0, 1, 2};
		description->submeshes.push_back({0, 3, 0, "chain"});
		description->materials.push_back({"chain-material", "chain-texture.tga"});
		description->dependencies.push_back({Assets::AssetType::Material, "chain-material"});
		description->dependencies.push_back({Assets::AssetType::Texture, "chain-texture.tga"});
		return {std::move(description), {}};
	}
};

BOOST_AUTO_TEST_CASE(asset_cache_publishes_loading_then_ready)
{
	const auto gate = std::make_shared<std::promise<std::vector<Byte>>>();
	const std::shared_future<std::vector<Byte>> source_result = gate->get_future().share();
	Assets::AssetCache cache([source_result](const Assets::AssetIdentity &) { return source_result.get(); });
	BOOST_REQUIRE(cache.Register_Model_Adapter(std::make_shared<Assets::W3DAdapter>()));

	const Assets::ModelAssetHandle handle = cache.Request_Model("Models\\TEST.W3D");
	BOOST_REQUIRE(handle.Is_Valid());
	BOOST_CHECK(cache.Get_State(handle) == Assets::AssetState::Loading);
	BOOST_CHECK(cache.Try_Get_Model(handle) == nullptr);

	gate->set_value(Make_Static_W3D());
	cache.Wait(handle);
	BOOST_CHECK(cache.Get_State(handle) == Assets::AssetState::Ready);
	BOOST_REQUIRE(cache.Try_Get_Model(handle) != nullptr);
}

BOOST_AUTO_TEST_CASE(duplicate_requests_share_one_cached_load)
{
	std::atomic_int source_calls{0};
	const std::vector<Byte> source_bytes = Make_Static_W3D();
	Assets::AssetCache cache([&source_calls, source_bytes](const Assets::AssetIdentity &) {
		++source_calls;
		return source_bytes;
	});
	BOOST_REQUIRE(cache.Register_Model_Adapter(std::make_shared<Assets::W3DAdapter>()));

	const Assets::ModelAssetHandle first = cache.Request_Model("./Models/Unit.W3D");
	const Assets::ModelAssetHandle second = cache.Request_Model("models\\unit.w3d");
	BOOST_CHECK(first == second);
	cache.Wait(first);
	BOOST_CHECK(source_calls.load() == 1);
	BOOST_CHECK(cache.Model_Count() == 1);
}

BOOST_AUTO_TEST_CASE(failed_asset_load_is_cached_as_failed)
{
	Assets::AssetCache cache([](const Assets::AssetIdentity &) { return std::vector<Byte>{}; });
	BOOST_REQUIRE(cache.Register_Model_Adapter(std::make_shared<Assets::W3DAdapter>()));
	const Assets::ModelAssetHandle handle = cache.Request_Model("missing.w3d");
	cache.Wait(handle);

	BOOST_CHECK(cache.Get_State(handle) == Assets::AssetState::Failed);
	BOOST_CHECK(cache.Try_Get_Model(handle) == nullptr);
	BOOST_CHECK(!cache.Get_Error(handle).empty());
	BOOST_CHECK(cache.Request_Model("MISSING.W3D") == handle);
}

BOOST_AUTO_TEST_CASE(concurrent_duplicate_requests_share_one_loading_entry)
{
	std::atomic_int source_calls{0};
	const std::vector<Byte> source_bytes = Make_Static_W3D();
	Assets::AssetCache cache([&source_calls, source_bytes](const Assets::AssetIdentity &) {
		++source_calls;
		return source_bytes;
	});
	BOOST_REQUIRE(cache.Register_Model_Adapter(std::make_shared<Assets::W3DAdapter>()));

	std::vector<std::future<Assets::ModelAssetHandle>> requests;
	for (int index = 0; index < 8; ++index) {
		requests.push_back(std::async(std::launch::async, [&cache, index] {
			return cache.Request_Model(index % 2 == 0 ? "Models\\Concurrent.W3D" : "models/concurrent.w3d");
		}));
	}
	const Assets::ModelAssetHandle first = requests.front().get();
	for (std::size_t index = 1; index < requests.size(); ++index)
		BOOST_CHECK(requests[index].get() == first);
	cache.Wait(first);
	BOOST_CHECK(source_calls.load() == 1);
	BOOST_CHECK(cache.Model_Count() == 1);
}

BOOST_AUTO_TEST_CASE(model_waits_for_material_and_texture_dependencies)
{
	const auto texture_gate = std::make_shared<std::promise<std::vector<Byte>>>();
	const auto texture_started = std::make_shared<std::promise<void>>();
	const std::shared_future<void> started_future = texture_started->get_future().share();
	const std::shared_future<std::vector<Byte>> texture_result = texture_gate->get_future().share();
	Assets::AssetCache cache([texture_started, texture_result](const Assets::AssetIdentity &identity) {
		if (identity.type == Assets::AssetType::Texture) {
			try {
				texture_started->set_value();
			} catch (...) {
			}
			return texture_result.get();
		}
		return std::vector<Byte>{Byte{1}};
	});
	BOOST_REQUIRE(cache.Register_Model_Adapter(std::make_shared<DependencyModelAdapter>()));

	const Assets::TextureAssetHandle texture = cache.Request_Texture("CHAIN-TEXTURE.TGA");
	const Assets::MaterialAssetHandle material =
		cache.Request_Material({"chain-material", "chain-texture.tga"});
	const Assets::ModelAssetHandle model = cache.Request_Model("CHAIN.MODEL");
	BOOST_REQUIRE(texture.Is_Valid());
	BOOST_REQUIRE(material.Is_Valid());
	BOOST_REQUIRE(model.Is_Valid());
	started_future.wait();

	BOOST_CHECK(cache.Get_State(texture) == Assets::AssetState::Loading);
	BOOST_CHECK(cache.Get_State(material) == Assets::AssetState::Loading);
	BOOST_CHECK(cache.Get_State(model) == Assets::AssetState::Loading);
	BOOST_CHECK(cache.Try_Get_Model(model) == nullptr);

	texture_gate->set_value({Byte{7}});
	cache.Wait(model);
	BOOST_CHECK(cache.Get_State(texture) == Assets::AssetState::Ready);
	BOOST_CHECK(cache.Get_State(material) == Assets::AssetState::Ready);
	BOOST_CHECK(cache.Get_State(model) == Assets::AssetState::Ready);
	BOOST_REQUIRE(cache.Try_Get_Model(model) != nullptr);
	BOOST_REQUIRE_EQUAL(cache.Model_Material_Dependencies(model).size(), 1);
	BOOST_REQUIRE_EQUAL(cache.Model_Texture_Dependencies(model).size(), 1);
	BOOST_CHECK(cache.Model_Material_Dependencies(model)[0] == material);
	BOOST_REQUIRE_EQUAL(cache.Material_Texture_Dependencies(material).size(), 1);
	BOOST_CHECK(cache.Material_Texture_Dependencies(material)[0] == texture);
	BOOST_REQUIRE(cache.Try_Get_Material(material) != nullptr);
	BOOST_REQUIRE(cache.Try_Get_Texture(texture) != nullptr);
}

BOOST_AUTO_TEST_CASE(dependency_failure_propagates_to_model_and_material)
{
	Assets::AssetCache cache([](const Assets::AssetIdentity &identity) {
		if (identity.type == Assets::AssetType::Texture)
			return std::vector<Byte>{};
		return std::vector<Byte>{Byte{1}};
	});
	BOOST_REQUIRE(cache.Register_Model_Adapter(std::make_shared<DependencyModelAdapter>()));

	const Assets::TextureAssetHandle texture = cache.Request_Texture("chain-texture.tga");
	const Assets::MaterialAssetHandle material =
		cache.Request_Material({"chain-material", "chain-texture.tga"});
	const Assets::ModelAssetHandle model = cache.Request_Model("chain.model");
	cache.Wait(model);

	BOOST_CHECK(cache.Get_State(texture) == Assets::AssetState::Failed);
	BOOST_CHECK(cache.Get_State(material) == Assets::AssetState::Failed);
	BOOST_CHECK(cache.Get_State(model) == Assets::AssetState::Failed);
	BOOST_CHECK(cache.Get_Error(material).find("texture") != std::string::npos);
	BOOST_CHECK(cache.Get_Error(model).find("material") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(repeated_dependency_requests_reuse_cached_runtime_assets)
{
	std::atomic_int texture_source_calls{0};
	Assets::AssetCache cache([&texture_source_calls](const Assets::AssetIdentity &identity) {
		if (identity.type == Assets::AssetType::Texture)
			++texture_source_calls;
		return std::vector<Byte>{Byte{1}};
	});
	BOOST_REQUIRE(cache.Register_Model_Adapter(std::make_shared<DependencyModelAdapter>()));

	const Assets::TextureAssetHandle first_texture = cache.Request_Texture("textures\\CHAIN-TEXTURE.TGA");
	const Assets::TextureAssetHandle second_texture = cache.Request_Texture("textures/chain-texture.tga");
	BOOST_CHECK(first_texture == second_texture);
	cache.Wait(first_texture);
	BOOST_CHECK(texture_source_calls.load() == 1);

	const Assets::MaterialAssetHandle first_material =
		cache.Request_Material({"materials/chain-material", "textures/chain-texture.tga"});
	const Assets::MaterialAssetHandle second_material =
		cache.Request_Material({"materials\\chain-material", "textures\\chain-texture.tga"});
	BOOST_CHECK(first_material == second_material);
	cache.Wait(first_material);
	BOOST_CHECK(cache.Material_Count() == 1);
}

BOOST_AUTO_TEST_CASE(dependency_completion_is_deterministic)
{
	const auto load = [] {
		Assets::AssetCache cache([](const Assets::AssetIdentity &) { return std::vector<Byte>{Byte{1}}; });
		BOOST_REQUIRE(cache.Register_Model_Adapter(std::make_shared<DependencyModelAdapter>()));
		const Assets::ModelAssetHandle handle = cache.Request_Model("chain.model");
		cache.Wait(handle);
		BOOST_REQUIRE(cache.Get_State(handle) == Assets::AssetState::Ready);
		const Assets::ModelAsset *asset = cache.Try_Get_Model(handle);
		BOOST_REQUIRE(asset != nullptr);
		return std::vector<std::size_t>{asset->Vertices().size(), asset->Indices().size(), asset->Materials().size()};
	};

	const auto first = load();
	const auto second = load();
	BOOST_CHECK(first == second);
}

BOOST_AUTO_TEST_CASE(cache_destruction_waits_for_inflight_work)
{
	const auto source_started = std::make_shared<std::promise<void>>();
	const auto source_gate = std::make_shared<std::promise<std::vector<Byte>>>();
	const std::shared_future<void> started_future = source_started->get_future().share();
	const std::shared_future<std::vector<Byte>> gate_future = source_gate->get_future().share();
	std::unique_ptr<Assets::AssetCache> cache = std::make_unique<Assets::AssetCache>(
		[source_started, gate_future](const Assets::AssetIdentity &) {
			try {
				source_started->set_value();
			} catch (...) {
			}
			return gate_future.get();
		});
	BOOST_REQUIRE(cache->Register_Model_Adapter(std::make_shared<Assets::W3DAdapter>()));
	cache->Request_Model("destroy-safely.w3d");
	started_future.wait();

	std::future<void> destruction = std::async(
		std::launch::async,
		[owned_cache = std::move(cache)]() mutable { owned_cache.reset(); });
	BOOST_CHECK(destruction.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout);
	source_gate->set_value(Make_Static_W3D());
	destruction.wait();
}
