export module games.generalszh.presentation.interaction.resources.build_placement;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// A structure being placed for a builder (the original's InGameUI build placement: m_pendingPlaceType,
// m_pendingPlaceSourceObjectID, the placement anchor and angle): what and for whom, where the ghost is and which way
// it faces, whether the place is anchored (the button held: its facing follows the mouse), and whether it may be built
// there (the host asks the game each frame: BuildAssistant::isLocationLegalToBuild).
export namespace generalszh::presentation
{
struct BuildPlacement
{
	bool active{false};
	std::string structure;
	std::uint32_t definition{0};
	ecs::Entity builder;
	std::array<float, 3> at{};
	float facing{0.0f}; // radians
	bool anchored{false};
	std::array<float, 2> anchorScreen{};
	bool legal{false};
	bool onGround{false}; // the pointer is over the ground
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::BuildPlacement>
{
	static constexpr std::string_view StableName = "generalszh.presentation.build_placement";
};
}
