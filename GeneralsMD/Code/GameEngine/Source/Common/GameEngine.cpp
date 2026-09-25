/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// GameEngine.cpp /////////////////////////////////////////////////////////////////////////////////
// Implementation of the Game Engine singleton
// Author: Michael S. Booth, April 2001

#include "PreRTS.h"
import engine.profiling;
import engine.debug;	// This must go first in EVERY cpp file in the GameEngine
#include <stdexcept>
#include <utility>
import engine.navigation.diagnostics.frame_capture;
import engine.platform;

#include "Common/ActionManager.h"
#include "Common/AudioAffect.h"
#include "Common/BuildAssistant.h"
#include "Common/CRCDebug.h"
#include "Common/FramePacer.h"
#include "Common/Radar.h"
#include "Common/PlayerTemplate.h"
#include "Common/Team.h"
#include "Common/PlayerList.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "Common/MessageStream.h"
#include "Common/ThingFactory.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/GlobalData.h"

#include "Common/RandomValue.h"
#include "Common/NameKeyGenerator.h"
#include "Common/ModuleFactory.h"

#include "Common/GameState.h"
#include "Common/GameStateMap.h"
#include "Common/Science.h"
#include "Common/FunctionLexicon.h"
#include "Common/CommandLine.h"
#include "Common/DamageFX.h"
#include "Common/MultiplayerSettings.h"
#include "Common/Recorder.h"
#include "Common/SpecialPower.h"
#include "Common/TerrainTypes.h"
#include "Common/Upgrade.h"
#include "Common/OptionPreferences.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/GameLOD.h"
#include "Common/RuntimeConfig.h"
#include "Common/GameCommon.h"	// FOR THE ALLOW_DEBUG_CHEATS_IN_RELEASE #define

#include "GameLogic/Armor.h"
#include "GameLogic/AI.h"
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/Damage.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/RankInfo.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"

#include "GameClient/ClientInstance.h"
#include "GameClient/FXList.h"
#include "GameClient/GameClient.h"
#include "GameClient/Keyboard.h"
#include "GameClient/Shell.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/Water.h"
#include "GameClient/TerrainRoads.h"
#include "GameClient/MetaEvent.h"
#include "GameClient/MapUtil.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"

#include "GameClient/Drawable.h"
#include "GameClient/GUICallbacks.h"

#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/GameSpy/GameResultsThread.h"

#include "Common/version.h"


//-------------------------------------------------------------------------------------------------

#ifdef DEBUG_CRC
class DeepCRCSanityCheck : public SubsystemInterface
{
public:
	DeepCRCSanityCheck() {}
	virtual ~DeepCRCSanityCheck() {}

	virtual void init() {}
	virtual void reset();
	virtual void update() {}

protected:
};

DeepCRCSanityCheck *TheDeepCRCSanityCheck = nullptr;

void DeepCRCSanityCheck::reset()
{
	static Int timesThrough = 0;
	static UnsignedInt lastCRC = 0;

	AsciiString fname;
	fname.format("%sCRCAfter%dMaps.dat", TheGlobalData->getPath_UserData().str(), timesThrough);
	UnsignedInt thisCRC = TheGameLogic->getCRC( CRC_RECALC, fname );

	engine::debug::log_info("DeepCRCSanityCheck: CRC is %X", thisCRC);
	engine::debug::invariant((timesThrough == 0 || thisCRC == lastCRC), "timesThrough == 0 || thisCRC == lastCRC", __FILE__, __LINE__, "CRC after reset did not match beginning CRC!\nNetwork games won't work after this.\nOld: 0x%8.8X, New: 0x%8.8X",
		lastCRC, thisCRC);
	lastCRC = thisCRC;

	timesThrough++;
}
#endif // DEBUG_CRC

//-------------------------------------------------------------------------------------------------
/// The GameEngine singleton instance
GameEngine *TheGameEngine = nullptr;

//-------------------------------------------------------------------------------------------------
SubsystemInterfaceList* TheSubsystemList = nullptr;

