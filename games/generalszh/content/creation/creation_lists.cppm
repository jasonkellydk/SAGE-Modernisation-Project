export module games.generalszh.content.creation.creation_lists;
import std;

export import engine.config.binding.schema;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;

// Object creation lists ("ObjectCreationList" blocks,
// Data/INI/ObjectCreationList.ini) as data: what a list creates, where and
// how it is thrown. CreateObject makes objects by name; CreateDebris makes
// GenericDebris pieces showing one of its models; FireWeapon fires a weapon;
// ApplyRandomForce throws the source itself.
// Delivery runs (DeliverPayload) are special powers' (special_powers).
// Angles and rates are turn units, forces per tick (the original applies
// them per frame), durations in ticks.
export namespace generalszh::content
{
enum class CreationKind : std::uint8_t
{
	CreateObject,
	CreateDebris,
	FireWeapon,
	ApplyRandomForce,
	Attack, // AttackNugget: its source attacks the spot with a weapon slot, so many shots
};

// The original's DebrisDisposition bits, in its order.
namespace disposition
{
inline constexpr std::uint32_t LikeExisting = 1u << 0;
inline constexpr std::uint32_t OnGroundAligned = 1u << 1;
inline constexpr std::uint32_t SendItFlying = 1u << 2;
inline constexpr std::uint32_t SendItUp = 1u << 3;
inline constexpr std::uint32_t SendItOut = 1u << 4;
inline constexpr std::uint32_t RandomForce = 1u << 5;
inline constexpr std::uint32_t Floating = 1u << 6;
inline constexpr std::uint32_t InheritVelocity = 1u << 7;
inline constexpr std::uint32_t Whirling = 1u << 8;
inline constexpr std::array<std::string_view, 9> Names{"LIKE_EXISTING", "ON_GROUND_ALIGNED", "SEND_IT_FLYING", "SEND_IT_UP", "SEND_IT_OUT",
	"RANDOM_FORCE", "FLOATING", "INHERIT_VELOCITY", "WHIRLING"};
}

struct CreationNugget
{
	CreationKind kind{CreationKind::CreateObject};
	// Object names (CreateObject) or model names (CreateDebris); one is picked per piece.
	std::vector<std::string> names;
	std::string weapon; // FireWeapon
	std::uint32_t weaponSlot{0};   // Attack: WeaponSlot (PRIMARY 0, SECONDARY 1, TERTIARY 2)
	std::uint32_t shots{1};        // Attack: NumberOfShots
	std::uint32_t count{1};
	Engine::Math::FixedVector3 offset;
	std::uint32_t disposition{disposition::OnGroundAligned};
	Engine::Math::Fixed intensity;
	// Turn units per tick; none: from the intensity (spin) or the spin (the rest).
	std::optional<std::int32_t> spinRate, yawRate, rollRate, pitchRate;
	Engine::Math::Fixed minForce, maxForce;
	Engine::Math::TurnAngle minPitch, maxPitch;
	std::uint64_t minLifetime{0}, maxLifetime{0};
	Engine::Math::Fixed mass;
	Engine::Math::Fixed extraFriction; // per tick
	Engine::Math::Fixed minHealth{Engine::Math::Fixed::One()}, maxHealth{Engine::Math::Fixed::One()};
	bool orientInForceDirection{false};
	bool requiresLivePlayer{false};
	bool skipIfSignificantlyAirborne{false};
	// ContainInsideSourceObject: what it makes goes inside the source (a portable structure mounts on it), or is
	// destroyed if it cannot.
	bool containInside{false};
	bool okToChangeModelColor{false};
	bool inheritsVeterancy{false}; // takes its source's veterancy level
	std::string particleSystem;
	// CreateDebris: animation sets (initial, flying, final; one picked per piece), the effect it plays as it lands,
	// the sound it makes each time it lands.
	std::vector<std::array<std::string, 3>> animationSets;
	std::string finalEffect;
	std::string bounceSound;
	// Read as the original does, which never uses it (PhysicsBehavior::m_extraBounciness is only saved).
	Engine::Math::Fixed extraBounciness;
	// What it makes does not collide with its source (PhysicsBehavior::setIgnoreCollisionsWith).
	bool ignorePrimaryObstacle{false};
	std::uint64_t invulnerableTicks{0}; // InvulnerableTime: an undetected defector this long (Object::goInvulnerable)
	std::string putInContainer;          // PutInContainer: what holds what it makes (a parachute)
};

struct CreationList
{
	std::vector<CreationNugget> nuggets;
};

using CreationLists = std::map<std::string, CreationList, std::less<>>;

namespace creation_detail
{
using engine::config::Node;

bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}

// Degrees per second -> signed turn units per tick.
std::int32_t Rate(const Node &node, engine::config::BindContext &bind)
{
	const auto perTick = engine::config::ReadDegreesPerSecond(node, bind);
	return perTick ? static_cast<std::int32_t>(perTick->units) : 0;
}

std::uint32_t Disposition(const Node &node)
{
	std::uint32_t bits = 0;
	for (const std::string_view token : node.values)
		for (std::size_t index = 0; index < disposition::Names.size(); ++index)
			if (Same(token, disposition::Names[index]))
				bits |= 1u << index;
	return bits;
}

CreationNugget ReadNugget(CreationKind kind, const Node &block, engine::config::BindContext &bind)
{
	CreationNugget nugget;
	nugget.kind = kind;
	for (const Node &field : block.children)
	{
		const std::string_view key = field.key;
		const auto fixed = [&] { return engine::config::ReadFixed(field, bind).value_or(Engine::Math::Fixed{}); };
		if (Same(key, "ObjectNames") || Same(key, "ModelNames"))
			for (const std::string_view name : field.values)
				nugget.names.emplace_back(name);
		else if (Same(key, "Weapon"))
			nugget.weapon = std::string(field.Value());
		else if (Same(key, "Count"))
			nugget.count = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::ReadInt(field, bind).value_or(1)));
		else if (Same(key, "Offset"))
			nugget.offset = engine::config::ReadVec3(field, bind).value_or(Engine::Math::FixedVector3{});
		else if (Same(key, "Disposition"))
			nugget.disposition = Disposition(field);
		else if (Same(key, "DispositionIntensity"))
			nugget.intensity = fixed();
		else if (Same(key, "SpinRate"))
			nugget.spinRate = Rate(field, bind);
		else if (Same(key, "YawRate"))
			nugget.yawRate = Rate(field, bind);
		else if (Same(key, "RollRate"))
			nugget.rollRate = Rate(field, bind);
		else if (Same(key, "PitchRate"))
			nugget.pitchRate = Rate(field, bind);
		else if (Same(key, "MinForceMagnitude"))
			nugget.minForce = fixed();
		else if (Same(key, "MaxForceMagnitude"))
			nugget.maxForce = fixed();
		else if (Same(key, "MinForcePitch"))
			nugget.minPitch = engine::config::ReadDegrees(field, bind).value_or(Engine::Math::TurnAngle{});
		else if (Same(key, "MaxForcePitch"))
			nugget.maxPitch = engine::config::ReadDegrees(field, bind).value_or(Engine::Math::TurnAngle{});
		else if (Same(key, "MinLifetime"))
			nugget.minLifetime = engine::config::ReadDurationTicks(field, bind).value_or(0);
		else if (Same(key, "MaxLifetime"))
			nugget.maxLifetime = engine::config::ReadDurationTicks(field, bind).value_or(0);
		else if (Same(key, "Mass"))
			nugget.mass = fixed();
		else if (Same(key, "ExtraFriction"))
			nugget.extraFriction = engine::config::ReadPerSecond(field, bind).value_or(Engine::Math::Fixed{});
		else if (Same(key, "MinHealth"))
			nugget.minHealth = engine::config::ReadPercent(field, bind).value_or(Engine::Math::Fixed::One());
		else if (Same(key, "MaxHealth"))
			nugget.maxHealth = engine::config::ReadPercent(field, bind).value_or(Engine::Math::Fixed::One());
		else if (Same(key, "OrientInForceDirection"))
			nugget.orientInForceDirection = engine::config::ReadBool(field, bind).value_or(false);
		else if (Same(key, "RequiresLivePlayer"))
			nugget.requiresLivePlayer = engine::config::ReadBool(field, bind).value_or(false);
		else if (Same(key, "ContainInsideSourceObject"))
			nugget.containInside = engine::config::ReadBool(field, bind).value_or(false);
		else if (Same(key, "SkipIfSignificantlyAirborne"))
			nugget.skipIfSignificantlyAirborne = engine::config::ReadBool(field, bind).value_or(false);
		else if (Same(key, "InheritsVeterancy"))
			nugget.inheritsVeterancy = engine::config::ReadBool(field, bind).value_or(false);
		else if (Same(key, "OkToChangeModelColor"))
			nugget.okToChangeModelColor = engine::config::ReadBool(field, bind).value_or(false);
		else if (Same(key, "ParticleSystem"))
			nugget.particleSystem = std::string(field.Value());
		else if (Same(key, "AnimationSet"))
			nugget.animationSets.push_back({std::string(field.Value(0)), std::string(field.Value(1)), std::string(field.Value(2))});
		else if (Same(key, "FXFinal"))
			nugget.finalEffect = std::string(field.Value());
		else if (Same(key, "BounceSound"))
			nugget.bounceSound = std::string(field.Value());
		else if (Same(key, "ExtraBounciness"))
			nugget.extraBounciness = fixed();
		else if (Same(key, "PutInContainer"))
			nugget.putInContainer = std::string(field.Value());
		else if (Same(key, "InvulnerableTime"))
			nugget.invulnerableTicks = engine::config::ReadDurationTicks(field, bind).value_or(0);
		else if (Same(key, "IgnorePrimaryObstacle"))
			nugget.ignorePrimaryObstacle = engine::config::ReadBool(field, bind).value_or(false);
	}
	return nugget;
}
}

