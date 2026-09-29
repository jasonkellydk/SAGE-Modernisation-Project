export module engine.gameplay.rts.parachute.algorithms.parachute_rigging;
import std;

export import engine.gameplay.rts.parachute.definitions.parachute_definition;
export import engine.gameplay.rts.parachute.components.parachute;
export import Engine.Core.Math.FixedAngle;

// Where a parachute's rider hangs and what its sway turns about (ParachuteContain::updateOffsetsFromBones): the
// chute's sway centre and harness and the rider's harness point, at rest, turned with the chute's facing (the rider
// faces as the chute does). The rider hangs at the chute's position plus `riderAttach`; the chute sways about
// `paraSway`, the rider about `riderSway` (both from its own position).
export namespace engine::gameplay
{
struct ParachuteOffsets
{
	Engine::Math::FixedVector3 paraSway;
	Engine::Math::FixedVector3 riderAttach;
	Engine::Math::FixedVector3 riderSway;
};

inline Engine::Math::FixedVector3 TurnedBy(const Engine::Math::FixedVector3 &bone, Engine::Math::TurnAngle facing) noexcept
{
	const Engine::Math::Fixed c = Engine::Math::Cos(facing), s = Engine::Math::Sin(facing);
	return {bone.x * c - bone.y * s, bone.x * s + bone.y * c, bone.z};
}

inline ParachuteOffsets RigParachute(const ParachuteDefinition &definition, const Parachute &chute, Engine::Math::TurnAngle facing) noexcept
{
	ParachuteOffsets offsets;
	offsets.paraSway = TurnedBy(definition.swayBone, facing);
	offsets.riderAttach = TurnedBy(definition.attachBone, facing) - TurnedBy(chute.RiderBone(), facing);
	offsets.riderSway = offsets.paraSway - offsets.riderAttach;
	return offsets;
}
}
