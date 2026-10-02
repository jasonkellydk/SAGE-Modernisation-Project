export module games.generalszh.shell.options.options_view_model;
import std;

export import games.generalszh.shell.model.shell_model;
export import games.generalszh.shell.options.user_options;

// The options menu's logic (the original OptionsMenu.cpp without windows):
// its controls show the player's options when it opens; Accept keeps what
// the controls say (the host saves Options.ini and applies it), Back drops
// it, Defaults puts the original's defaults in the controls; the advanced
// display panel opens when the detail box picks Custom and closes with its own
// accept (keeping its checks) or back (restoring them). Views bind to it.
export namespace generalszh::shell
{
class OptionsViewModel
{
public:
	// `detailNames`: the detail box's items (GUI:Low, GUI:Medium, GUI:High, GUI:VeryHigh, GUI:Custom, localized);
	// `resolutions`: the display modes the resolution box offers; `display`: the one the game runs at,
	// which the box shows (added if it is not a listed mode), not the one Options.ini names
	// (OptionsMenuInit, TheSuperHackers' fixes). None given: the box shows Options.ini's.
	// `addresses`: this machine's IPv4 addresses, as the LAN and online address boxes offer them.
	OptionsViewModel(ShellModel &model, UserOptions &options, const OptionDefaults &defaults, std::vector<std::u16string> detailNames = {},
		std::vector<std::pair<int, int>> resolutions = {}, std::optional<std::pair<int, int>> display = std::nullopt,
		std::vector<std::string> addresses = {})
		: m_model(model), m_options(options), m_defaults(defaults), m_resolutions(std::move(resolutions)), m_display(display),
		  m_addresses(std::move(addresses))
	{
		std::vector<std::u16string> shownAddresses;
		for (const std::string &address : m_addresses)
			shownAddresses.emplace_back(address.begin(), address.end());
		addressItems.Set(shownAddresses);
		onlineAddressItems.Set(std::move(shownAddresses));
		if (m_display && std::find(m_resolutions.begin(), m_resolutions.end(), *m_display) == m_resolutions.end())
			m_resolutions.push_back(*m_display);
		detailItems.Set(std::move(detailNames));
		std::vector<std::u16string> modes;
		for (const auto &[width, height] : m_resolutions)
		{
			const std::string text = std::to_string(width) + " x " + std::to_string(height);
			modes.emplace_back(text.begin(), text.end());
		}
		resolutionItems.Set(std::move(modes));
		// GCM_SELECTED on the detail box: Custom shows the advanced panel.
		m_detail = detail.Subscribe([this](int index) {
			if (index == CustomLod && m_shown)
				CustomDetail();
		});
		accept.SetAction([this] { Accept(); });
		back.SetAction([this] { Close(); });
		defaultsCommand.SetAction([this] { Show(DefaultOptions(m_defaults)); });
		advancedAccept.SetAction([this] {
			m_advancedKept = Read();
			advancedOpen.Set(false);
		});
		advancedBack.SetAction([this] {
			// cancelAdvancedOptions: its checks as they were and the detail back to the saved level.
			ShowAdvanced(m_advancedKept);
			m_shown = false;
			detail.Set(m_options.staticLod);
			m_shown = true;
			advancedOpen.Set(false);
		});
		m_open = model.optionsOpen.Subscribe([this](bool open) {
			if (open)
				Show(m_options); // OptionsMenuInit: the controls show the saved options
		});
	}

	~OptionsViewModel()
	{
		m_model.optionsOpen.Unsubscribe(m_open);
		detail.Unsubscribe(m_detail);
	}
	OptionsViewModel(const OptionsViewModel &) = delete;
	OptionsViewModel &operator=(const OptionsViewModel &) = delete;

	// Sliders (percent, as the layout's ranges) and check boxes.
	engine::gui::mvvm::Observable<int> musicVolume, soundVolume, speechVolume, scrollSpeed, gamma, particleCap, textureQuality;
	engine::gui::mvvm::Observable<bool> alternateMouse, retaliation, doubleClickAttackMove;
	engine::gui::mvvm::Observable<bool> shadowVolumes, shadowDecals, cloudShadows, groundLighting, smoothWater, extraAnimations, noDynamicLod,
		heatEffects, props, buildingOcclusion;
	engine::gui::mvvm::Observable<bool> advancedOpen{false};
	// Combo boxes: their items and the picked one.
	engine::gui::mvvm::Observable<std::vector<std::u16string>> detailItems, resolutionItems;
	engine::gui::mvvm::Observable<int> detail{-1}, resolution{-1};
	engine::gui::mvvm::Observable<std::vector<std::u16string>> addressItems, onlineAddressItems;
	engine::gui::mvvm::Observable<int> lanAddress{-1}, onlineAddress{-1};

	engine::gui::mvvm::Command accept, back, defaultsCommand, advancedAccept, advancedBack;

