export module engine.gameplay.rts.veterancy.resources.promotions;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;

// This tick's promotions earned in play (Object::onVeterancyLevelChanged with
// feedback: a higher level from experience), for the presentation to show
// (not levels set on objects as they are made).
export namespace engine::gameplay
{
struct Promotion
{
	ecs::Entity entity;
	std::uint8_t from{0};
	std::uint8_t to{0};
};

struct Promotions
{
	std::vector<Promotion> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::Promotions>
{
	static constexpr std::string_view StableName = "engine.gameplay.promotions";
};
}
