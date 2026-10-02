export module games.generalszh.presentation.objects.algorithms.tree_bending;
import std;

export import games.generalszh.presentation.objects.components.tree_bend;
export import games.generalszh.presentation.objects.resources.look_catalog;

// W3DTreeBuffer's toppling and pushing aside, for the map trees:
//   ToppleTree (applyTopplingForce): a standing tree starts falling away from
//   the crusher at the minimum topple speed (the crusher gives none), its
//   angular speed and acceleration that speed's initial shares; it plays its
//   topple FX where it stands;
//   PushTreeAside (pushAsideTree): a standing tree not already leaning leans
//   out to the side of the pusher's path it is on (unless the same pusher
//   pushed it within the last 3 ticks), over MoveOutwardTime;
//   StepTreeBend (prepareFrame, by this frame's logic frames): a falling tree
//   turns (Rx(-a * dy) then Ry(a * dx) on top of its fall, a its speed times
//   the frames, up to just short of flat); reaching it, it bounces back at its
//   bounce share (a bounce FX near its top when fast enough) or stops when
//   that is under 0.01; else it speeds up by its acceleration. A tree down
//   that dies when toppled sinks SinkDistance over SinkTime, then is gone. A
//   leaning tree leans out then back (MoveInwardTime) to upright;
//   BentTree: its world matrix (row-major, scale in) bent: toppled about its
//   base, or leaning by its model height times the lean.
export namespace generalszh::presentation
{
inline constexpr float TreeAngularLimit = std::numbers::pi_v<float> / 2.0f - std::numbers::pi_v<float> / 64.0f;
inline constexpr float TreeRadiusApprox = 7.0f; // TREE_RADIUS_APPROX

namespace tree_bending_detail
{
// rotation = turn * rotation (Pre_Apply_Rotation: the turn in the world, the base kept).
inline void Turn(std::array<float, 9> &rotation, const std::array<float, 9> &turn) noexcept
{
	std::array<float, 9> product{};
	for (std::size_t row = 0; row < 3; ++row)
		for (std::size_t column = 0; column < 3; ++column)
			for (std::size_t k = 0; k < 3; ++k)
				product[row * 3 + column] += turn[row * 3 + k] * rotation[k * 3 + column];
	rotation = product;
}

inline std::array<float, 9> RotationX(float radians) noexcept
{
	const float c = std::cos(radians), s = std::sin(radians);
	return {1, 0, 0, 0, c, -s, 0, s, c};
}

inline std::array<float, 9> RotationY(float radians) noexcept
{
	const float c = std::cos(radians), s = std::sin(radians);
	return {c, 0, s, 0, 1, 0, -s, 0, c};
}

inline void Fall(TreeBend &bend, float angle) noexcept
{
	Turn(bend.rotation, RotationX(-angle * bend.direction[1]));
	Turn(bend.rotation, RotationY(angle * bend.direction[0]));
}
}

// Returns whether it started falling (its topple FX then plays where it stands).
inline bool ToppleTree(TreeBend &bend, const TreeMotion &motion, std::array<float, 2> away) noexcept
{
	if (bend.state != tree_bend_state::Upright)
		return false;
	const float toppleSpeed = std::max(0.0f, motion.minimumToppleSpeed);
	const float length = std::sqrt(away[0] * away[0] + away[1] * away[1]);
	bend.direction = length > 0.0f ? std::array<float, 2>{away[0] / length, away[1] / length} : std::array<float, 2>{0.0f, 0.0f};
	bend.accumulation = 0.0f;
	bend.angularVelocity = toppleSpeed * motion.initialVelocity;
	bend.angularAcceleration = toppleSpeed * motion.initialAcceleration;
	bend.state = tree_bend_state::Falling;
	bend.rotation = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	return true;
}

// `from`: the tree less the pusher's position; `heading`: the pusher's direction (unit, 2D).
inline void PushTreeAside(TreeBend &bend, const TreeMotion &motion, std::array<float, 2> from, std::array<float, 2> heading, std::uint64_t pusher,
	std::uint32_t tick) noexcept
{
	const std::uint32_t last = bend.lastPushTick;
	bend.lastPushTick = tick;
	if (bend.pushSource == pusher && tick - last < 3)
		return;
	if (bend.pushAside != 0.0f)
		return;
	bend.pushSource = pusher;
	if (heading[0] * from[1] - heading[1] * from[0] > 0.0f)
	{
		bend.pushCos = -heading[1];
		bend.pushSin = heading[0];
	}
	else
	{
		bend.pushCos = heading[1];
		bend.pushSin = -heading[0];
	}
	bend.pushDelta = 1.0f / motion.framesToMoveOutward;
}

// Steps it by `frames` logic frames. Returns whether it bounced hard enough for its bounce FX, at `bounceAt`
// (3 * TREE_RADIUS_APPROX up the fallen tree, from its base).
inline bool StepTreeBend(TreeBend &bend, const TreeMotion &motion, float frames, std::array<float, 3> &bounceAt) noexcept
{
	if (bend.state == tree_bend_state::Falling)
	{
		float turn = bend.angularVelocity * frames;
		if (bend.accumulation + turn > TreeAngularLimit)
			turn = TreeAngularLimit - bend.accumulation;
		tree_bending_detail::Fall(bend, turn);
		bend.accumulation += turn;
		if (bend.accumulation >= TreeAngularLimit && bend.angularVelocity > 0.0f)
		{
			bend.angularVelocity *= -motion.bounceVelocity;
			if (std::fabs(bend.angularVelocity) < 0.01f)
			{
				bend.angularVelocity = 0.0f;
				bend.state = tree_bend_state::Down;
				if (motion.killWhenToppled)
					bend.sinkFramesLeft = motion.sinkFrames;
			}
			else if (std::fabs(bend.angularVelocity) >= 0.03f)
			{
				const float up = 3.0f * TreeRadiusApprox;
				bounceAt = {bend.rotation[2] * up, bend.rotation[5] * up, bend.rotation[8] * up};
				return true;
			}
		}
		else
			bend.angularVelocity += bend.angularAcceleration * frames;
		return false;
	}
	if (bend.state == tree_bend_state::Down)
	{
		if (motion.killWhenToppled)
		{
			if (bend.sinkFramesLeft <= 0.0f)
				bend.state = tree_bend_state::Gone;
			bend.sinkFramesLeft -= frames;
			bend.sunk += motion.sinkDistance / motion.sinkFrames * frames;
		}
		return false;
	}
	if (bend.state == tree_bend_state::Upright && bend.pushDelta != 0.0f)
	{
		// A logic frame's worth a logic frame (the original steps it each drawn frame, at 30 of them a second).
		bend.pushAside += bend.pushDelta * frames;
		if (bend.pushAside >= 1.0f)
			bend.pushDelta = -1.0f / motion.framesToMoveInward;
		else if (bend.pushAside <= 0.0f)
		{
			bend.pushDelta = 0.0f;
			bend.pushAside = 0.0f;
		}
	}
	return false;
}

// `world` (row-major 4x4, scale in, its translation the tree's base) as the tree buffer places the tree's vertices.
inline void BentTree(std::array<float, 16> &world, const TreeBend &bend, const TreeMotion &motion) noexcept
{
	if (bend.state != tree_bend_state::Upright)
	{
		std::array<float, 16> bent = world;
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t column = 0; column < 3; ++column)
			{
				float sum = 0.0f;
				for (std::size_t k = 0; k < 3; ++k)
					sum += bend.rotation[row * 3 + k] * world[k * 4 + column];
				bent[row * 4 + column] = sum;
			}
		bent[11] -= bend.sunk;
		world = bent;
		return;
	}
	if (bend.pushAside > 0.0f)
	{
		// Each vertex moved by its model height (unscaled) times the lean.
		world[2] += bend.pushAside * bend.pushCos * motion.maxOutwardMovement;
		world[6] += bend.pushAside * bend.pushSin * motion.maxOutwardMovement;
	}
}
}