//-------------------------------------------------------------------------------------------------
template<class SUBSYSTEM>
void initSubsystem(
	SUBSYSTEM*& sysref,
	AsciiString name,
	SUBSYSTEM* sys,
	Xfer *pXfer,
	const char* path1 = nullptr,
	const char* path2 = nullptr)
{
	sysref = sys;
	TheSubsystemList->initSubsystem(sys, path1, path2, pXfer, name);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
static void updateTGAtoDDS();

//-------------------------------------------------------------------------------------------------
static void updateWindowTitle()
{
	// TheSuperHackers @tweak Now prints product and version information in the Window title.

	engine::debug::invariant((TheVersion != nullptr), "TheVersion != nullptr", __FILE__, __LINE__, "TheVersion is null");
	engine::debug::invariant((TheGameText != nullptr), "TheGameText != nullptr", __FILE__, __LINE__, "TheGameText is null");

	UnicodeString title;

	if (rts::ClientInstance::getInstanceId() > 1u)
	{
		UnicodeString str;
		str.format(L"Instance:%.2u", rts::ClientInstance::getInstanceId());
		title.concat(str);
	}

	UnicodeString productString = TheVersion->getUnicodeProductString();

	if (!productString.isEmpty())
	{
		if (!title.isEmpty())
			title.concat(L" ");
		title.concat(productString);
	}

#if RTS_GENERALS
	const WideChar* defaultGameTitle = L"Command and Conquer Generals";
#elif RTS_ZEROHOUR
	const WideChar* defaultGameTitle = L"Command and Conquer Generals Zero Hour";
#endif
	UnicodeString gameTitle = TheGameText->FETCH_OR_SUBSTITUTE("GUI:Command&ConquerGenerals", defaultGameTitle);

	if (!gameTitle.isEmpty())
	{
		UnicodeString gameTitleFinal;
		UnicodeString gameVersion = TheVersion->getUnicodeVersion();

		if (productString.isEmpty())
		{
			gameTitleFinal = gameTitle;
		}
		else
		{
			UnicodeString gameTitleFormat = TheGameText->FETCH_OR_SUBSTITUTE("Version:GameTitle", L"for %ls");
			gameTitleFinal.format(gameTitleFormat.str(), gameTitle.str());
		}

		if (!title.isEmpty())
			title.concat(L" ");
		title.concat(gameTitleFinal.str());
		title.concat(L" ");
		title.concat(gameVersion.str());
	}

	if (!title.isEmpty())
	{
		AsciiString titleA;
		titleA.translate(title);	//get ASCII version for Win 9x

		if (TheGameEngine != nullptr && TheGameEngine->mainWindow() != nullptr)
			TheGameEngine->mainWindow()->set_title(titleA.str());
	}
}

//-------------------------------------------------------------------------------------------------
GameEngine::GameEngine(std::unique_ptr<engine::platform::IPlatform> platform)
	: m_platform(std::move(platform))
{
	engine::debug::invariant((m_platform != nullptr), "m_platform != nullptr", __FILE__, __LINE__, "GameEngine requires a platform implementation");
	// initialize to non garbage values
	m_logicTimeAccumulator = 0.0f;
	m_quitting = FALSE;
	m_isActive = FALSE;

}

engine::platform::IPlatform& GameEngine::platform() noexcept { return *m_platform; }

engine::platform::IWindow& GameEngine::createMainWindow(const engine::platform::WindowConfig& config)
{
	m_mainWindow = m_platform->windows().create(config);
	if (!m_mainWindow)
		throw std::runtime_error("Unable to create the game window");
	return *m_mainWindow;
}

engine::platform::IWindow* GameEngine::mainWindow() noexcept { return m_mainWindow.get(); }

//-------------------------------------------------------------------------------------------------
GameEngine::~GameEngine()
{
	// Resolve diagnostic module names before destroying the name table.
	navigation::diagnostics::frameCapture().finish();
	//extern std::vector<std::string>	preloadTextureNamesGlobalHack;
	//preloadTextureNamesGlobalHack.clear();

	delete TheMapCache;
	TheMapCache = nullptr;

//	delete TheShell;
//	TheShell = nullptr;

	TheGameResultsQueue->endThreads();

	// TheSuperHackers @fix helmutbuhler 03/06/2025
	// Reset all subsystems before deletion to prevent crashing due to cross dependencies.
	reset();

	TheSubsystemList->shutdownAll();
	delete TheSubsystemList;
	TheSubsystemList = nullptr;

	delete TheSkirmishGameInfo;
	TheSkirmishGameInfo = nullptr;

	delete TheChallengeGameInfo;
	TheChallengeGameInfo = nullptr;

	delete TheNetwork;
	TheNetwork = nullptr;

	delete TheCommandList;
	TheCommandList = nullptr;

	delete TheNameKeyGenerator;
	TheNameKeyGenerator = nullptr;

	delete TheFileSystem;
	TheFileSystem = nullptr;

	delete TheGameLODManager;
	TheGameLODManager = nullptr;

	Drawable::killStaticImages();

}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isTimeFrozen()
{
	// TheSuperHackers @fix The time can no longer be frozen in Network games. It would disconnect the player.
	if (TheNetwork != nullptr)
		return false;

	if (TheTacticalView != nullptr)
	{
		if (TheTacticalView->isTimeFrozen() && !TheTacticalView->isCameraMovementFinished())
			return true;
	}

	if (TheScriptEngine != nullptr)
	{
		if (TheScriptEngine->isTimeFrozenDebug() || TheScriptEngine->isTimeFrozenScript())
			return true;
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isGameHalted()
{
	if (TheNetwork != nullptr)
	{
		if (TheNetwork->isStalling())
			return true;
	}
	else
	{
		if (TheGameLogic != nullptr && TheGameLogic->isGamePaused())
			return true;
	}

	return false;
}

/** -----------------------------------------------------------------------------------------------
 * Initialize the game engine by initializing the GameLogic and GameClient.
 */
void GameEngine::init()
{
	try {
		//create an INI object to use for loading stuff
		INI ini;

		if (TheVersion)
		{
			engine::debug::log_info("================================================================================");
			engine::debug::log_info("Generals version %s", TheVersion->getAsciiVersion().str());
			engine::debug::log_info("Build date: %s", TheVersion->getAsciiBuildTime().str());
			engine::debug::log_info("Build location: %s", TheVersion->getAsciiBuildLocation().str());
			engine::debug::log_info("Build user: %s", TheVersion->getAsciiBuildUser().str());
			engine::debug::log_info("Build git revision: %s", TheVersion->getAsciiGitCommitCount().str());
			engine::debug::log_info("Build git version: %s", TheVersion->getAsciiGitTagOrHash().str());
			engine::debug::log_info("Build git commit time: %s", TheVersion->getAsciiGitCommitTime().str());
			engine::debug::log_info("Build git commit author: %s", Version::getGitCommitAuthorName());
			engine::debug::log_info("================================================================================");
		}







		TheSubsystemList = MSGNEW("GameEngineSubsystem") SubsystemInterfaceList;

		TheSubsystemList->addSubsystem(this);

		// initialize the random number system
		InitRandom();

		// Create the low-level file system interface
		TheFileSystem = createFileSystem();

		// not part of the subsystem list, because it should normally never be reset!
		TheNameKeyGenerator = MSGNEW("GameEngineSubsystem") NameKeyGenerator;
		TheNameKeyGenerator->init();




		// not part of the subsystem list, because it should normally never be reset!
		TheCommandList = MSGNEW("GameEngineSubsystem") CommandList;
		TheCommandList->init();



		XferCRC xferCRC;
		xferCRC.open("lightCRC");


		initSubsystem(TheLocalFileSystem, "TheLocalFileSystem", createLocalFileSystem(), nullptr);




		initSubsystem(TheArchiveFileSystem, "TheArchiveFileSystem", createArchiveFileSystem(), nullptr); // this MUST come after TheLocalFileSystem creation



		engine::debug::invariant((TheWritableGlobalData), "TheWritableGlobalData", __FILE__, __LINE__, "TheWritableGlobalData expected to be created");
		initSubsystem(TheWritableGlobalData, "TheWritableGlobalData", TheWritableGlobalData, &xferCRC, "Data\\INI\\Default\\GameData", "Data\\INI\\GameData");
		TheWritableGlobalData->parseCustomDefinition();





	#if defined(RTS_DEBUG)
		// If we're in Debug, load the Debug settings as well.
		ini.loadFileDirectory( "Data\\INI\\GameDataDebug", INI_LOAD_OVERWRITE, nullptr );
	#endif

		// special-case: parse command-line parameters after loading global data
		CommandLine::parseCommandLineForEngineInit();

		TheArchiveFileSystem->loadMods();

		// doesn't require resets so just create a single instance here.
		TheGameLODManager = MSGNEW("GameEngineSubsystem") GameLODManager;
		TheGameLODManager->init();

		// after parsing the command line, we may want to perform dds stuff. Do that here.
		if (TheGlobalData->m_shouldUpdateTGAToDDS) {
			// update any out of date targas here.
			updateTGAtoDDS();
		}

		// read the water settings from INI (must do prior to initing GameClient, apparently)
		ini.loadFileDirectory( "Data\\INI\\Default\\Water", INI_LOAD_OVERWRITE, &xferCRC );
		ini.loadFileDirectory( "Data\\INI\\Water", INI_LOAD_OVERWRITE, &xferCRC );
		ini.loadFileDirectory( "Data\\INI\\Default\\Weather", INI_LOAD_OVERWRITE, &xferCRC );
		ini.loadFileDirectory( "Data\\INI\\Weather", INI_LOAD_OVERWRITE, &xferCRC );





#ifdef DEBUG_CRC
		initSubsystem(TheDeepCRCSanityCheck, "TheDeepCRCSanityCheck", MSGNEW("GameEngineSubystem") DeepCRCSanityCheck, nullptr);
#endif // DEBUG_CRC
		initSubsystem(TheGameText, "TheGameText", CreateGameTextInterface(), nullptr);
		updateWindowTitle();



#if RETAIL_COMPATIBLE_CRC
		if (xferCRC.getCRC() == 0xA1E7F8E6)
			TheNameKeyGenerator->verifyNameKeyID(1);
#endif

		initSubsystem(TheScienceStore,"TheScienceStore", MSGNEW("GameEngineSubsystem") ScienceStore(), &xferCRC, "Data\\INI\\Default\\Science", "Data\\INI\\Science");
		initSubsystem(TheMultiplayerSettings,"TheMultiplayerSettings", MSGNEW("GameEngineSubsystem") MultiplayerSettings(), &xferCRC, "Data\\INI\\Default\\Multiplayer", "Data\\INI\\Multiplayer");
		initSubsystem(TheTerrainTypes,"TheTerrainTypes", MSGNEW("GameEngineSubsystem") TerrainTypeCollection(), &xferCRC, "Data\\INI\\Default\\Terrain", "Data\\INI\\Terrain");
		initSubsystem(TheTerrainRoads,"TheTerrainRoads", MSGNEW("GameEngineSubsystem") TerrainRoadCollection(), &xferCRC, "Data\\INI\\Default\\Roads", "Data\\INI\\Roads");
		initSubsystem(TheGlobalLanguageData,"TheGlobalLanguageData",MSGNEW("GameEngineSubsystem") GlobalLanguage, nullptr); // must be before the game text
		TheGlobalLanguageData->parseCustomDefinition();
		initSubsystem(TheAudio,"TheAudio", createAudioManager(TheGlobalData->m_headless), nullptr);

#if RTS_ZEROHOUR && RETAIL_COMPATIBLE_CRC
		TheNameKeyGenerator->syncNameKeyID();
#endif



		initSubsystem(TheFunctionLexicon,"TheFunctionLexicon", createFunctionLexicon(), nullptr);
		initSubsystem(TheModuleFactory,"TheModuleFactory", createModuleFactory(), nullptr);
		initSubsystem(TheMessageStream,"TheMessageStream", createMessageStream(), nullptr);
		initSubsystem(TheSidesList,"TheSidesList", MSGNEW("GameEngineSubsystem") SidesList(), nullptr);
		initSubsystem(TheCaveSystem,"TheCaveSystem", MSGNEW("GameEngineSubsystem") CaveSystem(), nullptr);
		initSubsystem(TheRankInfoStore,"TheRankInfoStore", MSGNEW("GameEngineSubsystem") RankInfoStore(), &xferCRC, nullptr, "Data\\INI\\Rank");
		initSubsystem(ThePlayerTemplateStore,"ThePlayerTemplateStore", MSGNEW("GameEngineSubsystem") PlayerTemplateStore(), &xferCRC, "Data\\INI\\Default\\PlayerTemplate", "Data\\INI\\PlayerTemplate");
		initSubsystem(TheParticleSystemManager,"TheParticleSystemManager", createParticleSystemManager(TheGlobalData->m_headless), nullptr);



		initSubsystem(TheFXListStore,"TheFXListStore", MSGNEW("GameEngineSubsystem") FXListStore(), &xferCRC, "Data\\INI\\Default\\FXList", "Data\\INI\\FXList");
		initSubsystem(TheWeaponStore,"TheWeaponStore", MSGNEW("GameEngineSubsystem") WeaponStore(), &xferCRC, nullptr, "Data\\INI\\Weapon");
		initSubsystem(TheObjectCreationListStore,"TheObjectCreationListStore", MSGNEW("GameEngineSubsystem") ObjectCreationListStore(), &xferCRC, "Data\\INI\\Default\\ObjectCreationList", "Data\\INI\\ObjectCreationList");
		initSubsystem(TheLocomotorStore,"TheLocomotorStore", MSGNEW("GameEngineSubsystem") LocomotorStore(), &xferCRC, nullptr, "Data\\INI\\Locomotor");
		initSubsystem(TheSpecialPowerStore,"TheSpecialPowerStore", MSGNEW("GameEngineSubsystem") SpecialPowerStore(), &xferCRC, "Data\\INI\\Default\\SpecialPower", "Data\\INI\\SpecialPower");
		initSubsystem(TheDamageFXStore,"TheDamageFXStore", MSGNEW("GameEngineSubsystem") DamageFXStore(), &xferCRC, nullptr, "Data\\INI\\DamageFX");
		initSubsystem(TheArmorStore,"TheArmorStore", MSGNEW("GameEngineSubsystem") ArmorStore(), &xferCRC, nullptr, "Data\\INI\\Armor");
		initSubsystem(TheBuildAssistant,"TheBuildAssistant", MSGNEW("GameEngineSubsystem") BuildAssistant, nullptr);





		initSubsystem(TheThingFactory,"TheThingFactory", createThingFactory(), &xferCRC, "Data\\INI\\Default\\Object", "Data\\INI\\Object");



#if RETAIL_COMPATIBLE_CRC
		if (xferCRC.getCRC() == 0x6209AF6E)
			TheNameKeyGenerator->verifyNameKeyID(2265);
#endif

		initSubsystem(TheUpgradeCenter,"TheUpgradeCenter", MSGNEW("GameEngineSubsystem") UpgradeCenter, &xferCRC, "Data\\INI\\Default\\Upgrade", "Data\\INI\\Upgrade");
		initSubsystem(TheGameClient,"TheGameClient", createGameClient(*m_platform, m_mainWindow.get()), nullptr);




		initSubsystem(TheAI,"TheAI", MSGNEW("GameEngineSubsystem") AI(), &xferCRC,  "Data\\INI\\Default\\AIData", "Data\\INI\\AIData");
		initSubsystem(TheGameLogic,"TheGameLogic", createGameLogic(*m_platform), nullptr);
		initSubsystem(TheTeamFactory,"TheTeamFactory", MSGNEW("GameEngineSubsystem") TeamFactory(), nullptr);
		initSubsystem(TheCrateSystem,"TheCrateSystem", MSGNEW("GameEngineSubsystem") CrateSystem(), &xferCRC, "Data\\INI\\Default\\Crate", "Data\\INI\\Crate");
		initSubsystem(ThePlayerList,"ThePlayerList", MSGNEW("GameEngineSubsystem") PlayerList(), nullptr);
		initSubsystem(TheRecorder,"TheRecorder", createRecorder(), nullptr);
		initSubsystem(TheRadar,"TheRadar", createRadar(TheGlobalData->m_headless), nullptr);
		initSubsystem(TheVictoryConditions,"TheVictoryConditions", createVictoryConditions(), nullptr);





		AsciiString fname;
		fname.format("Data\\%s\\CommandMap", GetGameLanguage().str());
		initSubsystem(TheMetaMap,"TheMetaMap", MSGNEW("GameEngineSubsystem") MetaMap(), nullptr, fname.str(), "Data\\INI\\CommandMap");

#if defined(RTS_DEBUG)
		ini.loadFileDirectory("Data\\INI\\CommandMapDebug", INI_LOAD_MULTIFILE, nullptr);
#endif

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
		ini.loadFileDirectory("Data\\INI\\CommandMapDemo", INI_LOAD_MULTIFILE, nullptr);
#endif

		TheMetaMap->generateMetaMap();
		TheMetaMap->verifyMetaMap();


		initSubsystem(TheActionManager,"TheActionManager", MSGNEW("GameEngineSubsystem") ActionManager(), nullptr);
		initSubsystem(TheGameStateMap,"TheGameStateMap", MSGNEW("GameEngineSubsystem") GameStateMap, nullptr );
		initSubsystem(TheGameState,"TheGameState", MSGNEW("GameEngineSubsystem") GameState, nullptr );

		// Create the interface for sending game results
		initSubsystem(TheGameResultsQueue,"TheGameResultsQueue", GameResultsInterface::createNewGameResultsInterface(), nullptr);




		xferCRC.close();
		TheWritableGlobalData->m_iniCRC = xferCRC.getCRC();
		engine::debug::log_info("INI CRC is 0x%8.8X", TheGlobalData->m_iniCRC);

		TheSubsystemList->postProcessLoadAll();

		TheFramePacer->setLogicTimeScaleFps(TheGlobalData->m_framesPerSecondLimit);

		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_musicOn, AudioAffect_Music);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_soundsOn, AudioAffect_Sound);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_sounds3DOn, AudioAffect_Sound3D);
		TheAudio->setOn(TheGlobalData->m_audioOn && TheGlobalData->m_speechOn, AudioAffect_Speech);

		// We're not in a network game yet, so set the network singleton to nullptr.
		TheNetwork = nullptr;

		//Create a default ini file for options if it doesn't already exist.
		//OptionPreferences prefs( TRUE );

		// If we turn m_quitting to FALSE here, then we throw away any requests to quit that
		// took place during loading. :-\ - jkmcd
		// If this really needs to take place, please make sure that pressing cancel on the audio
		// load music dialog will still cause the game to quit.
		// m_quitting = FALSE;

		// initialize the MapCache
		TheMapCache = MSGNEW("GameEngineSubsystem") MapCache;
		TheMapCache->updateCache();




		if (TheGlobalData->m_buildMapCache)
		{
			// just quit, since the map cache has already updated
			//populateMapListbox(nullptr, true, true);
			m_quitting = TRUE;
		}

		// load the initial shell screen
		//TheShell->push( "Menus/MainMenu.wnd" );

		// This allows us to run a map from the command line
		if (TheGlobalData->m_initialFile.isEmpty() == FALSE)
		{
			AsciiString fname = TheGlobalData->m_initialFile;
			fname.toLower();

			if (fname.endsWithNoCase(".map"))
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
				TheWritableGlobalData->m_playIntro = FALSE;
				TheWritableGlobalData->m_pendingFile = TheGlobalData->m_initialFile;

				// shutdown the top, but do not pop it off the stack
	//			TheShell->hideShell();

				// send a message to the logic for a new game
				GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
				msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
				msg->appendIntegerArgument(DIFFICULTY_NORMAL);
				msg->appendIntegerArgument(0);
				InitRandom(0);
			}
		}

		//
		if (TheMapCache && TheGlobalData->m_shellMapOn)
		{
			AsciiString lowerName = TheGlobalData->m_shellMapName;
			lowerName.toLower();

			MapCache::const_iterator it = TheMapCache->find(lowerName);
			if (it == TheMapCache->end())
			{
				TheWritableGlobalData->m_shellMapOn = FALSE;
			}
		}

	}
	catch (ErrorCode ec)
	{
		if (ec == ERROR_INVALID_D3D)
		{
			engine::debug::panic("%s: %s", "ERROR:D3DFailurePrompt", "ERROR:D3DFailureMessage");
		}
	}
	catch (INIException e)
	{
		if (e.mFailureMessage)
			engine::debug::panic(e.mFailureMessage);
		else
			engine::debug::panic("Uncaught Exception during initialization.");

	}
	catch (...)
	{
		engine::debug::panic("Uncaught Exception during initialization.");
	}

	resetSubsystems();

	HideControlBar();
}

