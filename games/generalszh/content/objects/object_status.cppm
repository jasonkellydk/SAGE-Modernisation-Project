export module games.generalszh.content.objects.object_status;
import std;

// Zero Hour's object status names (ObjectStatusMaskType), in the original's
// bit order: a StatusFlags bit per name. Die modules filter by them
// (RequiredStatus, ExemptStatus); the game sets the ones nothing else records.
export namespace generalszh::content
{
inline constexpr std::array<std::string_view, 45> ObjectStatusNames{"NONE", "DESTROYED", "CAN_ATTACK", "UNDER_CONSTRUCTION", "UNSELECTABLE",
	"NO_COLLISIONS", "NO_ATTACK", "AIRBORNE_TARGET", "PARACHUTING", "REPULSOR", "HIJACKED", "AFLAME", "BURNED", "WET", "IS_FIRING_WEAPON",
	"IS_BRAKING", "STEALTHED", "DETECTED", "CAN_STEALTH", "SOLD", "UNDERGOING_REPAIR", "RECONSTRUCTING", "MASKED", "IS_ATTACKING",
	"USING_ABILITY", "IS_AIMING_WEAPON", "NO_ATTACK_FROM_AI", "IGNORING_STEALTH", "IS_CARBOMB", "DECK_HEIGHT_OFFSET", "STATUS_RIDER1",
	"STATUS_RIDER2", "STATUS_RIDER3", "STATUS_RIDER4", "STATUS_RIDER5", "STATUS_RIDER6", "STATUS_RIDER7", "STATUS_RIDER8", "FAERIE_FIRE",
	"KILLING_SELF", "REASSIGN_PARKING", "BOOBY_TRAPPED", "IMMOBILE", "DISGUISED", "DEPLOYED"};

inline constexpr std::uint32_t NoStatus = 0xFFFFFFFFu;

// The bit of a status name (any case); NoStatus when unknown.
constexpr std::uint32_t ObjectStatusBit(std::string_view name) noexcept
{
	for (std::uint32_t index = 0; index < ObjectStatusNames.size(); ++index)
		if (std::ranges::equal(ObjectStatusNames[index], name, [](char a, char b) {
				return (a >= 'a' && a <= 'z' ? a - 32 : a) == (b >= 'a' && b <= 'z' ? b - 32 : b);
			}))
			return index;
	return NoStatus;
}

// A status list's mask (the names it sets; unknown names set nothing).
inline std::uint64_t ObjectStatusMask(std::span<const std::string_view> names) noexcept
{
	std::uint64_t mask = 0;
	for (const std::string_view name : names)
		if (const std::uint32_t bit = ObjectStatusBit(name); bit != NoStatus)
			mask |= std::uint64_t{1} << bit;
	return mask;
}
}
