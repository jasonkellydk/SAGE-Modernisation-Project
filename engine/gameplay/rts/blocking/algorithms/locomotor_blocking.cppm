export module engine.gameplay.rts.blocking.algorithms.locomotor_blocking;
import std;

export import engine.gameplay.rts.blocking.components.blocked_state;
export import engine.gameplay.rts.blocking.components.block_contact;

// A ground unit's locomotor held back by what is in its way (AIUpdateInterface::doLocomotor, EA's Zero Hour source):
//   BeginBlockedTick: blocked by the last tick's collisions, one more tick blocked (else none); it is no longer blocked
//     until the collisions say so again. Returns whether it counts as blocked.
//   BlockedSpeed: along its route (POSITION_ON_PATH), the speed it may go: blocked and faster than the collisions allow,
//     it creeps (its bump limit down to that speed and 5% under each tick) and stays blocked; otherwise it is not blocked
//     and its bump limit, if it has one, grows back 5% a tick from at least a fifth of its speed, capping it.
//   EndBlockedTick: a tick not blocked counts it at most one tick blocked; the allowed speed is unbounded again.
export namespace engine::gameplay
{
inline bool BeginBlockedTick(BlockedState &state, BlockContact &contact) noexcept
{
	state.frames = contact.blocked != 0 ? state.frames + 1u : 0u;
	contact.blocked = 0;
	return state.frames > 0;
}

inline Engine::Math::Fixed BlockedSpeed(BlockedState &state, const BlockContact &contact, Engine::Math::Fixed speed, bool &blocked) noexcept
{
	using Engine::Math::Fixed;
	if (blocked && speed > contact.maxSpeed)
	{
		speed = contact.maxSpeed;
		if (state.bumpLimit > speed)
			state.bumpLimit = speed;
		state.bumpLimit = state.bumpLimit * Fixed::FromRatio(95, 100);
		return state.bumpLimit;
	}
	blocked = false;
	if (state.bumpLimit < FastAsPossible)
	{
		const Fixed floor = speed * Fixed::FromRatio(1, 5);
		if (state.bumpLimit < floor)
			state.bumpLimit = floor;
		state.bumpLimit = state.bumpLimit * Fixed::FromRatio(105, 100);
	}
	return speed > state.bumpLimit ? state.bumpLimit : speed;
}

inline void EndBlockedTick(BlockedState &state, BlockContact &contact, bool blocked) noexcept
{
	if (!blocked && state.frames > 1)
		state.frames = 1;
	contact.maxSpeed = FastAsPossible;
}

// What the last tick's collisions did to its count and whether it is stuck (processCollision's writes to
// m_blockedFrames and m_isBlockedAndStuck), taken in before its move decides whether to plan its route again.
inline void TakeBlockedContact(BlockedState &state, BlockContact &contact) noexcept
{
	if (contact.frames == block_frames::One || (contact.frames == block_frames::AtLeastOne && state.frames == 0))
		state.frames = 1;
	if (contact.stuck != 0)
		state.stuck = 1;
	contact.frames = block_frames::Keep;
	contact.stuck = 0;
}
}