/** -----------------------------------------------------------------------------------------------
	* Reset all necessary parts of the game engine to be ready to accept new game data
	*/
void GameEngine::reset()
{

	WindowLayout *background = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	engine::debug::invariant((background), "background", __FILE__, __LINE__, "We Couldn't Load Menus/BlankWindow.wnd");
	background->hide(FALSE);
	background->bringForward();
	background->getFirstWindow()->winClearStatus(WIN_STATUS_IMAGE);
	Bool deleteNetwork = false;
	if (TheGameLogic->isInMultiplayerGame())
		deleteNetwork = true;

	resetSubsystems();

	if (deleteNetwork)
	{
		engine::debug::invariant((TheNetwork), "TheNetwork", __FILE__, __LINE__, "Deleting null TheNetwork!");
		delete TheNetwork;
		TheNetwork = nullptr;
	}
	if(background)
	{
		background->destroyWindows();
		deleteInstance(background);
		background = nullptr;
	}
}

/// -----------------------------------------------------------------------------------------------
void GameEngine::resetSubsystems()
{
	// TheSuperHackers @fix xezon 09/06/2025 Reset GameLogic first to purge all world objects early.
	// This avoids potentially catastrophic issues when objects and subsystems have cross dependencies.
	TheGameLogic->reset();

	TheSubsystemList->resetAll();
}

