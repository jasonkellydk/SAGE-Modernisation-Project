export module games.generalszh.presentation.objects.components.object_icons;
import std;

export import engine.ecs.core.component_registry;

// The animated icons drawn over an object (Drawable's DrawableIconInfo: an Anim2D per icon, made when the icon first
// shows and freed when it is killed): for each, the presentation clock when its animation was made (below 0: none),
// and those drawn this frame (bits by ObjectIcon; an animation may be kept undrawn, as ENTHUSIASTIC's under SUBLIMINAL).
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
	std::uint8_t drawn{0};

	bool Made(ObjectIcon icon) const noexcept { return since[static_cast<std::size_t>(icon)] >= 0.0; }
	bool Drawn(ObjectIcon icon) const noexcept { return (drawn >> static_cast<std::size_t>(icon) & 1u) != 0; }
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::ObjectIcons>
{
	static constexpr std::string_view StableName = "generalszh.presentation.object_icons";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}
