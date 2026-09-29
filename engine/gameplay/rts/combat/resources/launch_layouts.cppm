export module engine.gameplay.rts.combat.resources.launch_layouts;
import std;

export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
export import engine.gameplay.common.spatial.components.transform;
import engine.ecs.system.system;

// Where each kind of object launches its primary weapon's projectiles from
// (the original's WeaponBarrelInfo::projectile_offset_transform, from its
// model's launch bones at rest): a point per barrel in the object's own
// frame, and the turret's turn and pitch pivots when the launch point turns
// with a turret. Definition data, indexed by DefinitionRef.
export namespace engine::gameplay
{
struct LaunchLayout
{
	// Per weapon slot (PRIMARY, SECONDARY, TERTIARY), a point per barrel (empty: from the object's origin).
	std::array<std::vector<Engine::Math::FixedVector3>, 3> barrels;
	Engine::Math::FixedVector3 turretPivot;
	Engine::Math::FixedVector3 pitchPivot;
	// The second turret's (AltTurret) pivots.
	Engine::Math::FixedVector3 altTurretPivot;
	Engine::Math::FixedVector3 altPitchPivot;
};

struct LaunchLayouts
{
	std::vector<LaunchLayout> byDefinition;

	const LaunchLayout *Of(std::uint32_t definition) const noexcept
	{
		return definition < byDefinition.size() ? &byDefinition[definition] : nullptr;
	}
};

// Weapon::calcProjectileLaunchPosition: the barrel's point (the first for a barrel past the last), pitched
// about the pitch pivot's y by the turret's pitch, then turned about the turret pivot's z by its turn
// (when it has a turret), then onto the launcher. No layout: the launcher's own position.
// (`slot`: the weapon slot whose barrels it fires from; `alt`: its turret is the second one.)
inline Engine::Math::FixedVector3 LaunchPosition(const LaunchLayout *layout, std::uint32_t barrel, bool turreted, Engine::Math::TurnAngle turn,
	Engine::Math::TurnAngle pitch, const Transform &launcher, std::uint32_t slot = 0, bool alt = false) noexcept
{
	using Engine::Math::Fixed;
	using Engine::Math::FixedVector3;
	if (layout == nullptr)
		return launcher.position;
	const auto &barrels = layout->barrels[slot < 3 ? slot : 0];
	FixedVector3 p = barrels.empty() ? FixedVector3{} : barrels[barrel < barrels.size() ? barrel : 0];
	if (turreted)
	{
		const FixedVector3 pitchPivot = alt ? layout->altPitchPivot : layout->pitchPivot;
		const FixedVector3 turretPivot = alt ? layout->altTurretPivot : layout->turretPivot;
		// In_Place_Pre_Rotate_Y(-pitch) about the pitch pivot: the front rises with the pitch.
		const Fixed cp = Engine::Math::Cos(pitch), sp = Engine::Math::Sin(pitch);
		const FixedVector3 q = p - pitchPivot;
		p = pitchPivot + FixedVector3{q.x * cp - q.z * sp, q.y, q.x * sp + q.z * cp};
		// In_Place_Pre_Rotate_Z(turn) about the turret pivot.
		const Fixed ct = Engine::Math::Cos(turn), st = Engine::Math::Sin(turn);
		const FixedVector3 r = p - turretPivot;
		p = turretPivot + FixedVector3{r.x * ct - r.y * st, r.x * st + r.y * ct, r.z};
	}
	const Fixed cf = Engine::Math::Cos(launcher.facing), sf = Engine::Math::Sin(launcher.facing);
	return launcher.position + FixedVector3{p.x * cf - p.y * sf, p.x * sf + p.y * cf, p.z};
}
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::LaunchLayouts>
{
	static constexpr std::string_view StableName = "engine.gameplay.launch_layouts";
};
}
