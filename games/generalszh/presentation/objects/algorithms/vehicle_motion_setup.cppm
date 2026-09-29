export module games.generalszh.presentation.objects.algorithms.vehicle_motion_setup;
import std;

export import engine.ecs.core.world;
export import games.generalszh.presentation.objects.components.vehicle_motion;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.session.session_view;
import games.generalszh.content.objects.model_draw;
import games.generalszh.presentation.objects.systems.chassis_systems;
import games.generalszh.content.locomotors.locomotor_catalog;
import games.generalszh.content.objects.model_conditions;
import Engine.Core.Math.FixedPresentation;

// Setting presentation up on the simulation's world: its side-table
// components (registered with the session's components, before the world
// is finalized), and each definition's motion look, read from the content
// once, when the session first uses the definition.
export namespace generalszh::presentation
{
void RegisterVehicleMotion(ecs::World &world)
{
	world.RegisterComponent<MotionSample>();
	world.RegisterComponent<ChassisMotion>();
	world.RegisterComponent<TreadRoll>();
	world.RegisterComponent<WheelRoll>();
	world.RegisterComponent<MotionEmission>();
}

MotionLook ReadMotionLook(const session::SessionView &view, std::uint32_t definition)
{
	const content::ObjectDefinition &object = view.Definition(definition);
	MotionLook look;
	if (const auto treads = content::ReadTankTreads(object); treads && treads->rate > Engine::Math::Fixed{})
	{
		look.treadRate = Engine::Math::ToFloat(treads->rate);
		look.treadPivot = Engine::Math::ToFloat(treads->pivotFraction);
		look.treadDrive = Engine::Math::ToFloat(treads->driveFraction);
	}
	if (auto tires = content::ReadTruckTires(object))
	{
		look.wheelBones = std::move(tires->bones);
		look.steeredBones = std::move(tires->steered);
		look.wheelCorners = std::move(tires->corners);
		look.wheelMultiplier = Engine::Math::ToFloat(tires->rotationMultiplier);
		look.cabBone = tires->cabBone;
		look.trailerBone = tires->trailerBone;
		look.cabFactor = Engine::Math::ToFloat(tires->cabFactor);
		look.trailerFactor = Engine::Math::ToFloat(tires->trailerFactor);
		look.swingDamping = Engine::Math::ToFloat(tires->damping);
		look.powerslideAddition = Engine::Math::ToFloat(tires->powerslideAddition);
		if (const auto angle = view.Content().wheelTurnAngles.find(content::ObjectLocomotorName(object, view.Content().locomotors));
			angle != view.Content().wheelTurnAngles.end())
			look.wheelTurn = Engine::Math::ToFloat(angle->second) * 3.14159265f / 180.0f;
	}
	for (const content::MotionEmitter &emitter : content::ReadMotionEmitters(object))
	{
		look.motionSystems.push_back(emitter.system);
		look.motionRoles.push_back(static_cast<std::uint8_t>(emitter.role));
		look.truck = look.truck || emitter.role == content::MotionEmitterRole::Dust || emitter.role == content::MotionEmitterRole::DirtSpray ||
			emitter.role == content::MotionEmitterRole::Powerslide;
	}
	look.landingSound = std::string(object.Sound("TruckLandingSound"));
	look.powerslideSound = std::string(object.Sound("TruckPowerslideSound"));
	if (const auto *locomotor = content::ObjectLocomotor(object, view.Content().locomotors))
		look.maxSpeed = Engine::Math::ToFloat(locomotor->maxSpeed);
	return look;
}

// Reads the looks of definitions the session has taken on since the last call.
void KnowMotionLooks(MotionLooks &looks, const session::SessionView &view)
{
	looks.dyingBit = content::model_condition::Dying;
	const std::size_t count = view.DefinitionCount();
	if (looks.byDefinition.size() < count)
	{
		looks.byDefinition.resize(count);
		looks.known.resize(count, 0);
	}
	for (std::uint32_t definition = 0; definition < count; ++definition)
		if (looks.known[definition] == 0)
		{
			looks.byDefinition[definition] = ReadMotionLook(view, definition);
			looks.known[definition] = 1;
		}
}
}
