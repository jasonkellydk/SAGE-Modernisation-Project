export module engine.gameplay.rts.combat.algorithms.deploy_states;
import std;

export import engine.gameplay.rts.combat.components.deploy;
export import engine.gameplay.rts.combat.components.turret;

// DeployStyleAIUpdate::setMyState: entering a state. Unpacking takes UnpackTime (a reversed packing: the part of it
// already packed, from UnpackTime); packing takes PackTime (reversed: the part already unpacked, also from
// UnpackTime, as the original); packing turns its turret off when it works only deployed; deployed turns it back on;
// aligning its turrets sends them back to rest. `turret`: the one aiming its current weapon (none: no turret).
export namespace engine::gameplay
{
inline void SetDeployState(Deploy &deploy, Turret *turret, DeployState state, std::uint64_t now, bool reverse = false) noexcept
{
	deploy.state = state;
	const auto reversed = [&] {
		const std::uint64_t left = deploy.waitTick > now ? deploy.waitTick - now : 0;
		const std::uint64_t total = deploy.unpackTicks;
		deploy.waitTick = now + (total > left ? total - left : 0);
	};
	switch (state)
	{
	case DeployState::Deploy:
		if (reverse)
			reversed();
		else
			deploy.waitTick = now + deploy.unpackTicks;
		break;
	case DeployState::Undeploy:
		if (reverse)
			reversed();
		else
			deploy.waitTick = now + deploy.packTicks;
		if (deploy.turretsOnlyWhenDeployed && turret != nullptr)
			turret->enabled = false;
		break;
	case DeployState::ReadyToMove:
		deploy.waitTick = 0;
		break;
	case DeployState::ReadyToAttack:
		deploy.waitTick = 0;
		if (deploy.turretsOnlyWhenDeployed && turret != nullptr)
			turret->enabled = true;
		break;
	case DeployState::AligningTurrets:
		deploy.waitTick = 0;
		if (turret != nullptr)
			turret->state = TurretState::Recenter; // recenterTurret
		break;
	}
}
}
