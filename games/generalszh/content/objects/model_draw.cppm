export module games.generalszh.content.objects.model_draw;
import std;

export import games.generalszh.content.objects.object_definition;

// What an object looks like at rest, from its Draw modules: the model the
// W3D*Draw module shows in its default condition state (W3DModelDraw and
// its tank/truck/... variants), or the single ModelName of tree/prop draws,
// and the animation that state plays (rotors, flags, radar dishes...).
export namespace generalszh::content
{
// W3DModelDraw's AnimationMode names, in the original's order.
enum class ModelAnimationMode : std::uint8_t
{
	Manual,
	Loop,
	Once,
	LoopPingPong,
	LoopBackwards,
	OnceBackwards,
};

struct RestingModel
{
	std::string model;     // W3D model name, without extension
	std::string drawType;  // the Draw module type it came from
	// The state's first Animation (or IdleAnimation), "<skeleton>.<clip>";
	// empty when the state does not animate.
	std::string animation;
	// A condition state's AnimationMode defaults to ONCE, as in the original.
	ModelAnimationMode animationMode{ModelAnimationMode::Once};
	// IdleAnimations play ONCE and are then picked again, so they repeat.
	bool idleAnimation{false};
};

RestingModel DefaultModel(const ObjectDefinition &object);

// How a tank's treads roll (W3DTankDraw): their texture scrolls at `rate`
// a second while it drives at `driveFraction` of its top speed or more, and
// the two sides roll opposite ways while it turns slower than
// `pivotFraction` of it.
struct TankTreads
{
	Engine::Math::Fixed rate;
	Engine::Math::Fixed pivotFraction{Engine::Math::Fixed::FromRatio(6, 10)};
	Engine::Math::Fixed driveFraction{Engine::Math::Fixed::FromRatio(3, 10)};
};

std::optional<TankTreads> ReadTankTreads(const ObjectDefinition &object);

// A truck's tires (W3DTruckDraw): their bones, turned by distance driven
// times `rotationMultiplier` (radians per unit, as the original).
struct TruckTires
{
	std::vector<std::string> bones;   // every tire
	std::vector<std::string> steered; // the front ones, which also steer
	// Each tire's corner, for its suspension (0 front left, 1 front right, 2 rear left, 3 rear right: the mid front
	// ones ride with the front, the mid and mid rear ones with the rear).
	std::vector<std::uint8_t> corners;
	Engine::Math::Fixed rotationMultiplier;
	// The cab and trailer swing with the steering (CabRotationMultiplier,
	// TrailerRotationMultiplier), easing by RotationDamping.
	std::string cabBone;
	std::string trailerBone;
	Engine::Math::Fixed cabFactor;
	Engine::Math::Fixed trailerFactor;
	Engine::Math::Fixed damping;
	// PowerslideRotationAddition: how much faster the rear tires turn (a tick) while it powerslides.
	Engine::Math::Fixed powerslideAddition;
};

std::optional<TruckTires> ReadTruckTires(const ObjectDefinition &object);

// The particle systems a vehicle kicks up while it moves: a tank's tread
// debris (TreadDebrisLeft/Right; W3DTankDraw defaults to the dirt ones), a
// truck's Dust and DirtSpray.
// A particle system a vehicle runs while it moves, and what it is to its draw module (how its motion scales it).
enum class MotionEmitterRole : std::uint8_t
{
	TreadDebris, // W3DTankDraw TreadDebrisLeft/Right
	Dust,        // W3DTruckDraw Dust
	DirtSpray,   // W3DTruckDraw DirtSpray
	Powerslide,  // W3DTruckDraw PowerslideSpray
};

struct MotionEmitter
{
	std::string system;
	MotionEmitterRole role{MotionEmitterRole::TreadDebris};
};

std::vector<MotionEmitter> ReadMotionEmitters(const ObjectDefinition &object);
}

namespace generalszh::content
{
namespace
{
// The draw modules built on W3DTankDraw / W3DTruckDraw (their treads, tires, dust and debris).
bool IsTankDraw(std::string_view type) noexcept { return type == "W3DTankDraw" || type == "W3DOverlordTankDraw"; }
bool IsTruckDraw(std::string_view type) noexcept { return type == "W3DTruckDraw" || type == "W3DPoliceCarDraw" || type == "W3DOverlordTruckDraw"; }

bool IsNone(std::string_view value) noexcept
{
	return value.empty() || value == "NONE" || value == "None" || value == "none";
}

bool SameName(std::string_view left, std::string_view right) noexcept
{
	if (left.size() != right.size())
		return false;
	for (std::size_t i = 0; i < left.size(); ++i)
	{
		const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
		if (lower(left[i]) != lower(right[i]))
			return false;
	}
	return true;
}

void ReadAnimation(const engine::config::Node &state, RestingModel &result)
{
	for (const engine::config::Node &child : state.children)
	{
		const bool idle = child.key == "IdleAnimation";
		if ((child.key == "Animation" || idle) && result.animation.empty() && !IsNone(child.Value()))
		{
			result.animation = std::string(child.Value());
			result.idleAnimation = idle;
		}
	}
	if (const auto *mode = state.Find("AnimationMode"))
	{
		static constexpr std::array<std::string_view, 6> names{"MANUAL", "LOOP", "ONCE", "LOOP_PINGPONG", "LOOP_BACKWARDS", "ONCE_BACKWARDS"};
		for (std::size_t i = 0; i < names.size(); ++i)
			if (SameName(mode->Value(), names[i]))
				result.animationMode = static_cast<ModelAnimationMode>(i);
	}
}
}

RestingModel DefaultModel(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Draw || module.block == nullptr || !std::string_view(module.type).starts_with("W3D"))
			continue;
		if (const auto *name = module.block->Find("ModelName"))
			if (!name->Value().empty())
				return {std::string(name->Value()), module.type};
		const engine::config::Node *state = module.block->Find("DefaultConditionState");
		if (state == nullptr)
			state = module.block->Find("ConditionState");
		if (state == nullptr)
			continue;
		if (const auto *model = state->Find("Model"))
		{
			const std::string_view value = model->Value();
			if (!IsNone(value))
			{
				RestingModel result{std::string(value), module.type};
				ReadAnimation(*state, result);
				return result;
			}
		}
	}
	return {};
}

