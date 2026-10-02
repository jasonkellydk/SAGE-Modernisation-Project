export module games.generalszh.presentation.interaction.resources.academy_client_records;
import std;

import engine.ecs.system.system;

// What only the local machine records in its player's academy (the original's AcademyStats fields the client writes:
// SelectionTranslator's recordDragSelection, CommandTranslator's recordDoubleClickAttackMoveOrderGiven, both on the
// local player's record), read by the skirmish score screen's war school advice. Not simulation state: neither saved
// nor hashed (each machine its own, as the original's).
export namespace generalszh::presentation
{
struct AcademyClientRecords
{
	std::uint32_t dragSelections{0};          // m_dragSelectUnits: a selection that took more than one at once
	std::uint32_t doubleClickAttackMoves{0};  // m_doubleClickAttackMoveOrdersGiven (the double-click guard is not ported)
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::AcademyClientRecords>
{
	static constexpr std::string_view StableName = "generalszh.presentation.academy_client_records";
};
}