/// -----------------------------------------------------------------------------------------------
Bool GameEngine::canUpdateGameLogic(UnsignedInt logicTimeQueryFlags)
{
	// This updates the paused game status of the game logic.
	TheGameLogic->preUpdate();

	TheFramePacer->setTimeFrozen(isTimeFrozen());
	TheFramePacer->setGameHalted(isGameHalted());

	if (TheNetwork != nullptr)
	{
		return canUpdateNetworkGameLogic();
	}
	else
	{
		return canUpdateRegularGameLogic(logicTimeQueryFlags);
	}
}

/// -----------------------------------------------------------------------------------------------
Bool GameEngine::canUpdateNetworkGameLogic()
{
	engine::debug::invariant((TheNetwork != nullptr), "TheNetwork != nullptr", __FILE__, __LINE__, "TheNetwork is null");

	if (TheNetwork->isFrameDataReady())
	{
		// Important: The Network is definitely no longer stalling.
		TheFramePacer->setGameHalted(false);

		return true;
	}

	return false;
}

/// -----------------------------------------------------------------------------------------------
Bool GameEngine::canUpdateRegularGameLogic(UnsignedInt logicTimeQueryFlags)
{
	const Int logicTimeScaleFps = TheFramePacer->getActualLogicTimeScaleFps(logicTimeQueryFlags);

	if (logicTimeScaleFps <= 0)
	{
		return false;
	}

	const Int maxRenderFps = TheFramePacer->getActualFramesPerSecondLimit();

#if defined(_ALLOW_DEBUG_CHEATS_IN_RELEASE)
	const Bool useFastMode = TheGlobalData->m_TiVOFastMode;
#else	//always allow this cheat key if we're in a replay game.
	const Bool useFastMode = TheGlobalData->m_TiVOFastMode && TheGameLogic->isInReplayGame();
#endif

	if (useFastMode || logicTimeScaleFps >= maxRenderFps)
	{
		// Logic time scale is uncapped or larger equal Render FPS. Update straight away.
		return true;
	}
	else
	{
		// TheSuperHackers @tweak xezon 06/08/2025
		// The logic time step is now decoupled from the render update.
		const Real targetFrameTime = 1.0f / logicTimeScaleFps;
		m_logicTimeAccumulator += min(TheFramePacer->getUpdateTime(), targetFrameTime);

		if (m_logicTimeAccumulator >= targetFrameTime)
		{
			m_logicTimeAccumulator -= targetFrameTime;
			return true;
		}
	}

	return false;
}