std::optional<TankTreads> ReadTankTreads(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		// W3DTankTruckDraw scrolls its treads too, but only driving (its pivoting is commented out in the original).
		const bool tankTruck = module.type == "W3DTankTruckDraw";
		if (module.slot != ModuleSlot::Draw || module.block == nullptr || (!IsTankDraw(module.type) && !tankTruck))
			continue;
		TankTreads treads;
		const auto fixed = [&](std::string_view key, Engine::Math::Fixed fallback) {
			const auto *node = module.block->Find(key);
			return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(fallback) : fallback;
		};
		treads.rate = fixed("TreadAnimationRate", treads.rate);
		treads.pivotFraction = fixed("TreadPivotSpeedFraction", treads.pivotFraction);
		treads.driveFraction = fixed("TreadDriveSpeedFraction", treads.driveFraction);
		if (tankTruck)
			treads.pivotFraction = Engine::Math::Fixed{};
		return treads;
	}
	return std::nullopt;
}

std::optional<TruckTires> ReadTruckTires(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		// W3DTankTruckDraw's tires turn as a truck's (its mid tires, which no shipped object has, never turn there).
		if (module.slot != ModuleSlot::Draw || module.block == nullptr || (!IsTruckDraw(module.type) && module.type != "W3DTankTruckDraw"))
			continue;
		TruckTires tires;
		for (const engine::config::Node &field : module.block->children)
		{
			const std::string_view key = field.key;
			const auto fixed = [&] { return engine::config::values::ParseFixed(field.Value()).value_or(Engine::Math::Fixed{}); };
			if (key.ends_with("TireBone") && !IsNone(field.Value()))
			{
				tires.bones.emplace_back(field.Value());
				tires.corners.push_back(static_cast<std::uint8_t>((key.find("Front") != std::string_view::npos ? 0u : 2u) +
					(key.find("Right") != std::string_view::npos ? 1u : 0u)));
				// LeftFront, RightFront, MidLeftFront, MidRightFront steer (as the original).
				if (key.find("Front") != std::string_view::npos)
					tires.steered.emplace_back(field.Value());
			}
			else if (SameName(key, "TireRotationMultiplier"))
				tires.rotationMultiplier = fixed();
			else if (SameName(key, "CabBone") && !IsNone(field.Value()))
				tires.cabBone = std::string(field.Value());
			else if (SameName(key, "TrailerBone") && !IsNone(field.Value()))
				tires.trailerBone = std::string(field.Value());
			else if (SameName(key, "CabRotationMultiplier"))
				tires.cabFactor = fixed();
			else if (SameName(key, "TrailerRotationMultiplier"))
				tires.trailerFactor = fixed();
			else if (SameName(key, "RotationDamping"))
				tires.damping = fixed();
			else if (SameName(key, "PowerslideRotationAddition"))
				tires.powerslideAddition = fixed();
		}
		if (tires.bones.empty())
			return std::nullopt;
		return tires;
	}
	return std::nullopt;
}

std::vector<MotionEmitter> ReadMotionEmitters(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Draw || module.block == nullptr)
			continue;
		const std::string_view type = module.type;
		const bool tank = IsTankDraw(type), tankTruck = type == "W3DTankTruckDraw", truck = IsTruckDraw(type);
		if (!tank && !tankTruck && !truck)
			continue;
		std::vector<MotionEmitter> systems;
		const auto add = [&](std::string_view key, std::string_view fallback, MotionEmitterRole role) {
			const auto *node = module.block->Find(key);
			const std::string_view name = node != nullptr ? node->Value() : fallback;
			if (!IsNone(name))
				systems.push_back({std::string(name), role});
		};
		if (tank || tankTruck)
		{
			add("TreadDebrisLeft", tank ? "TrackDebrisDirtLeft" : "", MotionEmitterRole::TreadDebris);
			add("TreadDebrisRight", tank ? "TrackDebrisDirtRight" : "", MotionEmitterRole::TreadDebris);
		}
		if (truck || tankTruck)
		{
			add("Dust", "", MotionEmitterRole::Dust);
			add("DirtSpray", "", MotionEmitterRole::DirtSpray);
			add("PowerslideSpray", "", MotionEmitterRole::Powerslide);
		}
		return systems;
	}
	return {};
}
}
