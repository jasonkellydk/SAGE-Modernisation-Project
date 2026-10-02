export module games.generalszh.presentation.objects.resources.bridge_art;
import std;

import engine.ecs.system.system;

// The map-drawn bridges' art (W3DBridgeBuffer / W3DBridge): per Roads.ini Bridge (in the bridge catalog's order) its
// BridgeScale and, per body damage state (PRISTINE, DAMAGED, REALLYDAMAGED, RUBBLE), the model W3DBridge::load reads for
// it: its BRIDGE_LEFT, BRIDGE_SPAN and BRIDGE_RIGHT meshes, each with its sub-object's transform at rest, and the
// Roads.ini texture drawn on all of them. `loaded` is load's answer: false when the model is not there or has no
// BRIDGE_LEFT mesh (the bridge then cannot show that state). Filled once from the content between ticks; frames only
// read it. And the bridges as drawn this frame (BridgeViews): each its template, the state whose model it shows (the
// one its damage state asks for, or the one before when that one cannot load), its two ends and its scale; `version`
// changes whenever any of that does, so the renderer rebuilds its geometry only then (W3DBridgeBuffer::drawBridges'
// `changed`).
export namespace generalszh::presentation
{
// BodyDamageType's count.
inline constexpr std::size_t BridgeDamageStates = 4;
// A bridge whose shown model could not load at all (both loads failed): nothing drawn.
inline constexpr std::uint8_t NoBridgeModel = 0xFF;

// One mesh of a bridge model: its vertices (positions, normals, the first texture stage's coordinates) and triangles
// as the W3D file holds them, and its sub-object's transform at rest (row-major 3x4; identity when no sub-object of
// the model's hierarchy names it).
struct BridgeMesh
{
	std::array<float, 12> rest{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
	std::vector<std::array<float, 3>> positions;
	std::vector<std::array<float, 3>> normals;
	std::vector<std::array<float, 2>> uvs;
	std::vector<std::array<std::uint32_t, 3>> triangles;
};

struct BridgeModel
{
	bool loaded{false};
	std::optional<BridgeMesh> left;  // BRIDGE_LEFT
	std::optional<BridgeMesh> span;  // BRIDGE_SPAN
	std::optional<BridgeMesh> right; // BRIDGE_RIGHT
	std::string texture;             // Texture, TextureDamaged, TextureReallyDamaged, TextureBroken
};

struct BridgeKind
{
	float scale{1.0f}; // BridgeScale
	std::array<BridgeModel, BridgeDamageStates> states;
};

struct BridgeArt
{
	std::vector<BridgeKind> kinds; // by bridge template (the bridge catalog's order)

	const BridgeKind *Kind(std::uint32_t bridgeTemplate) const noexcept
	{
		return bridgeTemplate < kinds.size() ? &kinds[bridgeTemplate] : nullptr;
	}
};

struct BridgeView
{
	std::uint32_t bridgeTemplate{0};
	std::uint8_t model{NoBridgeModel}; // the damage state whose model shows
	std::array<float, 3> from{};
	std::array<float, 3> to{};
	float scale{1.0f};

	bool operator==(const BridgeView &) const = default;
};

struct BridgeViews
{
	std::vector<BridgeView> bridges;
	std::vector<BridgeView> next; // the system's scratch for this frame's list
	std::uint64_t version{0};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::BridgeArt>
{
	static constexpr std::string_view StableName = "generalszh.presentation.bridge_art";
};
template<>
struct ResourceTraits<generalszh::presentation::BridgeViews>
{
	static constexpr std::string_view StableName = "generalszh.presentation.bridge_views";
};
}
