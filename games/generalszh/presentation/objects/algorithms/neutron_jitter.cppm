export module games.generalszh.presentation.objects.algorithms.neutron_jitter;
import std;

// A superweapon missile's launch shake (NeutronMissileUpdate::doAttack, SpecialJitterDistance): only its drawing moves.
export namespace generalszh::presentation
{
// Each attack tick in its special speed climb (launched `elapsed` ticks ago, 0 < elapsed < SpecialSpeedTime) its drawable's
// instance transform is set off along its own sideways (y) and up (z) axes by a random [-1, 1] of (1 - elapsed / SpecialSpeedTime)
// SpecialJitterDistance each; the tick that ends the climb clears it. The original draws its numbers from the logic's random
// values but says they need not be synced: here the tick's two numbers come from `roll` (16 bits each). Its axes are those of
// its transform that tick: forward its heading, sideways left of its facing (radians), up the forward crossed with it.
inline std::array<float, 3> NeutronJitter(float jitter, std::uint64_t specialTicks, std::uint64_t elapsed, const std::array<float, 3> &forward,
	float facing, std::uint32_t roll) noexcept
{
	if (jitter <= 0.0f || specialTicks == 0 || elapsed == 0 || elapsed >= specialTicks)
		return {};
	const float amplitude = (1.0f - static_cast<float>(elapsed) / static_cast<float>(specialTicks)) * jitter;
	const float y = (static_cast<float>(roll & 0xFFFFu) / 32767.5f - 1.0f) * amplitude;
	const float z = (static_cast<float>(roll >> 16) / 32767.5f - 1.0f) * amplitude;
	const std::array<float, 3> side{-std::sin(facing), std::cos(facing), 0.0f};
	const std::array<float, 3> up{forward[1] * side[2] - forward[2] * side[1], forward[2] * side[0] - forward[0] * side[2],
		forward[0] * side[1] - forward[1] * side[0]};
	return {side[0] * y + up[0] * z, side[1] * y + up[1] * z, side[2] * y + up[2] * z};
}
}