/// -----------------------------------------------------------------------------------------------
/** -----------------------------------------------------------------------------------------------
 * Update the game engine by updating the GameClient and GameLogic singletons.
 */
void GameEngine::update()
{
	engine::profiling::Scope profile_scope_904("GameEngine_update");
	{
		{
			// VERIFY CRC needs to be in this code block.  Please to not pull TheGameLogic->update() inside this block.
			VERIFY_CRC

			TheRadar->update();

			/// @todo Move audio init, update, etc, into GameClient update

			{
				auto timing=navigation::diagnostics::frameCapture().measure("engine.audio",TheGameLogic->getFrame());
				TheAudio->update();
			}
			auto& capture=navigation::diagnostics::frameCapture();
			capture.beginPhase();
			TheGameClient->update();
			capture.endClient();
			TheMessageStream->propagateMessages();

			if (TheNetwork != nullptr)
			{
				TheNetwork->update();
			}
		}

		// TheSuperHackers @info Ignores frozen time because the script engine needs updating in the logic update regardless.
		if (canUpdateGameLogic(FramePacer::IgnoreFrozenTime))
		{
			auto& capture=navigation::diagnostics::frameCapture();
			capture.beginPhase();
			TheGameLogic->update();

			if (!TheFramePacer->isTimeFrozen())
			{
				auto timing=capture.measure("client.step",TheGameLogic->getFrame());
				TheGameClient->step();
			}
			capture.endLogic();
		}
	}
}

