export module games.generalszh.gameplay.world.resources.chat_inbox;
import std;

import engine.ecs.system.system;

// The in-game chat lines that came through the command stream this tick (a Chat command: NETCOMMANDTYPE_CHAT), for each
// machine to show its own player (ConnectionManager::processChat). Nothing in the simulation reads it: never hashed nor
// checkpointed; the host takes the lines as they come.
export namespace generalszh::gameplay
{
struct ChatLine
{
	std::uint32_t player{0}; // the sender's player
	std::string text;        // UTF-8
	std::uint32_t slots{0};  // the game slots it is for (bit n: slot n)
};

struct ChatInbox
{
	std::vector<ChatLine> lines;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::ChatInbox>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.chat_inbox";
};
}
