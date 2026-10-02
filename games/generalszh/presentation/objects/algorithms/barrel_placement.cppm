export module games.generalszh.presentation.objects.algorithms.barrel_placement;
import std;

export import games.generalszh.presentation.objects.resources.object_frame;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.content.objects.model_states;
export import Engine.Core.Math.TurnAngle;

export namespace generalszh::presentation
{
namespace barrel_placement_detail
{
inline float SignedRadians(Engine::Math::TurnAngle angle) noexcept
{
	return static_cast<float>(static_cast<std::int32_t>(angle.units)) * (6.283185307179586f / 4294967296.0f);
}
}

// Where a weapon's fire FX plays (W3DModelDraw::handleWeaponFireFX: the firing barrel's FireFX bone's
// transform as drawn): the bone NAME01, NAME02... for each barrel (NAME itself when unnumbered), turned
// as the draw turns it when it hangs below the turret (pitch about the pitch bone's y, then the turn
// about the turret bone's z, each plus its art angle), then onto the object. None when the state has
// no FireFX bone or the model lacks it (the caller keeps the drawable's position); nullopt with
// `pending` set while the model still loads.
struct BarrelPlace
{
	std::array<float, 3> at{};
	float yaw{0.0f};
};

// (`boneName`, when given, in place of the state's FireFX bone: a laser's LaserBoneName.)
// (`alt`: the bone hangs on the second turret: AltTurret / AltTurretPitch and their art offsets.)
inline std::optional<BarrelPlace> PlaceOnBarrel(const PresentedObject &object, const content::ModelState &state, std::string_view model,
	const BonePoses &poses, float turret, float pitch, std::uint32_t barrel, bool *pending = nullptr, std::string_view boneName = {}, bool alt = false)
{
	const std::string &turretBone = alt ? state.altTurretBone : state.turretBone;
	const std::string &pitchBone = alt ? state.altTurretPitchBone : state.turretPitchBone;
	const Engine::Math::TurnAngle artAngle = alt ? state.altTurretArtAngle : state.turretArtAngle;
	const Engine::Math::TurnAngle artPitch = alt ? state.altTurretArtPitch : state.turretArtPitch;
	if (pending != nullptr)
		*pending = false;
	const std::string fireBone = boneName.empty() ? state.fireFxBone : std::string(boneName);
	if (fireBone.empty() || model.empty() || !poses.pose)
		return std::nullopt;
	char numbered[128];
	std::snprintf(numbered, sizeof numbered, "%s%02u", fireBone.c_str(), barrel + 1u);
	BoneLookup bone = poses.pose(model, numbered);
	std::string name = numbered;
	if (!bone.ready)
	{
		if (pending != nullptr)
			*pending = true;
		return std::nullopt;
	}
	if (!bone.found)
	{
		// A single barrel's bone goes unnumbered; so does a barrel past the numbered ones (barrel 0's then).
		std::snprintf(numbered, sizeof numbered, "%s01", fireBone.c_str());
		bone = poses.pose(model, fireBone);
		name = fireBone;
		if (!bone.found)
		{
			bone = poses.pose(model, numbered);
			name = numbered;
		}
	}
	if (!bone.found)
		return std::nullopt;
	std::array<float, 3> p = bone.position;
	float yaw = bone.yaw;
	const auto under = [&](const std::string &ancestor) { return !ancestor.empty() && poses.descends && poses.descends(model, name, ancestor); };
	if (under(pitchBone))
		if (const BoneLookup hinge = poses.pose(model, pitchBone); hinge.found)
		{
			// About the hinge's own y (its turn round z gives the axis), as the draw's -y by the aim.
			const float angle = -(pitch + barrel_placement_detail::SignedRadians(artPitch));
			const float ax = -std::sin(hinge.yaw), ay = std::cos(hinge.yaw);
			const std::array<float, 3> v{p[0] - hinge.position[0], p[1] - hinge.position[1], p[2] - hinge.position[2]};
			const float c = std::cos(angle), s = std::sin(angle), dot = ax * v[0] + ay * v[1];
			// Rodrigues about the unit axis (ax, ay, 0).
			const std::array<float, 3> cross{ay * v[2], -ax * v[2], ax * v[1] - ay * v[0]};
			for (int i = 0; i < 3; ++i)
			{
				const float axis = i == 0 ? ax : i == 1 ? ay : 0.0f;
				p[i] = hinge.position[i] + v[i] * c + cross[i] * s + axis * dot * (1.0f - c);
			}
		}
	if (under(turretBone))
		if (const BoneLookup pivot = poses.pose(model, turretBone); pivot.found)
		{
			const float angle = turret + barrel_placement_detail::SignedRadians(artAngle);
			const float c = std::cos(angle), s = std::sin(angle);
			const float x = p[0] - pivot.position[0], y = p[1] - pivot.position[1];
			p[0] = pivot.position[0] + c * x - s * y;
			p[1] = pivot.position[1] + s * x + c * y;
			yaw += angle;
		}
	const float c = std::cos(object.facing), s = std::sin(object.facing);
	const float x = p[0] * object.scale, y = p[1] * object.scale, z = p[2] * object.scale;
	return BarrelPlace{{object.position[0] + c * x - s * y, object.position[1] + s * x + c * y, object.position[2] + z}, object.facing + yaw};
}
}