CreationLists BindCreationLists(const engine::config::Document &document, const engine::time::FixedStep &step)
{
	using namespace creation_detail;
	CreationLists lists;
	engine::config::Diagnostics diagnostics;
	engine::config::BindContext bind{diagnostics, step};
	for (const Node &root : document.Roots())
	{
		if (root.key != "ObjectCreationList")
			continue;
		CreationList list;
		for (const Node &child : root.children)
		{
			if (Same(child.key, "CreateObject"))
				list.nuggets.push_back(ReadNugget(CreationKind::CreateObject, child, bind));
			else if (Same(child.key, "CreateDebris"))
				list.nuggets.push_back(ReadNugget(CreationKind::CreateDebris, child, bind));
			else if (Same(child.key, "FireWeapon"))
				list.nuggets.push_back(ReadNugget(CreationKind::FireWeapon, child, bind));
			else if (Same(child.key, "ApplyRandomForce"))
				list.nuggets.push_back(ReadNugget(CreationKind::ApplyRandomForce, child, bind));
			else if (Same(child.key, "Attack"))
			{
				CreationNugget attack;
				attack.kind = CreationKind::Attack;
				if (const Node *shots = child.Find("NumberOfShots"))
					attack.shots = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::values::ParseInt(shots->Value()).value_or(1)));
				if (const Node *slot = child.Find("WeaponSlot"); slot != nullptr && !slot->values.empty())
					attack.weaponSlot = Same(slot->Value(), "SECONDARY") ? 1u : Same(slot->Value(), "TERTIARY") ? 2u : 0u;
				list.nuggets.push_back(std::move(attack));
			}
		}
		lists.insert_or_assign(std::string(root.Value()), std::move(list));
	}
	return lists;
}
}
