export module games.generalszh.shell.options.options_view;
import std;

export import games.generalszh.shell.options.options_view_model;
export import Engine.UI.WND.Bindings;

// Binds Window/Menus/OptionsMenu.wnd to the OptionsViewModel: the layout
// shows while the options are open, its sliders and check boxes follow the
// view model both ways, its buttons run its commands.
export namespace generalszh::shell
{
inline void BindOptionsView(Engine::UI::WND::WNDBindings &bindings, OptionsViewModel &viewModel, ShellModel &model)
{
	bindings.BindVisible("OptionsMenu.wnd:OptionsMenuParent", model.optionsOpen);
	bindings.BindVisible("OptionsMenu.wnd:WinAdvancedDisplayOptions", viewModel.advancedOpen);
	bindings.BindComboBox("OptionsMenu.wnd:ComboBoxDetail", viewModel.detailItems, viewModel.detail);
	bindings.BindComboBox("OptionsMenu.wnd:ComboBoxResolution", viewModel.resolutionItems, viewModel.resolution);
	bindings.BindComboBox("OptionsMenu.wnd:ComboBoxIP", viewModel.addressItems, viewModel.lanAddress);
	bindings.BindComboBox("OptionsMenu.wnd:ComboBoxOnlineIP", viewModel.onlineAddressItems, viewModel.onlineAddress);
	bindings.BindSlider("OptionsMenu.wnd:SliderMusicVolume", viewModel.musicVolume);
	bindings.BindSlider("OptionsMenu.wnd:SliderSFXVolume", viewModel.soundVolume);
	bindings.BindSlider("OptionsMenu.wnd:SliderVoiceVolume", viewModel.speechVolume);
	bindings.BindSlider("OptionsMenu.wnd:SliderScrollSpeed", viewModel.scrollSpeed);
	bindings.BindSlider("OptionsMenu.wnd:SliderGamma", viewModel.gamma);
	bindings.BindSlider("OptionsMenu.wnd:ParticleCapSlider", viewModel.particleCap);
	bindings.BindSlider("OptionsMenu.wnd:LowResSlider", viewModel.textureQuality);
	bindings.BindChecked("OptionsMenu.wnd:CheckAlternateMouse", viewModel.alternateMouse);
	bindings.BindChecked("OptionsMenu.wnd:Retaliation", viewModel.retaliation);
	bindings.BindChecked("OptionsMenu.wnd:CheckDoubleClickAttackMove", viewModel.doubleClickAttackMove);
	bindings.BindChecked("OptionsMenu.wnd:Check3DShadows", viewModel.shadowVolumes);
	bindings.BindChecked("OptionsMenu.wnd:Check2DShadows", viewModel.shadowDecals);
	bindings.BindChecked("OptionsMenu.wnd:CheckCloudShadows", viewModel.cloudShadows);
	bindings.BindChecked("OptionsMenu.wnd:CheckGroundLighting", viewModel.groundLighting);
	bindings.BindChecked("OptionsMenu.wnd:CheckSmoothWater", viewModel.smoothWater);
	bindings.BindChecked("OptionsMenu.wnd:CheckExtraAnimations", viewModel.extraAnimations);
	bindings.BindChecked("OptionsMenu.wnd:CheckNoDynamicLOD", viewModel.noDynamicLod);
	bindings.BindChecked("OptionsMenu.wnd:CheckHeatEffects", viewModel.heatEffects);
	bindings.BindChecked("OptionsMenu.wnd:CheckShowProps", viewModel.props);
	bindings.BindChecked("OptionsMenu.wnd:CheckBehindBuilding", viewModel.buildingOcclusion);
	bindings.BindCommand("OptionsMenu.wnd:ButtonAccept", viewModel.accept);
	bindings.BindCommand("OptionsMenu.wnd:ButtonBack", viewModel.back);
	bindings.BindCommand("OptionsMenu.wnd:ButtonDefaults", viewModel.defaultsCommand);
	bindings.BindCommand("OptionsMenu.wnd:ButtonAdvanceAccept", viewModel.advancedAccept);
	bindings.BindCommand("OptionsMenu.wnd:ButtonAdvanceBack", viewModel.advancedBack);
}
}