/** -----------------------------------------------------------------------------------------------
 * The "main loop" of the game engine. It will not return until the game exits.
 */
void GameEngine::execute()
{
	auto& capture=navigation::diagnostics::frameCapture();
	UnsignedInt capturedObjects=0,capturedObjectFrame=~0u;
#if defined(RTS_DEBUG)
	const std::uint64_t startTime = m_platform->clock().monotonic_nanoseconds() / 1000000000u;
#endif

	// pretty basic for now
	while( !m_quitting )
	{
		if (capture.enabled()) {
			capture.begin(TheGameLogic->getFrame(),TheGameLogic->isInGame() && !TheGameLogic->isInShellGame(),
				TheGameLogic->isGamePaused(),TheGlobalData->m_xResolution,TheGlobalData->m_yResolution,TheGlobalData->m_windowed);
			// Include diagnostic overhead in the measured frame.
			// Refresh once per simulated second rather than traversing each frame.
			if (TheGameLogic->getFrame()/LOGICFRAMES_PER_SECOND!=capturedObjectFrame) {
				capturedObjectFrame=TheGameLogic->getFrame()/LOGICFRAMES_PER_SECOND;
				capturedObjects=TheGameLogic->getObjectCount();
			}
		}

		//if (TheGlobalData->m_vTune)
		{
		}

		{

#if defined(RTS_DEBUG)
			{
				// enter only if in benchmark mode
				if (TheGlobalData->m_benchmarkTimer > 0)
				{
					const std::uint64_t currentTime = m_platform->clock().monotonic_nanoseconds() / 1000000000u;
					if (TheGlobalData->m_benchmarkTimer < currentTime - startTime)
					{
						if (TheGameLogic->isInGame())
						{
							if (TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
							{
								TheRecorder->stopRecording();
							}
							TheGameLogic->clearGameData();
						}
						TheGameEngine->setQuitting(TRUE);
					}
				}
			}
#endif

			{
				try
				{
					// compute a frame
					update();
				}
				catch (INIException e)
				{
					// Release CRASH doesn't return, so don't worry about executing additional code.
					if (e.mFailureMessage)
						engine::debug::panic(e.mFailureMessage);
					else
						engine::debug::panic("Uncaught Exception in GameEngine::update");
				}
				catch (...)
				{
					// try to save info off
					try
					{
						if (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_RECORD && TheRecorder->isMultiplayer())
							TheRecorder->cleanUpReplayFile();
					}
					catch (...)
					{
					}
					engine::debug::panic("Uncaught Exception in GameEngine::update");
				}
			}

			TheFramePacer->update();
			if (capture.end(TheGameLogic->getFrame(),capturedObjects)) setQuitting(TRUE);
		}


	}
}

/** -----------------------------------------------------------------------------------------------
	* Factory for the message stream
	*/
MessageStream *GameEngine::createMessageStream()
{
	// if you change this update the tools that use the engine systems
	// like GUIEdit, it creates a message stream to run in "test" mode
	return MSGNEW("GameEngineSubsystem") MessageStream;
}

//-------------------------------------------------------------------------------------------------
FileSystem *GameEngine::createFileSystem()
{
	return MSGNEW("GameEngineSubsystem") FileSystem;
}

//-------------------------------------------------------------------------------------------------
Bool GameEngine::isMultiplayerSession()
{
	return TheRecorder->isMultiplayer();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#define CONVERT_EXEC1	"..\\Build\\nvdxt -list buildDDS.txt -dxt5 -full -outdir Art\\Textures > buildDDS.out"

void updateTGAtoDDS()
{
	// Here's the scoop. We're going to traverse through all of the files in the Art\Textures folder
	// and determine if there are any .tga files that are newer than associated .dds files. If there
	// are, then we will re-run the compression tool on them.

	File *fp = TheLocalFileSystem->openFile("buildDDS.txt", File::WRITE | File::CREATE | File::TRUNCATE | File::TEXT);
	if (!fp) {
		return;
	}

	FilenameList files;
	TheLocalFileSystem->getFileListInDirectory("Art\\Textures\\", "", "*.tga", files, TRUE);
	FilenameList::iterator it;
	for (it = files.begin(); it != files.end(); ++it) {
		AsciiString filenameTGA = *it;
		AsciiString filenameDDS = *it;
		FileInfo infoTGA;
		TheLocalFileSystem->getFileInfo(filenameTGA, &infoTGA);

		// skip the water textures, since they need to be NOT compressed
		filenameTGA.toLower();
		if (strstr(filenameTGA.str(), "caust"))
		{
			continue;
		}
		// and the recolored stuff.
		if (strstr(filenameTGA.str(), "zhca"))
		{
			continue;
		}

		// replace tga with dds
		filenameDDS.truncateBy(3); // tga
		filenameDDS.concat("dds");

		Bool needsToBeUpdated = FALSE;
		FileInfo infoDDS;
		if (TheFileSystem->doesFileExist(filenameDDS.str())) {
			TheFileSystem->getFileInfo(filenameDDS, &infoDDS);
			if (infoTGA.timestampHigh > infoDDS.timestampHigh ||
					(infoTGA.timestampHigh == infoDDS.timestampHigh &&
					 infoTGA.timestampLow > infoDDS.timestampLow)) {
				needsToBeUpdated = TRUE;
			}
		} else {
			needsToBeUpdated = TRUE;
		}

		if (!needsToBeUpdated) {
			continue;
		}

		filenameTGA.concat("\n");
		fp->write(filenameTGA.str(), filenameTGA.getLength());
	}

	fp->close();

	system(CONVERT_EXEC1);
}