	// A custom detail level was picked (GCM_SELECTED on the detail box).
	void CustomDetail()
	{
		m_advancedKept = Read();
		advancedOpen.Set(true);
	}

private:
	void Show(const UserOptions &options)
	{
		m_shown = false;
		detail.Set(options.staticLod);
		int mode = -1;
		const std::pair<int, int> shown = m_display.value_or(std::pair{options.resolutionWidth, options.resolutionHeight});
		for (std::size_t index = 0; index < m_resolutions.size(); ++index)
			if (m_resolutions[index] == shown)
				mode = static_cast<int>(index);
		resolution.Set(mode);
		// OptionsMenuInit: the address Options.ini names, else the first (which becomes the one kept).
		const auto pick = [this](const std::string &chosen) {
			for (std::size_t index = 0; index < m_addresses.size(); ++index)
				if (m_addresses[index] == chosen)
					return static_cast<int>(index);
			return m_addresses.empty() ? -1 : 0;
		};
		lanAddress.Set(pick(options.lanAddress));
		onlineAddress.Set(pick(options.onlineAddress));
		m_shown = true;
		musicVolume.Set(options.musicVolume);
		soundVolume.Set(options.SoundSlider());
		speechVolume.Set(options.speechVolume);
		scrollSpeed.Set(options.scrollFactor);
		gamma.Set(options.gamma);
		alternateMouse.Set(options.alternateMouse);
		retaliation.Set(options.retaliation);
		doubleClickAttackMove.Set(options.doubleClickAttackMove);
		ShowAdvanced(options);
		m_advancedKept = options;
		advancedOpen.Set(false);
	}

	void ShowAdvanced(const UserOptions &options)
	{
		shadowVolumes.Set(options.shadowVolumes);
		shadowDecals.Set(options.shadowDecals);
		cloudShadows.Set(options.cloudShadows);
		groundLighting.Set(options.groundLighting);
		smoothWater.Set(options.smoothWater);
		extraAnimations.Set(options.extraAnimations);
		noDynamicLod.Set(!options.dynamicLod);
		heatEffects.Set(options.heatEffects);
		props.Set(options.trees);
		buildingOcclusion.Set(options.buildingOcclusion);
		particleCap.Set(options.maxParticleCount);
		textureQuality.Set(2 - options.textureReduction); // LowResSlider: 2 - TextureReduction
	}

	// What the controls say, over the saved options (fields no control shows stay).
	UserOptions Read() const
	{
		UserOptions options = m_options;
		options.musicVolume = musicVolume.Get();
		// saveOptions: the slider split into 2D and 3D by the relative volume, only when it moved.
		if (soundVolume.Get() != m_options.SoundSlider())
			options.SetSoundSlider(soundVolume.Get(), m_defaults.relative2DVolume);
		options.speechVolume = speechVolume.Get();
		if (scrollSpeed.Get() > 0)
			options.scrollFactor = scrollSpeed.Get();
		options.gamma = gamma.Get();
		options.alternateMouse = alternateMouse.Get();
		options.retaliation = retaliation.Get();
		options.doubleClickAttackMove = doubleClickAttackMove.Get();
		options.shadowVolumes = shadowVolumes.Get();
		options.shadowDecals = shadowDecals.Get();
		options.cloudShadows = cloudShadows.Get();
		options.groundLighting = groundLighting.Get();
		options.smoothWater = smoothWater.Get();
		options.extraAnimations = extraAnimations.Get();
		options.dynamicLod = !noDynamicLod.Get();
		options.heatEffects = heatEffects.Get();
		options.trees = props.Get();
		options.buildingOcclusion = buildingOcclusion.Get();
		options.maxParticleCount = particleCap.Get();
		options.textureReduction = 2 - textureQuality.Get();
		if (detail.Get() >= 0)
			options.staticLod = detail.Get();
		if (resolution.Get() >= 0 && static_cast<std::size_t>(resolution.Get()) < m_resolutions.size())
			std::tie(options.resolutionWidth, options.resolutionHeight) = m_resolutions[static_cast<std::size_t>(resolution.Get())];
		if (lanAddress.Get() >= 0 && static_cast<std::size_t>(lanAddress.Get()) < m_addresses.size())
			options.lanAddress = m_addresses[static_cast<std::size_t>(lanAddress.Get())];
		if (onlineAddress.Get() >= 0 && static_cast<std::size_t>(onlineAddress.Get()) < m_addresses.size())
			options.onlineAddress = m_addresses[static_cast<std::size_t>(onlineAddress.Get())];
		return options;
	}

	void Accept()
	{
		m_options = Read();
		m_model.optionsSaved.Set(m_model.optionsSaved.Get() + 1); // the host writes Options.ini and applies it
		Close();
	}

	void Close() { m_model.optionsOpen.Set(false); }

	ShellModel &m_model;
	UserOptions &m_options;
	OptionDefaults m_defaults;
	UserOptions m_advancedKept;
	std::vector<std::pair<int, int>> m_resolutions;
	std::vector<std::string> m_addresses;
	std::optional<std::pair<int, int>> m_display;
	bool m_shown{false}; // the controls show the options (a detail change is the player's)
	engine::gui::mvvm::SubscriptionId m_open{0};
	engine::gui::mvvm::SubscriptionId m_detail{0};
};
}
