export module games.generalszh.gameplay.powers.resources.cash_notices;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Cash that changed hands this tick where it shows (for the presentation's floating text): a kill's bounty over the
// killer (Player::doBountyForKill: "+$n" yellow, 10 up), a cash hack's take over the hacker ("+$n" green, 20 up) and
// its loss over the hacked (GUI:LoseCash red, 30 up). Per tick: not saved.
export namespace generalszh::gameplay
{
struct CashNotice
{
	enum class Kind : std::uint8_t
	{
		Bounty,
		Stolen,
		Lost,
		Hacked, // a hacker's pay (HackInternetState: GUI:AddCash, green, over it)
	};
	Kind kind{Kind::Bounty};
	std::int64_t amount{0};
	Engine::Math::FixedVector3 position; // already raised above the object
};

struct CashNotices
{
	std::vector<CashNotice> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::CashNotices>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.cash_notices";
};
}
