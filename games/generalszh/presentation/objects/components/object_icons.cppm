export module games.generalszh.presentation.objects.components.object_icons;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

// The animated icons drawn over an object (Drawable's DrawableIconInfo: an Anim2D per icon, made when the icon first
// shows and freed when it is killed): for each, the presentation clock when its animation was made (below 0: none),
// and those drawn this frame (bits by ObjectIcon; an animation may be kept undrawn, as ENTHUSIASTIC's under SUBLIMINAL),
// and each one's client random draw when it was made (Anim2D::Anim2D's randomizeCurrentFrame for an Animation2D with
// RandomizeStartFrame: Anim2DTemplate::StartImage).
// A side table on the simulation's own entities, kept by ObjectIconSystem.
export namespace generalszh::presentation
{
enum class ObjectIcon : std::uint8_t
{
	Disabled,     // ICON_DISABLED
	Enthusiastic, // ICON_ENTHUSIASTIC
	Subliminal,   // ICON_ENTHUSIASTIC_SUBLIMINAL
	CarBomb,      // ICON_CARBOMB
	DefaultHeal,  // ICON_DEFAULT_HEAL
	StructureHeal, // ICON_STRUCTURE_HEAL
	VehicleHeal,  // ICON_VEHICLE_HEAL
};

inline constexpr std::size_t ObjectIconCount = 7;

// TheDrawableIconNames: each icon's Animation2D.
inline constexpr std::array<std::string_view, ObjectIconCount> ObjectIconAnimations{"Disabled", "Enthusiastic", "Subliminal", "CarBomb", "DefaultHeal",
	"StructureHeal", "VehicleHeal"};

struct ObjectIcons
{
	std::array<double, ObjectIconCount> since{-1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0};
	std::array<std::uint32_t, ObjectIconCount> roll{};
	std::uint8_t drawn{0};

	bool Made(ObjectIcon icon) const noexcept { return since[static_cast<std::size_t>(icon)] >= 0.0; }
	bool Drawn(ObjectIcon icon) const noexcept { return (drawn >> static_cast<std::size_t>(icon) & 1u) != 0; }
};

// A script's emoticon (NAMED_SET_EMOTICON / TEAM_SET_EMOTICON: Drawable::setEmoticon, its ICON_EMOTICON Anim2D and
// m_keepTillFrame): the Animation2D it shows, the presentation clock when it was made and its random draw, and the logic
// frame it shows through (until: ~0, for good). A side table on the simulation's own entities.
struct ObjectEmoticon
{
	std::string animation;
	double since{0.0};
	std::uint32_t roll{0};
	std::uint64_t until{0};
};

// The client random draw an icon's animation takes when it is made (GameClientRandomValue's randomValue: the original's
// client stream is never synchronised, so any uniform draw does): a SplitMix64 mix of the object, the icon and the
// presentation clock then, so the chunked icon system draws it without a shared stream.
inline std::uint32_t IconRoll(ecs::Entity entity, ObjectIcon icon, double clock) noexcept
{
	std::uint64_t value = (std::uint64_t{entity.index} << 32 | entity.generation) ^ std::bit_cast<std::uint64_t>(clock) ^
		(static_cast<std::uint64_t>(icon) + 1) * 0x9E3779B97F4A7C15ull;
	value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
	value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
	return static_cast<std::uint32_t>((value ^ (value >> 31)) >> 32);
}
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::ObjectEmoticon>
{
	static constexpr std::string_view StableName = "generalszh.presentation.object_emoticon";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};

template<>
struct ComponentTraits<generalszh::presentation::ObjectIcons>
{
	static constexpr std::string_view StableName = "generalszh.presentation.object_icons";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
