export module games.generalszh.gameplay.appearance.systems.steering_look_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.rts.movement.components.locomotion;
export import games.generalszh.gameplay.appearance.components.steering_look;
import games.generalszh.content.objects.model_conditions;

// AnimationSteeringUpdate::update, each tick in parallel: going straight, a
// full-rate turn right (TURN_NEGATIVE) or left starts CENTER_TO_RIGHT /
// CENTER_TO_LEFT; turning no longer that way, it recenters (RIGHT_TO_CENTER /
// LEFT_TO_CENTER); recentering with no turn, it is straight again. Each change
// but the last holds for MinTransitionTime before the next.
export namespace generalszh::gameplay
{
struct SteeringLookSystem
{
	using Query = ecs::Query<ecs::Write<SteeringLook>, ecs::Write<engine::gameplay::Appearance>, ecs::Read<engine::gameplay::Locomotion>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace mc = content::model_condition;
		constexpr std::uint32_t centerToRight = content::ModelConditionBit("CENTER_TO_RIGHT"), centerToLeft = content::ModelConditionBit("CENTER_TO_LEFT"),
								rightToCenter = content::ModelConditionBit("RIGHT_TO_CENTER"), leftToCenter = content::ModelConditionBit("LEFT_TO_CENTER");
		const std::uint64_t now = context.Tick();
		auto looks = chunk.Get<SteeringLook>();
		auto appearances = chunk.Get<engine::gameplay::Appearance>();
		const auto motions = chunk.Get<engine::gameplay::Locomotion>();
		for (std::size_t row = 0; row < looks.size(); ++row)
		{
			SteeringLook &look = looks[row];
			if (now < look.nextTick)
				continue;
			engine::gameplay::Appearance &appearance = appearances[row];
			const std::int8_t turn = motions[row].turning;
			const auto change = [&](SteeringPose pose, std::uint32_t clear, std::uint32_t set) {
				appearance.Set(clear, false);
				appearance.Set(set);
				look.pose = pose;
				look.nextTick = now + look.transitionTicks;
			};
			switch (look.pose)
			{
			case SteeringPose::Straight:
				if (turn < 0)
					change(SteeringPose::CenterToRight, centerToRight, centerToRight);
				else if (turn > 0)
					change(SteeringPose::CenterToLeft, centerToLeft, centerToLeft);
				break;
			case SteeringPose::CenterToRight:
				if (turn >= 0)
					change(SteeringPose::RightToCenter, centerToRight, rightToCenter);
				break;
			case SteeringPose::CenterToLeft:
				if (turn <= 0)
					change(SteeringPose::LeftToCenter, centerToLeft, leftToCenter);
				break;
			case SteeringPose::RightToCenter:
			case SteeringPose::LeftToCenter:
				if (turn == 0)
				{
					appearance.Set(leftToCenter, false);
					appearance.Set(rightToCenter, false);
					look.pose = SteeringPose::Straight;
					look.nextTick = now;
				}
				break;
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::SteeringLookSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.steering_look";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
