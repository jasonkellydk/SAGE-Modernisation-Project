export module games.generalszh.hud.comm_views;
import std;

export import games.generalszh.hud.in_game_chat;
export import games.generalszh.hud.diplomacy;
export import Engine.UI.WND.Bindings;

// Binds Window/InGameChat.wnd to the InGameChatViewModel (its box TextEntryChat with Enter as GEM_EDIT_DONE, its
// StaticTextChatType label, ButtonClear) and Window/Diplomacy.wnd to the DiplomacyViewModel (InGameParent's rows:
// StaticTextPlayer / Side / Team / Status<n> in their colours, ButtonMute<n> / ButtonUnMute<n>; SoloParent's
// ListboxSolo; ButtonHide). The buddy panel (GameSpy) and its radio buttons stay hidden. These tables are the whole view.
export namespace generalszh::hud
{
inline void BindInGameChatView(Engine::UI::WND::WNDBindings &bindings, InGameChatViewModel &viewModel)
{
	const std::string chat = "InGameChat.wnd:";
	bindings.BindVisible(chat + "ParentInGameChat", viewModel.shown);
	bindings.BindEntry(chat + "TextEntryChat", viewModel.entry, &viewModel.done);
	bindings.BindText(chat + "StaticTextChatType", viewModel.typeLabel);
	bindings.BindCommand(chat + "ButtonClear", viewModel.clear);
}

inline void BindDiplomacyView(Engine::UI::WND::WNDBindings &bindings, DiplomacyViewModel &viewModel)
{
	const std::string window = "Diplomacy.wnd:";
	bindings.BindVisible(window + "Parent", viewModel.shown);
	bindings.BindVisible(window + "InGameParent", viewModel.inGameShown);
	bindings.BindVisible(window + "SoloParent", viewModel.soloShown);
	for (const char *hidden : {"BuddiesParent", "RadioButtonInGame", "RadioButtonBuddies"})
		bindings.BindVisible(window + hidden, viewModel.never);
	bindings.BindCommand(window + "ButtonHide", viewModel.hide);
	bindings.BindList(window + "ListboxSolo", viewModel.briefingLines, viewModel.noRow);
	bindings.BindRowColors(window + "ListboxSolo", viewModel.briefingRowColors);
	for (std::size_t row = 0; row < DiplomacyRows; ++row)
	{
		const std::string number = std::to_string(row);
		bindings.BindVisible(window + "StaticTextPlayer" + number, viewModel.rowShown[row]);
		bindings.BindText(window + "StaticTextPlayer" + number, viewModel.rowName[row]);
		bindings.BindTextColor(window + "StaticTextPlayer" + number, viewModel.rowColor[row]);
		bindings.BindVisible(window + "StaticTextSide" + number, viewModel.rowShown[row]);
		bindings.BindText(window + "StaticTextSide" + number, viewModel.rowSide[row]);
		bindings.BindTextColor(window + "StaticTextSide" + number, viewModel.rowColor[row]);
		bindings.BindVisible(window + "StaticTextTeam" + number, viewModel.rowShown[row]);
		bindings.BindText(window + "StaticTextTeam" + number, viewModel.rowTeam[row]);
		bindings.BindTextColor(window + "StaticTextTeam" + number, viewModel.rowColor[row]);
		bindings.BindVisible(window + "StaticTextStatus" + number, viewModel.rowShown[row]);
		bindings.BindText(window + "StaticTextStatus" + number, viewModel.rowStatus[row]);
		bindings.BindTextColor(window + "StaticTextStatus" + number, viewModel.rowStatusColor[row]);
		bindings.BindVisible(window + "ButtonMute" + number, viewModel.muteShown[row]);
		bindings.BindVisible(window + "ButtonUnMute" + number, viewModel.unmuteShown[row]);
		bindings.BindCommand(window + "ButtonMute" + number, viewModel.mute[row]);
		bindings.BindCommand(window + "ButtonUnMute" + number, viewModel.unmute[row]);
	}
}
}
