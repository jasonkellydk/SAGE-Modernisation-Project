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

// FILE: GameLogic.cpp ////////////////////////////////////////////////////////////////////////////
// GameLogic class implementation
// Author: Michael S. Booth, October 2000
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"
import engine.profiling;
import engine.debug;	// This must go first in EVERY cpp file in the GameEngine
#include <cstdint>
import engine.navigation.scheduling.deadline_queue;
import engine.navigation.diagnostics.frame_capture;
import engine.platform;

#include "Common/AudioAffect.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/BuildAssistant.h"
#include "Common/CRCDebug.h"
#include "Common/FramePacer.h"
#include "Common/GameAudio.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GameState.h"
#include "Common/GameUtility.h"
#include "Common/INI.h"
#include "Common/LatchRestore.h"
#include "Common/MapObject.h"
#include "Common/MultiplayerSettings.h"
#include "Common/OSDisplay.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Radar.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "Common/StatsCollector.h"
#include "Common/ThingFactory.h"
#include "Common/Team.h"
#include "Common/ThingTemplate.h"
#include "GameClient/Water.h"
#include "GameClient/Snow.h"
#include "Common/WellKnownKeys.h"
#include "Common/Xfer.h"
#include "Common/XferCRC.h"
#include "Common/XferDeepCRC.h"
#include "Common/GameSpyMiscPreferences.h"

#include "GameClient/ControlBar.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "GameClient/GameText.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/InGameUI.h"
#include "GameClient/LoadScreen.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/TerrainVisual.h"
#include "GameClient/View.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/GameWindowTransitions.h"

#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/CaveSystem.h"
#include "GameLogic/CrateSystem.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Module/DestroyModule.h"
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/PolygonTrigger.h"
#include "GameLogic/ScriptActions.h"
#include "GameLogic/ScriptConditions.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/SidesList.h"
#include "GameLogic/VictoryConditions.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GhostObject.h"

#include "Common/DataChunk.h"
#include "GameLogic/Scripts.h"

#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"



struct QuitGameException {};

#include "Common/UnitTimings.h" //Contains the DO_UNIT_TIMINGS define jba.
// If defined, the game times various units.
#ifdef DO_UNIT_TIMINGS
#pragma MESSAGE("*** WARNING *** DOING DO_UNIT_TIMINGS!!!!")
Bool g_UT_gotUnit = false;
const ThingTemplate *g_UT_curThing = nullptr;
Bool g_UT_startTiming = false;
FILE *g_UT_timingLog=nullptr;
FILE *g_UT_commaLog=nullptr;
// Note - this is only for gathering timing data!  DO NOT DO THIS IN REGULAR CODE!!!  JBA
#define BRUTAL_TIMING_HACK
#include "../../GameEngineDevice/Include/W3DDevice/GameClient/Module/W3DModelDraw.h"
extern void externalAddTree(Coord3D location, Real scale, Real angle, AsciiString name);
#endif






// I'm making this larger now that we know how big our maps are going to be.
enum { OBJ_HASH_SIZE	= 8192 };

/// The GameLogic singleton instance
GameLogic *TheGameLogic = nullptr;

static void findAndSelectCommandCenter(Object *obj, void* alreadyFound);


// ------------------------------------------------------------------------------------------------
/** This enum is for loading screen bar progress */
// ------------------------------------------------------------------------------------------------
enum
{
	LOAD_PROGRESS_START =0,
	LOAD_PROGRESS_POST_PARTICLE_INI_LOAD = LOAD_PROGRESS_START + 1,
	LOAD_PROGRESS_POST_LOAD_MAP = LOAD_PROGRESS_POST_PARTICLE_INI_LOAD + 1,
	LOAD_PROGRESS_SIDE_POPULATION = LOAD_PROGRESS_POST_LOAD_MAP + 1,  // Increment the next one by at least MAX_SLOTS
	LOAD_PROGRESS_POST_SIDE_LIST_INIT = LOAD_PROGRESS_SIDE_POPULATION + 1 + MAX_SLOTS,
	LOAD_PROGRESS_POST_PLAYER_LIST_RESET = LOAD_PROGRESS_POST_SIDE_LIST_INIT + 1,
	LOAD_PROGRESS_POST_SCRIPT_ENGINE_NEW_MAP = LOAD_PROGRESS_POST_PLAYER_LIST_RESET + 1,
	LOAD_PROGRESS_POST_VICTORY_CONDITION_SETUP = LOAD_PROGRESS_POST_SCRIPT_ENGINE_NEW_MAP + 2,
	LOAD_PROGRESS_POST_VICTORY_CONDITION_SET_VICTORY_CONDITION = LOAD_PROGRESS_POST_VICTORY_CONDITION_SETUP + 1,
	LOAD_PROGRESS_POST_GHOST_OBJECT_MANAGER_RESET = LOAD_PROGRESS_POST_VICTORY_CONDITION_SET_VICTORY_CONDITION + 1,
	LOAD_PROGRESS_POST_TERRAIN_LOGIC_NEW_MAP = LOAD_PROGRESS_POST_GHOST_OBJECT_MANAGER_RESET + 1,
	LOAD_PROGRESS_POST_BRIDGE_LOAD = LOAD_PROGRESS_POST_TERRAIN_LOGIC_NEW_MAP + 1,
	LOAD_PROGRESS_POST_PATHFINDER_NEW_MAP = LOAD_PROGRESS_POST_BRIDGE_LOAD + 1,

	LOAD_PROGRESS_LOOP_ALL_THE_FREAKN_OBJECTS = LOAD_PROGRESS_POST_BRIDGE_LOAD + 1,
	LOAD_PROGRESS_MAX_ALL_THE_FREAKN_OBJECTS = 80,// THE END OF THE BIG ASS PROGRESS PAUSE

	LOAD_PROGRESS_LOOP_INITIAL_NETWORK_BUILDINGS = LOAD_PROGRESS_MAX_ALL_THE_FREAKN_OBJECTS + 1,  // Increment the next one by at least MAX_SLOTS
	LOAD_PROGRESS_POST_INITIAL_NETWORK_BUILDINGS = LOAD_PROGRESS_LOOP_INITIAL_NETWORK_BUILDINGS + MAX_SLOTS + 1,

	LOAD_PROGRESS_POST_PRELOAD_ASSETS = LOAD_PROGRESS_POST_INITIAL_NETWORK_BUILDINGS + 1,
	LOAD_PROGRESS_POST_STARTING_CAMERA = LOAD_PROGRESS_POST_PRELOAD_ASSETS + 1,
	LOAD_PROGRESS_POST_STARTING_CAMERA_2 = LOAD_PROGRESS_POST_STARTING_CAMERA + 1,
	LOAD_PROGRESS_END = 100,
};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static Waypoint * findNamedWaypoint(AsciiString name)
{
	for (Waypoint *way = TheTerrainLogic->getFirstWaypoint(); way; way = way->getNext())
	{
		if (way->getName() == name)
		{
			return way;
		}
	}
	return nullptr;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void setFPMode()
{
  // Set floating point round mode to CHOP, which only comes
  // into play when precision is exceeded.  This is necessary
  // for the fast float to int routines used elsewhere in the
  // system.
	//
	// Also set floating point precision to low.  It could be
	// anything as long as it is consistent, really, but this
	// is in the (vain?) hope of any slight speed boost.
	//
	_fpreset();

	UnsignedInt curVal = _statusfp();
	UnsignedInt newVal = curVal;
	newVal = (newVal & ~_MCW_RC) | (_RC_NEAR & _MCW_RC);
	//newVal = (newVal & ~_MCW_RC) | (_RC_CHOP & _MCW_RC);
	newVal = (newVal & ~_MCW_PC) | (_PC_24   & _MCW_PC);

	_controlfp(newVal, _MCW_PC | _MCW_RC);
}

//-------------------------------------------------------------------------------------------------
const char* toString(GameMode mode)
{
	switch (mode)
	{
		case GAME_SINGLE_PLAYER:
			return "GAME_SINGLE_PLAYER";
		case GAME_LAN:
			return "GAME_LAN";
		case GAME_SKIRMISH:
			return "GAME_SKIRMISH";
		case GAME_REPLAY:
			return "GAME_REPLAY";
		case GAME_SHELL:
			return "GAME_SHELL";
		case GAME_INTERNET:
			return "GAME_INTERNET";
		case GAME_NONE:
			return "GAME_NONE";
		default:
			return "GAME_UNKNOWN";
	}
}

// ------------------------------------------------------------------------------------------------
/** GameLogic class constructor */
// ------------------------------------------------------------------------------------------------
struct GameLogic::ScheduledUpdates { navigation::scheduling::DeadlineQueue<UpdateModulePtr> queue; };

GameLogic::GameLogic(engine::platform::IClockService& clock, engine::platform::IThreadingService& threading)
	: m_clock(clock), m_threading(threading), m_scheduledUpdates(std::make_unique<ScheduledUpdates>())
{
	m_background = nullptr;
	m_CRC = 0;
	m_isInUpdate = FALSE;

	m_rankPointsToAddAtGameStart = 0;

	for(Int i = 0; i < MAX_SLOTS; i++)
	{
		m_progressComplete[i] = FALSE;
		m_progressCompleteTimeout[i] = 0;
	}

	m_shouldValidateCRCs = FALSE;

	m_startNewGame = FALSE;

	m_frame = 0;
	m_hasUpdated = FALSE;
	m_frameObjectsChangedTriggerAreas = 0;
	m_width = 0;
	m_height = 0;
	m_objList = nullptr;
	m_curUpdateModule = nullptr;
	m_nextObjID = INVALID_ID;
	m_startNewGame = FALSE;
	m_gameMode = GAME_NONE;
	m_rankLevelLimit = 1000;
	m_pauseFrame = 0;
	m_gamePaused = FALSE;
	m_pauseSound = FALSE;
	m_pauseMusic = FALSE;
	m_pauseInput = FALSE;
	m_inputEnabledMemory = TRUE;
	m_mouseVisibleMemory = TRUE;
	m_logicTimeScaleEnabledMemory = FALSE;
	m_loadScreen = nullptr;
	m_forceGameStartByTimeOut = FALSE;

	m_loadingMap = FALSE;
	m_loadingSave = FALSE;
	m_clearingGameData = FALSE;
	m_quitToDesktopAfterMatch = FALSE;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool GameLogic::isInSinglePlayerGame()
{
	return (m_gameMode == GAME_SINGLE_PLAYER ||
		(TheRecorder && TheRecorder->isPlaybackMode() && TheRecorder->getGameMode() == GAME_SINGLE_PLAYER));
}


//-------------------------------------------------------------------------------------------------
/** Destroy all objects immediately */
//-------------------------------------------------------------------------------------------------
void GameLogic::destroyAllObjectsImmediate()
{
	Object *obj;
	Object *nextObj;

	// TheSuperHackers @bugfix xezon 22/05/2025 Set all remaining objects effectively dead to avoid triggering their
	// death modules that eventually would spawn new objects, such as debris, which could then crash the game.
	// See https://github.com/TheSuperHackers/GeneralsGameCode/issues/896
	for( obj = m_objList; obj; obj = obj->getNextObject() )
	{
		obj->setEffectivelyDead(true);
	}

	// destroy all remaining objects
	for( obj = m_objList; obj; obj = nextObj )
	{
		nextObj = obj->getNextObject();
		destroyObject( obj );
	}

	// process the destroy list immediately
	processDestroyList();
	engine::debug::invariant((m_objList == nullptr), "m_objList == nullptr", __FILE__, __LINE__, "destroyAllObjectsImmediate: Object list not cleared");

}

//-------------------------------------------------------------------------------------------------
/**GameLogic class destructor, the destruction order should mirror the
 * initialization order */
// ------------------------------------------------------------------------------------------------
GameLogic::~GameLogic()
{

	// clear any object TOC we might have
	m_objectTOC.clear();

	if (m_background)
	{
		m_background->destroyWindows();
		deleteInstance(m_background);
		m_background = nullptr;
	}

	// destroy all remaining objects
	destroyAllObjectsImmediate();

	// delete the logical terrain
	delete TheTerrainLogic;
	TheTerrainLogic = nullptr;

	delete TheGhostObjectManager;
	TheGhostObjectManager=nullptr;

	// delete the partition manager
	delete ThePartitionManager;
	ThePartitionManager = nullptr;

	delete TheScriptActions;
	TheScriptActions = nullptr;

	delete TheScriptConditions;
	TheScriptConditions = nullptr;

	// delete the Script Engine
	delete TheScriptEngine;
	TheScriptEngine = nullptr;

	// Null out TheGameLogic
	TheGameLogic = nullptr;
}

// ------------------------------------------------------------------------------------------------
/** (re)initialize the instance. */
// ------------------------------------------------------------------------------------------------
void GameLogic::init()
{

	setFPMode();

	// create the partition manager
	ThePartitionManager = NEW PartitionManager;
	ThePartitionManager->init();
	ThePartitionManager->setName("ThePartitionManager");


	// Create system for holding deleted objects that are
	// still in the partition manager because player has a fogged
	// view of them.
	TheGhostObjectManager = createGhostObjectManager(TheGlobalData->m_headless);

	// create the terrain logic
	TheTerrainLogic = createTerrainLogic();
	TheTerrainLogic->init();
	TheTerrainLogic->setName("TheTerrainLogic");

	// Create script engine system.
	TheScriptActions = NEW ScriptActions;		 // Basically, a subsystem of TheScriptEngine.
	TheScriptConditions = NEW ScriptConditions;	 // Basically, a subsystem of TheScriptEngine.
	TheScriptEngine = NEW ScriptEngine;
	TheScriptEngine->init();
	TheScriptEngine->setName("TheScriptEngine");

	// create a team for the player
	//engine::debug::invariant((ThePlayerList), "ThePlayerList", __FILE__, __LINE__, "null ThePlayerList");
	//ThePlayerList->setLocalPlayer(0);
	reset();
	m_isInUpdate = FALSE;
}

//-------------------------------------------------------------------------------------------------
/** Reset the game logic systems */
//-------------------------------------------------------------------------------------------------
void GameLogic::reset()
{
	m_thingTemplateBuildableOverrides.clear();
	m_controlBarOverrides.clear();

	// set the hash to be rather large. We need to optimize this value later.
//	m_objHash.clear();
//	m_objHash.resize(OBJ_HASH_SIZE);
	m_objVector.clear();
	m_objVector.resize(OBJ_HASH_SIZE, nullptr);

	m_pauseFrame = 0;
	m_pauseSound = FALSE;
	m_pauseMusic = FALSE;
	m_inputEnabledMemory = TRUE;
	m_mouseVisibleMemory = TRUE;
	m_logicTimeScaleEnabledMemory = FALSE;
	pauseGameLogic(FALSE);
	pauseGameInput(FALSE);

	setFPMode();

	// destroy all objects
	destroyAllObjectsImmediate();

	m_nextObjID = (ObjectID)1;

	m_frameObjectsChangedTriggerAreas = 0;

	TheGhostObjectManager->reset();
	ThePartitionManager->reset();
	TheTerrainLogic->reset();
	TheAI->reset();
	TheScriptEngine->reset();

	m_CRC = 0;
	for(Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_progressComplete[i] = FALSE;
		m_progressCompleteTimeout[i] = 0;
	}
	m_forceGameStartByTimeOut = FALSE;

	delete TheStatsCollector;
	TheStatsCollector = nullptr;

	// clear any table of contents we have
	m_objectTOC.clear();

	m_frame = 0;
	m_hasUpdated = FALSE;
	m_width = DEFAULT_WORLD_WIDTH;
	m_height = DEFAULT_WORLD_HEIGHT;
	m_objList = nullptr;
#ifdef ALLOW_NONSLEEPY_UPDATES
	m_normalUpdates.clear();
#endif
	m_scheduledUpdates->queue.forEach([](auto,UpdateModulePtr module) { module->friend_setIndexInLogic(-1); });
	m_scheduledUpdates->queue.clear();
	m_curUpdateModule = nullptr;

	m_isScoringEnabled = TRUE;
	m_showBehindBuildingMarkers = TRUE;
	m_drawIconUI = TRUE;
	m_showDynamicLOD = TRUE;
	m_scriptHulkMaxLifetimeOverride = -1;

	// Clean up any water transparency overrides that were generated for this map.
	WaterTransparencySetting *wt = (WaterTransparencySetting*) TheWaterTransparency.getNonOverloadedPointer();
	TheWaterTransparency = (WaterTransparencySetting*) wt->deleteOverrides();

	// Clean up any weather overrides that were generated for this map.
	WeatherSetting *ws = (WeatherSetting*) TheWeatherSetting.getNonOverloadedPointer();
	TheWeatherSetting = (WeatherSetting*) ws->deleteOverrides();

	m_rankPointsToAddAtGameStart = 0;
}

static Object * placeObjectAtPosition(Int slotNum, AsciiString objectTemplateName, Coord3D& pos, Player *pPlayer,
																	const PlayerTemplate *pTemplate)
{
	const ThingTemplate* btt = TheThingFactory->findTemplate(objectTemplateName);

	engine::debug::invariant((btt), "btt", __FILE__, __LINE__, "TheThingFactory didn't find a template in placeObjectAtPosition()");

	Object *obj = TheThingFactory->newObject( btt, pPlayer->getDefaultTeam() );
	engine::debug::invariant((obj), "obj", __FILE__, __LINE__, "TheThingFactory didn't give me a valid Object for player %d's (%ls) starting building",
		slotNum, pTemplate->getDisplayName().str());
	if (obj)
	{
		obj->setOrientation(obj->getTemplate()->getPlacementViewAngle());
		obj->setPosition( &pos );

		//engine::debug::log_info("Placed a starting building for %s at waypoint %s", playerName.str(), waypointName.str());
		engine::debug::log_trace("Placed an object for %ls at pos (%g,%g,%g)", pPlayer->getPlayerDisplayName().str(),
			pos.x, pos.y, pos.z);
		DUMPCOORD3D(&pos);

		Team *team = pPlayer->getDefaultTeam();
		// Now onCreates were called at the constructor.  This magically created
		// thing needs to be considered as Built for Game specific stuff.
		for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
		{
			CreateModuleInterface* create = (*m)->getCreate();
			if (!create)
				continue;
			create->onBuildComplete();
		}

		// Since the team now has members, activate it.
		// srj sez: team should not be null, but could be for ill-formed user maps. so don't crash.
		if (team)
			team->setActive();
		TheAI->pathfinder()->addObjectToPathfindMap(obj);
		if (obj->getAIUpdateInterface() && !obj->isKindOf(KINDOF_IMMOBILE)) {
			engine::debug::log_trace("Not immobile - adjusting dest");
			if (TheAI->pathfinder()->adjustDestination(obj, obj->getAIUpdateInterface()->getLocomotorSet(), &pos)) {
				DUMPCOORD3D(&pos);
				TheAI->pathfinder()->updateGoal(obj, &pos, LAYER_GROUND);	// Units always start on the ground for now.  jba.
				obj->setPosition( &pos );
			}
		}
	}

	return obj;
}

static void placeNetworkBuildingsForPlayer(Int slotNum, const GameSlot *pSlot, Player *pPlayer, const PlayerTemplate *pTemplate)
{
	Int startPos = pSlot->getStartPos();
	AsciiString waypointName;
	waypointName.format("Player_%d_Start", startPos+1); // start pos waypoints are 1-based

	AsciiString rallyWaypointName;
	rallyWaypointName.format("Player_%d_Rally", startPos+1); // start pos waypoints are 1-based

	Waypoint *waypoint = findNamedWaypoint(waypointName);
	Waypoint *rallyWaypoint = findNamedWaypoint(rallyWaypointName);
	engine::debug::invariant((waypoint), "waypoint", __FILE__, __LINE__, "Player %d has no starting waypoint (Player_%d_Start)", slotNum, startPos);
	if (!waypoint)
		return;

	Coord3D pos = *waypoint->getLocation();
	pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );

	AsciiString buildingTemplateName = pTemplate->getStartingBuilding();

	engine::debug::invariant((!buildingTemplateName.isEmpty()), "!buildingTemplateName.isEmpty()", __FILE__, __LINE__, "no starting building type for player %d (playertemplate %ls)",
		slotNum, pTemplate->getDisplayName().str());
	if (buildingTemplateName.isEmpty())
		return;

	engine::debug::log_info("Placing starting building at waypoint %s", waypointName.str());
	Object *conYard = placeObjectAtPosition(slotNum, buildingTemplateName, pos, pPlayer, pTemplate);

	if (!conYard)
		return;

	pPlayer->onStructureCreated(nullptr, conYard);
	pPlayer->onStructureConstructionComplete(nullptr, conYard, FALSE);

	//pos.x -= conYard->getGeometryInfo().getBoundingSphereRadius()/2;
	pos.y -= conYard->getGeometryInfo().getBoundingSphereRadius()/2;
	pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );

	if (rallyWaypoint)
	{
		pos = *rallyWaypoint->getLocation();
		pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
	}

	for (Int i=0; i<MAX_MP_STARTING_UNITS; ++i)
	{
		AsciiString objName = pTemplate->getStartingUnit(i);
		if (objName.isNotEmpty())
		{
			Coord3D objPos = pos;
			FindPositionOptions options;
			options.minRadius = conYard->getGeometryInfo().getBoundingSphereRadius() * 0.7f;
			options.maxRadius = conYard->getGeometryInfo().getBoundingSphereRadius() * 1.3f;
			engine::debug::log_info("Placing starting object %d (%s)", i, objName.str());
			ThePartitionManager->update();
			Bool foundPos = ThePartitionManager->findPositionAround(&pos, &options, &objPos);
			if (foundPos)
			{
				Object *unit = placeObjectAtPosition(slotNum, objName, objPos, pPlayer, pTemplate);
				if (unit) {
					pPlayer->onUnitCreated(nullptr, unit);
				}
			}
			else
			{
				engine::debug::log_info("Could not find position");
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
LoadScreen *GameLogic::getLoadScreen( Bool loadingSaveGame )
{
	switch (m_gameMode)
	{
	case GAME_SHELL:
		return NEW ShellGameLoadScreen;
		break;
	case GAME_SINGLE_PLAYER:
	{
		Campaign* currentCampaign = TheCampaignManager->getCurrentCampaign();
		if( currentCampaign && loadingSaveGame == FALSE )
		{
			if ( currentCampaign->m_isChallengeCampaign)
			{
				return NEW ChallengeLoadScreen;
			}
			return NEW SinglePlayerLoadScreen;
		}
		else
			return NEW ShellGameLoadScreen;
		break;
	}
	case GAME_SKIRMISH:
		return NEW MultiPlayerLoadScreen;
		break;
	case GAME_LAN:
		return NEW MultiPlayerLoadScreen;
		break;
	case GAME_REPLAY:
		return NEW ShellGameLoadScreen;
		break;
	case GAME_INTERNET:
		return NEW GameSpyLoadScreen;
		break;
	case GAME_NONE:
	default:
		return nullptr;
	}

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void handleNameChange( MapObject *mapObj )
{
	if( !mapObj->getName().compare( "AmericaTankLeopard" ) )
	{
		mapObj->setName( "AmericaTankCrusader" );
		const ThingTemplate *thingTemplate = TheThingFactory->findTemplate( mapObj->getName() );
		mapObj->setThingTemplate( thingTemplate );
	}
	if( !mapObj->getName().compare( "AmericaVehicleHumVee" ) )
	{
		mapObj->setName( "AmericaVehicleHumvee" );
		const ThingTemplate *thingTemplate = TheThingFactory->findTemplate( mapObj->getName() );
		mapObj->setThingTemplate( thingTemplate );
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static void checkForDuplicateColors( GameInfo *game )
{
	if(!game)
		return;
	Int i;

	// In QuickMatch, people can possibly set preferred color and house.
	// Here, we check for collisions in the color choice.  If there is a
	// collision, the first player will get the color, and the other(s)
	// will map to random.
	for (i=MAX_SLOTS-1; i>=0; --i)
	{
		GameSlot *slot = game->getSlot(i);

		if (!slot || !slot->isOccupied())
			continue;

		Int colorIdx = slot->getColor();
		if (colorIdx < 0 || colorIdx >= TheMultiplayerSettings->getNumColors())
			continue; // don't need to fix up random ones

		slot->setColor(-1);
		if (!game->isColorTaken(colorIdx))
		{
			// we were the only one using the color.  it is safe to keep using it.
			slot->setColor(colorIdx);
		}
		else if (colorIdx >= 0)
		{
			engine::debug::log_info("Clearing color %d for player %d", colorIdx, i);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static void populateRandomSideAndColor( GameInfo *game )
{
	if(!game)
		return;
	Int i;

#define MORE_RANDOM
#ifdef MORE_RANDOM
	std::vector<Int> startSlots;
	for (i = 0; i < ThePlayerTemplateStore->getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate* ptTest = ThePlayerTemplateStore->getNthPlayerTemplate(i);
		if (!ptTest || ptTest->getStartingBuilding().isEmpty())
			continue;

		if ( game->oldFactionsOnly() && !ptTest->isOldFaction() )
		  continue;

		// Prevent from selecting the disabled Generals.
		// This is also enforced at GUI setup (GUIUtil.cpp).
		// @todo: unlock these when something rad happens

		// TheSuperHackers @logic-client-separation helmutbuhler 11/04/2025
		// TheChallengeGenerals belongs to client, we shouldn't depend on that here.
		Bool disallowLockedGenerals = TRUE;
		const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName(ptTest->getName());
		Bool startsLocked = general ? !general->isStartingEnabled() : FALSE;
		if (disallowLockedGenerals && startsLocked)
			continue;

		startSlots.push_back(i);
	}
#endif

	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);

		if (!slot || !slot->isOccupied())
			continue;

		// clean up random factions
		Int playerTemplateIdx = slot->getPlayerTemplate();
		engine::debug::log_info("Player %d has playerTemplate index %d", i, playerTemplateIdx);
		while (playerTemplateIdx != PLAYERTEMPLATE_OBSERVER && (playerTemplateIdx < 0 || playerTemplateIdx >= ThePlayerTemplateStore->getPlayerTemplateCount()))
		{
			engine::debug::invariant((playerTemplateIdx == PLAYERTEMPLATE_RANDOM), "playerTemplateIdx == PLAYERTEMPLATE_RANDOM", __FILE__, __LINE__, "Non-random bad playerTemplate %d in slot %d", playerTemplateIdx, i);
#ifdef MORE_RANDOM
			// our RNG is basically shit -- horribly nonrandom at the start of the sequence.
			// get a few values at random to get rid of the dreck.
			// there's no mathematical basis for this, but empirically, it helps a lot.
			UnsignedInt silly = GetGameLogicRandomSeed() % 7;
			for (UnsignedInt poo = 0; poo < silly; ++poo)
			{
				GameLogicRandomValue(0, 1);	// ignore result
			}
			Int idxIdx = GameLogicRandomValue(0, 1000) % startSlots.size();
			playerTemplateIdx = startSlots[idxIdx];
#else
			playerTemplateIdx = GameLogicRandomValue(0, ThePlayerTemplateStore->getPlayerTemplateCount()-1);
#endif
			const PlayerTemplate* pt = ThePlayerTemplateStore->getNthPlayerTemplate(playerTemplateIdx);
			if (!pt || pt->getStartingBuilding().isEmpty())
			{
#ifdef MORE_RANDOM
				engine::debug::invariant(false, "debug failure", __FILE__, __LINE__, "should not be possible");
#endif
				playerTemplateIdx = -1; // only pick playable factions
			}
			else
			{
				engine::debug::log_info("Setting playerTemplateIdx %d to %d", i, playerTemplateIdx);
				slot->setPlayerTemplate(playerTemplateIdx);
			}
		}

		Int colorIdx = slot->getColor();
		if (colorIdx < 0 || colorIdx >= TheMultiplayerSettings->getNumColors())
		{
			engine::debug::invariant((colorIdx == -1), "colorIdx == -1", __FILE__, __LINE__, "Non-random bad color %d in slot %d", colorIdx, i);
			while (colorIdx == -1)
			{
				colorIdx = GameLogicRandomValue(0, TheMultiplayerSettings->getNumColors()-1);
				if (game->isColorTaken(colorIdx))
					colorIdx = -1;
			}
			engine::debug::log_info("Setting color %d to %d", i, colorIdx);
			slot->setColor(colorIdx);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
static const WaypointMap s_emptyWaypoints = WaypointMap();

static void populateRandomStartPosition( GameInfo *game )
{
	if(!game)
		return;

	Int i;
	Int numPlayers = MAX_SLOTS;
	const MapMetaData *md = TheMapCache->findMap( game->getMap() );
	if (md)
		numPlayers = md->m_numPlayers;
	else
		printf("Could not find map \"%s\"\n", game->getMap().str());
	engine::debug::invariant((md), "md", __FILE__, __LINE__, "Could not find map %s in the mapcache", game->getMap().str());

	// generate a map of start spot distances
	Real startSpotDistance[MAX_SLOTS][MAX_SLOTS];
	for (i=0; i<MAX_SLOTS; ++i)
	{
		for (Int j=0; j<MAX_SLOTS; ++j)
		{
			if (i != j && (i<numPlayers && j<numPlayers))
			{
				const WaypointMap& waypoints = md ? md->m_waypoints : s_emptyWaypoints;
				AsciiString w1, w2;
				w1.format("Player_%d_Start", i+1);
				w2.format("Player_%d_Start", j+1);
				WaypointMap::const_iterator c1 = waypoints.find(w1);
				WaypointMap::const_iterator c2 = waypoints.find(w2);
				if (c1 == waypoints.end() || c2 == waypoints.end())
				{
					// couldn't find a waypoint.  must be kinda far away.
					startSpotDistance[i][j] = 1000000.0f;
				}
				else
				{
					Coord3D p1 = c1->second;
					Coord3D p2 = c2->second;
					startSpotDistance[i][j] = sqrt( sqr(p1.x-p2.x) + sqr(p1.y-p2.y) );
				}
			}
			else
			{
				startSpotDistance[i][j] = 0.0f; // not gonna need this
			}
		}
	}

	// see if a start spot has been chosen at all yet
	Bool hasStartSpotBeenPicked = FALSE;
	Bool taken[MAX_SLOTS];
	for (i=0; i<MAX_SLOTS; ++i)
	{
		taken[i] = (i<numPlayers)?FALSE:TRUE;
	}
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);

		if (!slot || !slot->isOccupied() || slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;

		Int posIdx = slot->getStartPos();
		if (posIdx >= 0 || posIdx >= numPlayers)
		{
			hasStartSpotBeenPicked = TRUE;
			taken[posIdx] = TRUE;
		}
	}

#if 0  //GS  The old way puts everyone as far apart as possible.
	// now pick non-observer spots
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);

		if (!slot || !slot->isOccupied() || slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;

		// clean up random start spots
		Int posIdx = slot->getStartPos();
		if (posIdx < 0 || posIdx >= numPlayers)
		{
			engine::debug::invariant((posIdx == -1), "posIdx == -1", __FILE__, __LINE__, "Non-random bad start position %d in slot %d", posIdx, i);
			if (hasStartSpotBeenPicked)
			{
				// pick the farthest spot away
				Real farthestDistance = 0.0f;
				Int farthestIndex = -1;
				for (posIdx = 0; posIdx < numPlayers; ++posIdx)
				{
					if (!taken[posIdx])
					{
						if (farthestIndex < 0)
						{
							farthestIndex = posIdx; // take this one as best if none else
							for (Int n=0; n<numPlayers; ++n)
							{
								if (taken[n] && n != posIdx)
									farthestDistance += startSpotDistance[posIdx][n];
							}
						}
						else
						{
							Real dist = 0.0f;
							for (Int n=0; n<numPlayers; ++n)
							{
								if (taken[n] && n != posIdx)
									dist += startSpotDistance[posIdx][n];
							}
							if (dist > farthestDistance)
							{
								farthestDistance = dist;
								farthestIndex = posIdx;
							}
						}
					}
				}
				engine::debug::invariant((farthestIndex >= 0), "farthestIndex >= 0", __FILE__, __LINE__, "Couldn't find a farthest spot!");
				slot->setStartPos(farthestIndex);
				taken[farthestIndex] = TRUE;
			}
			else
			{
				// We're the first real spot.  Pick randomly.
				// This while loop shouldn't be necessary, since we're first.  Why not, though?
				while (posIdx == -1)
				{
					posIdx = GameLogicRandomValue(0, numPlayers-1);
					if (game->isStartPositionTaken(posIdx))
						posIdx = -1;
				}
				engine::debug::log_info("Setting start position %d to %d (random choice)", i, posIdx);
				slot->setStartPos(posIdx);
				taken[posIdx] = TRUE;
				hasStartSpotBeenPicked = TRUE;
			}
		}
	}
#else  //GS  The new way puts teammates next to each other.
	Int teamPosIdx[MAX_SLOTS];
	for (i=0; i<MAX_SLOTS; ++i)
		teamPosIdx[i] = -1;  //team has no starting position yet

	// now pick non-observer spots
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);

		if (!slot || !slot->isOccupied() || slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;  //slot not used

		Int posIdx = slot->getStartPos();
		if (posIdx >= 0  &&  posIdx < numPlayers)
			continue;  //position already assigned
		engine::debug::invariant((posIdx == -1), "posIdx == -1", __FILE__, __LINE__, "Non-random bad start position %d in slot %d", posIdx, i);

		//choose a starting position
		Int team = slot->getTeamNumber();
		if( !hasStartSpotBeenPicked )
		{	// We're the first real spot.  Pick randomly.
			while (posIdx == -1)
			{	// This while loop shouldn't be neccessary, since we're first.  Why not, though?
				posIdx = GameLogicRandomValue(0, numPlayers-1);
				if (game->isStartPositionTaken(posIdx))
					posIdx = -1;
			}
			engine::debug::log_info("Setting start position %d to %d (random choice)", i, posIdx);
			hasStartSpotBeenPicked = TRUE;
			slot->setStartPos(posIdx);
			taken[posIdx] = TRUE;
			if( team > -1 )
				teamPosIdx[team] = posIdx;  //remember where this team is
		} else
		{	//pick teams far apart, team members close together
			if( team < 0  ||  teamPosIdx[ team ] == -1 )  //if team None or team not yet placed
			{	//pick position furthest from all other teams
				Real farthestDistance = 0.0f;
				Int farthestIndex = -1;
				for (posIdx = 0; posIdx < numPlayers; ++posIdx)
				{
					if (taken[posIdx])
						continue;  //skip occupied positions

					if (farthestIndex < 0)
					{	//take this one as best if none else
						farthestIndex = posIdx;
						for (Int n=0; n<numPlayers; ++n)
						{
							if (taken[n] && n != posIdx)
								farthestDistance += startSpotDistance[posIdx][n];
						}
					}
					else
					{	//find empty position furthest from all taken positions
						Real dist = 0.0f;
						for (Int n=0; n<numPlayers; ++n)
						{
							if (taken[n] && n != posIdx)
								dist += startSpotDistance[posIdx][n];
						}
						if (dist > farthestDistance)
						{
							farthestDistance = dist;
							farthestIndex = posIdx;
						}
					}
				}

				engine::debug::invariant((farthestIndex >= 0), "farthestIndex >= 0", __FILE__, __LINE__, "Couldn't find a farthest spot!");
				slot->setStartPos(farthestIndex);
				taken[farthestIndex] = TRUE;
				if( team > -1 )
					teamPosIdx[team] = farthestIndex;  //remember where this team is
			}
			else  //team already has a starting position
			{	//pick position closest to team
				Real closestDist = FLT_MAX;
				Int  closestIdx = 0;
				for( Int n=0;  n < numPlayers;  ++n )
				{
					Real dist = startSpotDistance[ teamPosIdx[team] ][n];
					if( !taken[n]  &&  dist < closestDist )
					{	//found a better match
						closestDist = dist;
						closestIdx = n;
					}
				}
				engine::debug::invariant((closestDist < FLT_MAX), "closestDist < FLT_MAX", __FILE__, __LINE__, "Couldn't find a closest starting position!");
				slot->setStartPos(closestIdx);
				taken[closestIdx] = TRUE;
			}
		}
	}
#endif // 0

	// now go back & assign observer spots
	Int numPlayersInGame = 0;
	for (i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		if (slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
			++numPlayersInGame;
	}
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);

		if (!slot || !slot->isOccupied())
			continue;

		if (slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
			continue;

		// clean up random start spots
		Int posIdx = -1;
		if (numPlayersInGame == 0)
			posIdx = 0;
		while (posIdx == -1)
		{
			posIdx = GameLogicRandomValue(0, numPlayers-1);
			if (!game->isStartPositionTaken(posIdx))
				posIdx = -1;
		}
		engine::debug::log_info("Setting observer start position %d to %d", i, posIdx);
		slot->setStartPos(posIdx);
	}
}

// ------------------------------------------------------------------------------------------------
/** Update the load screen progress */
// ------------------------------------------------------------------------------------------------
void GameLogic::updateLoadProgress( Int progress )
{

	if( m_loadScreen )
		m_loadScreen->update( progress );

	if (TheGameEngine->getQuitting() || m_quitToDesktopAfterMatch)
	{
		throw QuitGameException();
	}
}

// ------------------------------------------------------------------------------------------------
/** Delete the load screen */
// ------------------------------------------------------------------------------------------------
void GameLogic::deleteLoadScreen()
{

	delete m_loadScreen;
	m_loadScreen = nullptr;

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::updateDisplayBusyState()
{
	const Bool busySystem = isInInteractiveGame() && !isGamePaused();
	const Bool busyDisplay = busySystem && !TheGlobalData->m_headless;

	OSDisplaySetBusyState(busyDisplay, busySystem);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::setGameMode( GameMode mode )
{
	GameMode prev = m_gameMode;
	m_gameMode = mode;

	TheMouse->onGameModeChanged(prev, mode);

	updateDisplayBusyState();
}

// ------------------------------------------------------------------------------------------------
/** Entry point for starting a new game, the engine is already in clean state at this
	* point and ready to load up with all the data */
// ------------------------------------------------------------------------------------------------
void GameLogic::startNewGame( Bool loadingSaveGame )
{
	try
	{
		tryStartNewGame(loadingSaveGame);
	}
	catch (QuitGameException&)
	{
		if (m_quitToDesktopAfterMatch && TheGameEngine)
		{
			TheGameEngine->setQuitting(TRUE);
		}
	}
}

void GameLogic::tryStartNewGame( Bool loadingSaveGame )
{


	// reset the frame counter
	m_frame = 0;
	m_hasUpdated = FALSE;

	setLoadingMap( TRUE );

	if( loadingSaveGame == FALSE )
	{

		// record pristine map name when we're loading from the map (not a save game)
		TheGameState->setPristineMapName( TheGlobalData->m_mapName );

		//
		// sanity, the pristine map should never start with "Save", otherwise that would be
		// a map from the save directory
		//
		if (TheGameState->isInSaveDirectory(TheGlobalData->m_mapName))
		{

			engine::debug::invariant(false, "debug failure", __FILE__, __LINE__,  "FATAL SAVE/LOAD ERROR! - Setting a pristine map name that refers to a map in the save directory.  The pristine map should always refer to the ORIGINAL map in the Maps directory, if the pristine map string is corrupt then map.ini files will not load correctly." );

		}

		if( m_startNewGame == FALSE )
		{
			/// @todo: Here is where we would look at the game mode & play an intro movie or something.
			// Failing that, we just set the flag so the actual game can start from a uniform
			// entry point (startNewGame() called from update()).
			if( m_gameMode == GAME_SINGLE_PLAYER )
			{

				if(m_background)
				{
					m_background->destroyWindows();
					deleteInstance(m_background);
					m_background = nullptr;
				}
				m_loadScreen = getLoadScreen( loadingSaveGame );
				if(m_loadScreen)
				{
					TheWritableGlobalData->m_loadScreenRender = TRUE;	///< mark it so only a few select things are rendered during load
					m_loadScreen->init(nullptr);
				}

			}

			m_startNewGame = TRUE;
			return;

		}

	}

	m_rankLevelLimit = 1000;	// this is reset every game.

	//
	// only reset the next object ID allocator counter when we're not loading a save game.
	// for save games, we read this value out of the save game file and it is important
	// that we preserve it as we load and execute the game
	//
	if( loadingSaveGame == FALSE )
		m_nextObjID = (ObjectID)1;

	TheWritableGlobalData->m_loadScreenRender = TRUE;	///< mark it so only a few select things are rendered during load
	TheWritableGlobalData->m_TiVOFastMode = FALSE;	//always disable the TIVO fast-forward mode at the start of a new game.

	Campaign* currentCampaign = TheCampaignManager->getCurrentCampaign();
	Bool isChallengeCampaign = m_gameMode == GAME_SINGLE_PLAYER && currentCampaign && currentCampaign->m_isChallengeCampaign;

	// Fill in the game color and Factions before we do the Load Screen
	TheGameInfo = nullptr;
	if (TheNetwork)
	{
		if (TheLAN)
		{
			engine::debug::log_info("Starting network game");
			TheGameInfo = TheLAN->GetMyGame();
		}
		else
		{
			engine::debug::log_info("Starting gamespy game");
			TheGameInfo = TheGameSpyGame;	/// @todo: MDC add back in after demo
		}
	}
	else
	{
		if (TheRecorder && TheRecorder->isPlaybackMode())
		{
			TheGameInfo = TheRecorder->getGameInfo();
		}
		else if(m_gameMode == GAME_SKIRMISH)
		{
		  TheGameInfo = TheSkirmishGameInfo;
		}
		else if(isChallengeCampaign)
		{
			TheGameInfo = TheChallengeGameInfo;
		}
	}

  // On a NEW game, we need to copy the superweapon restrictions from the game info to here
  // (because TheGameInfo is not always saved and doesn't carry over to replays). On a save
  // game, we save the superweapon restrictions in GameLogic::xfer()
  if ( !loadingSaveGame )
  {
    if ( TheGameInfo )
    {
      m_superweaponRestriction = TheGameInfo->getSuperweaponRestriction();
    }
    else
    {
      // ??? Apparently this is legit? Oh well, use defaults
      m_superweaponRestriction = 0;
    }
  }

	checkForDuplicateColors( TheGameInfo );

	Bool isSkirmishOrSkirmishReplay = FALSE;
	if (TheGameInfo)
	{
		for (Int i=0; i<MAX_SLOTS; ++i)
		{
			GameSlot *slot = TheGameInfo->getSlot(i);
			if (!loadingSaveGame) {
				if (slot->hasSavedOriginalSetup())
				{
					engine::debug::invariant((m_gameMode == GAME_SKIRMISH), "m_gameMode == GAME_SKIRMISH", __FILE__, __LINE__, "Expected GAME_SKIRMISH but got %s", toString(m_gameMode));

					// TheSuperHackers @fix Caball009 19/03/2026 Random color, position and faction are based on the logical seed. For improved determinism,
					// restarted games now set the original values so that the games start with the exact same logical seed values as the first time.
					slot->setColor(slot->getOriginalColor());
					slot->setStartPos(slot->getOriginalStartPos());
					slot->setPlayerTemplate(slot->getOriginalPlayerTemplate());
				}
				else
				{
					slot->saveOriginalSetup();
				}
			}
			if (slot->isAI())
			{
				isSkirmishOrSkirmishReplay = TRUE;
			}
		}
	} else {
		if (m_gameMode == GAME_SINGLE_PLAYER)	{
			delete TheSkirmishGameInfo;
			TheSkirmishGameInfo = nullptr;
		}
	}

	populateRandomSideAndColor( TheGameInfo );
	populateRandomStartPosition( TheGameInfo );

	//****************************//
	// Start the LoadScreen Now!	//
	//****************************//

	// Get the m_loadScreen for this kind of game
	if(!m_loadScreen && !(TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_SIMULATION_PLAYBACK))
	{
		m_loadScreen = getLoadScreen( loadingSaveGame );
		if(m_loadScreen)
		{
			TheMouse->setVisibility(FALSE);
			m_loadScreen->init(TheGameInfo);

			updateLoadProgress( LOAD_PROGRESS_START );
		}
	}
	if(m_background)
	{
		m_background->destroyWindows();
		deleteInstance(m_background);
		m_background = nullptr;
	}
	setFPMode();
	if(TheCampaignManager)
		TheCampaignManager->SetVictorious(FALSE);
	m_startNewGame = FALSE;

	// update the loadscreen
	if(m_loadScreen)
		updateLoadProgress(LOAD_PROGRESS_POST_PARTICLE_INI_LOAD);

	engine::debug::invariant((m_frame == 0), "m_frame == 0", __FILE__, __LINE__, "framecounter expected to be 0 here");

	// before loading the map, load the map.ini file in the same directory.
	loadMapINI( TheGlobalData->m_mapName );

	// load a map
	TheTerrainLogic->loadMap( TheGlobalData->m_mapName, false );
	// anytime the world's size changes, must reset the partition mgr
	//ThePartitionManager->init();

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_LOAD_MAP);


	Int localSlot = 0;
	Int progressCount = LOAD_PROGRESS_SIDE_POPULATION;
	if (TheGameInfo)
	{

		if (TheGameEngine->isMultiplayerSession() || isSkirmishOrSkirmishReplay)
		{
			// Saves off any player, and resets the sides to 0 players so we can add the skirmish players.
			TheSidesList->prepareForMP_or_Skirmish();
		}

		//engine::debug::log_info("Starting LAN game with %d players", game->getNumPlayers());
		for (int i=0; i<MAX_SLOTS; ++i)
		{
			// Add a Side to TheSidesList
			GameSlot *slot = TheGameInfo->getSlot(i);

			if (!slot || !slot->isHuman())
			{
				m_progressComplete[i] = TRUE;
				lastHeardFrom(i);
			}

			if (!slot || !slot->isOccupied())
				continue;

			Dict d;
			d.clear();
			AsciiString playerName;
			playerName.format("player%d", i);
			d.setAsciiString(TheKey_playerName, playerName);
			d.setBool(TheKey_playerIsHuman, slot->isHuman());
			d.setUnicodeString(TheKey_playerDisplayName, slot->getName());
			const PlayerTemplate* pt;
			if (slot->getPlayerTemplate() >= 0)
				pt = ThePlayerTemplateStore->getNthPlayerTemplate(slot->getPlayerTemplate());
			else
				pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );
			if (pt)
			{
				d.setAsciiString(TheKey_playerFaction, KEYNAME(pt->getNameKey()));
			}

			if (TheGameInfo->isPlayerPreorder(i))
			{
				d.setBool(TheKey_playerIsPreorder, TRUE);
			}

			AsciiString enemiesString, alliesString;
			Int team = slot->getTeamNumber();
			engine::debug::log_info("Looking for allies of player %d, team %d", i, team);
			for(int j=0; j < MAX_SLOTS; ++j)
			{
				GameSlot *teamSlot = TheGameInfo->getSlot(j);
				// for check to see if we're trying to add ourselves
				if(i == j || !teamSlot->isOccupied())
					continue;

				engine::debug::log_info("Player %d is team %d", j, teamSlot->getTeamNumber());

				AsciiString teamPlayerName;
				teamPlayerName.format("player%d", j);
				// if our team is None, or our team is not equal to their team,
				// then their our enemy
				Bool isEnemy = FALSE;
				if(team == -1 || teamSlot->getTeamNumber() != team ) isEnemy = TRUE;
				engine::debug::log_info("Player %d is %s", j, (isEnemy)?"enemy":"ally");

				if (isEnemy)
				{
					if(!enemiesString.isEmpty())
						enemiesString.concat(" ");
					enemiesString.concat(teamPlayerName);
				}
				else
				{
					// he's on our team, add him!
					if(!alliesString.isEmpty())
						alliesString.concat(" ");
					alliesString.concat(teamPlayerName);
				}
			}
			d.setAsciiString(TheKey_playerAllies, alliesString);
			d.setAsciiString(TheKey_playerEnemies, enemiesString);
			engine::debug::log_info("Player %d's teams are: allies=%s, enemies=%s", i,alliesString.str(),enemiesString.str());
/*

			Int colorIdx = slot->getColor();
			if (colorIdx < 0 || colorIdx >= TheMultiplayerSettings->getNumColors())
			{
				engine::debug::invariant((colorIdx == -1), "colorIdx == -1", __FILE__, __LINE__, "Non-random bad color %d in slot %d", colorIdx, i);
				while (colorIdx == -1)
				{
					colorIdx = GameLogicRandomValue(0, TheMultiplayerSettings->getNumColors()-1);
					if (game->isColorTaken(colorIdx))
						colorIdx = -1;
				}
				engine::debug::log_info("Setting color %d to %d", i, colorIdx);
				slot->setColor(colorIdx);
			}
			*/

			d.setInt(TheKey_playerColor, TheMultiplayerSettings->getColor(slot->getColor())->getColor());
			d.setInt(TheKey_playerNightColor, TheMultiplayerSettings->getColor(slot->getColor())->getNightColor());
			d.setInt(TheKey_multiplayerStartIndex, slot->getStartPos());
//			d.setBool(TheKey_multiplayerIsLocal, slot->isLocalPlayer());
//			d.setBool(TheKey_multiplayerIsLocal, slot->getIP() == game->getLocalIP());
			d.setBool(TheKey_multiplayerIsLocal, slot->isHuman() && (slot->getName().compare(TheGameInfo->getSlot(TheGameInfo->getLocalSlotNum())->getName().str()) == 0));

/*
			if (slot->getIP() == game->getLocalIP())
			{
				localSlot = i;
				engine::debug::log_info("GameLogic::StartNewGame - local slot is %d", localSlot);
			}
*/

			if (isSkirmishOrSkirmishReplay)
			{
				d.setBool(TheKey_playerIsSkirmish, true);
				switch (slot->getState()) {
					case SLOT_EASY_AI : d.setInt(TheKey_skirmishDifficulty, DIFFICULTY_EASY); break;
					case SLOT_MED_AI : d.setInt(TheKey_skirmishDifficulty, DIFFICULTY_NORMAL); break;
					case SLOT_BRUTAL_AI : d.setInt(TheKey_skirmishDifficulty, DIFFICULTY_HARD); break;
					default: break;	 // no setting.
				}
			}

			AsciiString slotNameAscii;
			slotNameAscii.translate(slot->getName());
			if (slot->isHuman() && TheGameInfo->getSlotNum(slotNameAscii) == TheGameInfo->getLocalSlotNum()) {
				localSlot = i;
			}
			TheSidesList->addSide(&d);

			AsciiString playerTeamName;
			playerTeamName.set("team");
			playerTeamName.concat(playerName);

			d.clear();
			d.setAsciiString(TheKey_teamName, playerTeamName);
			d.setAsciiString(TheKey_teamOwner, playerName);
			d.setBool(TheKey_teamIsSingleton, true);
			TheSidesList->addTeam(&d);

			engine::debug::log_info("Added side %d", i);
			updateLoadProgress(progressCount + i);
		}
	}
	//if(m_gameMode != GAME_REPLAY)
	//{

		// Always add in an observer Player
		Dict d;
		d.setAsciiString(TheKey_playerName, "ReplayObserver");
		d.setBool(TheKey_playerIsHuman, TRUE);
		d.setUnicodeString(TheKey_playerDisplayName, L"Observer");
		const PlayerTemplate* pt;
		pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );
		if (pt)
		{
			d.setAsciiString(TheKey_playerFaction, KEYNAME(pt->getNameKey()));
		}
		d.setAsciiString(TheKey_playerAllies, AsciiString::TheEmptyString);
		d.setAsciiString(TheKey_playerEnemies, AsciiString::TheEmptyString);
		d.setInt(TheKey_playerColor, TheMultiplayerSettings->getColor(0)->getColor());
		d.setInt(TheKey_playerNightColor, TheMultiplayerSettings->getColor(0)->getNightColor());
		d.setInt(TheKey_multiplayerStartIndex, 0);
		d.setBool(TheKey_multiplayerIsLocal, FALSE);

		TheSidesList->addSide(&d);
		d.clear();
		d.setAsciiString(TheKey_teamName, "teamReplayObserver");
		d.setAsciiString(TheKey_teamOwner, "ReplayObserver");
		d.setBool(TheKey_teamIsSingleton, true);
		TheSidesList->addTeam(&d);
	//}
	TheSidesList->validateSides();

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_SIDE_LIST_INIT);

	// update the player list to match the new map.
	TheTeamFactory->reset();
	ThePlayerList->newGame();

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_PLAYER_LIST_RESET);

	// Tell the script engine that a newe set of scripts is loaded.
	TheScriptEngine->newMap();

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_SCRIPT_ENGINE_NEW_MAP);

	if (TheGameEngine->isMultiplayerSession() || isSkirmishOrSkirmishReplay)
	{
		// if there are no other teams (happens for debugging) don't end the game immediately
		Int numTeams = 0; // this can be higher than expected, but is accurate for determining 0, 1, 2+
		Int lastTeam = -1;
		if (TheGameInfo)
		{
			for (int i=0; i<MAX_SLOTS; ++i)
			{
				const GameSlot *slot = TheGameInfo->getConstSlot(i);
				if (slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
				{
					if (slot->getTeamNumber() == -1 || slot->getTeamNumber() != lastTeam)
					{
						++numTeams;
						lastTeam = slot->getTeamNumber();
					}
				}
			}
		}

		if (numTeams > 1)
		{
			// add in the multiplayer victory/defeat scripts
			AsciiString path = "Data\\Scripts\\MultiplayerScripts.scb";
			CachedFileInputStream theInputStream;
			if (theInputStream.open(path))
			{
				ChunkInputStream *pStrm = &theInputStream;
				DataChunkInput file( pStrm );
				file.registerParser( "PlayerScriptsList", AsciiString::TheEmptyString, ScriptList::ParseScriptsDataChunk );
				if (!file.parse(nullptr)) {
					engine::debug::log_info("ERROR - Unable to read in multiplayer scripts.");
					return;
				}
				ScriptList *scripts[MAX_PLAYER_COUNT];
				Int count = ScriptList::getReadScripts(scripts);
				if (count)
				{
					ScriptList *pSL = TheSidesList->getSideInfo(0)->getScriptList();
					if (pSL != nullptr)
					{
						Script *next = scripts[0]->getScript();
						while (next)
						{
							Script *dupe = next->duplicate();
							pSL->addScript(dupe, 0);
							next = next->getNext();
						}
					}
				}
				for (Int i=0; i<count; ++i)
				{
					deleteInstance(scripts[i]);
				}
			}
		}

		/*
		// add multiplayer single-player defeat script
		if (numTeams > 1)
		{
			AsciiString name;
			name.format("MultiplayerPlayerDefeat"); // name is for the ui in the editor
			Script *pNewScript = newInstance(Script);
			pNewScript->setName(name);

			Condition *pFalse1 = newInstance(Condition)(Condition::MULTIPLAYER_PLAYER_DEFEAT);
			OrCondition *pOr = newInstance(OrCondition);
			pOr->setFirstAndCondition(pFalse1);
			pNewScript->setOrCondition(pOr);

			ScriptAction *action = newInstance(ScriptAction)(ScriptAction::LOCALDEFEAT);
			pNewScript->setAction(action);

			ScriptList *pSL = TheSidesList->getSideInfo(0)->getScriptList();
			pSL->addScript(pNewScript, 0); // adds this as the player's first script (index 0)
		}

		// add multiplayer victory scripts
		if (numTeams > 1)
		{
			AsciiString name;
			name.format("MultiplayerVictory"); // name is for the ui in the editor
			Script *pNewScript = newInstance( Script );
			pNewScript->setName(name);

			Condition *pFalse1 = newInstance(Condition)(Condition::MULTIPLAYER_ALLIED_VICTORY);
			OrCondition *pOr = newInstance(OrCondition);
			pOr->setFirstAndCondition(pFalse1);
			pNewScript->setOrCondition(pOr);

			ScriptAction *action = newInstance(ScriptAction)(ScriptAction::VICTORY);
			pNewScript->setAction(action);

			ScriptList *pSL = TheSidesList->getSideInfo(0)->getScriptList();
			pSL->addScript(pNewScript, 0); // adds this as the player's first script (index 0)
		}

		// add multiplayer defeat scripts
		if (numTeams > 1)
		{
			AsciiString name;
			name.format("MultiplayerDefeat"); // name is for the ui in the editor
			Script *pNewScript = newInstance(Script);
			pNewScript->setName(name);

			Condition *pFalse1 = newInstance(Condition)(Condition::MULTIPLAYER_ALLIED_DEFEAT);
			OrCondition *pOr = newInstance(OrCondition);
			pOr->setFirstAndCondition(pFalse1);
			pNewScript->setOrCondition(pOr);

			ScriptAction *action = newInstance(ScriptAction)(ScriptAction::DEFEAT);
			pNewScript->setAction(action);

			ScriptList *pSL = TheSidesList->getSideInfo(0)->getScriptList();
			pSL->addScript(pNewScript, 0); // adds this as the player's first script (index 0)
		}
		*/
	}

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_VICTORY_CONDITION_SETUP);

	Player *localPlayer = ThePlayerList->getLocalPlayer();

	// set the radar as on a new map
	TheRadar->newMap( TheTerrainLogic );

	// TheSuperHackers @tweak Force on radar for all observers.
	for (Int i = 0; i < MAX_PLAYER_COUNT; ++i)
	{
		Player *player = ThePlayerList->getNthPlayer(i);
		if (player->isPlayerObserver())
		{
			TheRadar->forceOn(i, TRUE);
		}
	}

	TheInGameUI->setClientQuiet( FALSE ); // okay to start beeping and stuff

	// Tell the multiplayer victory condition singleton that the players are created
	TheVictoryConditions->cachePlayerPtrs();
	TheVictoryConditions->setVictoryConditions(VICTORY_NOBUILDINGS);

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_VICTORY_CONDITION_SET_VICTORY_CONDITION);

	// set the world extents to that of the map
	Region3D extent;
	TheTerrainLogic->getExtent( &extent );

	setWidth( extent.hi.x - extent.lo.x );
	setHeight( extent.hi.y - extent.lo.y );

	// anytime the world's size changes, must reset the partition mgr
	ThePartitionManager->init();
	ThePartitionManager->refreshShroudForLocalPlayer();// Can't do this until after init, and doesn't seem right to do in init

	TheGhostObjectManager->setLocalPlayerIndex(localPlayer->getPlayerIndex());
	TheGhostObjectManager->reset();

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_GHOST_OBJECT_MANAGER_RESET);

	// update the terrain logic now that all is loaded
	TheTerrainLogic->newMap( loadingSaveGame );

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_TERRAIN_LOGIC_NEW_MAP);


		// Special case, load any bridge map objects.
	for (MapObject *pMapObj = MapObject::getFirstMapObject(); pMapObj; pMapObj = pMapObj->getNext())
	{

		if (pMapObj->getFlag(FLAG_BRIDGE_FLAGS) || pMapObj->getFlag(FLAG_ROAD_FLAGS))
			continue;	// these roads & bridges are special cased in the terrain side.

		// get thing template based from map object name
		const ThingTemplate *thingTemplate = pMapObj->getThingTemplate();
		if( thingTemplate == nullptr )
			continue;

		if (!thingTemplate->isBridgeLike())
			continue;

		Team *team = ThePlayerList->getNeutralPlayer()->getDefaultTeam();
		// create new object in the world
		Object *obj = TheThingFactory->newObject( thingTemplate, team ); //, OBJECT_STATUS_LOADING_FROM_MAP );
		if( obj )
		{
			Coord3D pos = *pMapObj->getLocation();
			pos.z += TheTerrainLogic->getGroundHeight( pos.x, pos.y );

			Real angle = normalizeAngle(pMapObj->getAngle());
			obj->setOrientation(angle);
			obj->setPosition( &pos );
			if (thingTemplate->isBridge()) {
				TheTerrainLogic->addLandmarkBridgeToLogic(obj);
			}
			if (thingTemplate->isKindOf(KINDOF_WALK_ON_TOP_OF_WALL)) {
				TheAI->pathfinder()->addWallPiece(obj);
			}

			// Do this after positioning the object, because we may place objects in response to keys
			// in the map object properties.
			// update this object instance with properties from the map object
			obj->updateObjValuesFromMapProperties( pMapObj->getProperties() );
		}

	}

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_BRIDGE_LOAD);

	// refresh the radar to reflect loaded bridges
	TheRadar->refreshTerrain( TheTerrainLogic );

	// tell the AI about it
	// Note that it is important that the pathfinder be called before the map objects are loaded.
	TheAI->pathfinder()->newMap();

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_PATHFINDER_NEW_MAP);

	// reveal the map for the permanent observer
	Player *observerPlayer = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey("ReplayObserver"));
	ThePartitionManager->revealMapForPlayerPermanently( observerPlayer->getPlayerIndex() );
	engine::debug::log_info("Reveal shroud for %ls whose index is %d", observerPlayer->getPlayerDisplayName().str(), observerPlayer->getPlayerIndex());

	if (TheGameInfo)
	{
		for (int i=0; i<MAX_SLOTS; ++i)
		{
			GameSlot *slot = TheGameInfo->getSlot(i);

			if (!slot || !slot->isOccupied())
				continue;

			Player *player = ThePlayerList->getPlayerFromSlotIndex(i);

			if (slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			{
				engine::debug::log_info("Clearing shroud for observer in slot %d with player index %d", i, player->getPlayerIndex());
				ThePartitionManager->revealMapForPlayerPermanently( player->getPlayerIndex() );
			}
			else
			{
				// remove shroud for the player in MP games
				if (!TheMultiplayerSettings->isShroudInMultiplayer())
					ThePartitionManager->revealMapForPlayer( player->getPlayerIndex() );
			}
		}
	}


	Bool useTrees = TheGlobalData->m_useTrees;

	// If forceFluffToProp == true, removable objects get created on client only. [7/14/2003]
	// If static lod is HIGH, we don't do force fluff to client side only (create logic side props, more expensive. jba)
	Bool forceFluffToProp = TheGameLODManager->getStaticLODLevel() < STATIC_GAME_LOD_HIGH;
	if (TheGameLODManager->getStaticLODLevel() == STATIC_GAME_LOD_CUSTOM &&
			TheGlobalData->m_useShadowVolumes) {
		// Custom LOD, and volumetric shadows turned on - very high detail.  So use logic props too. jba. [7/14/2003]
		forceFluffToProp = false;
	}

	if (TheRecorder && TheRecorder->isMultiplayer()) {
		useTrees = TRUE; // Always use trees in multiplayer, cause we want it to sync properly. jba.
		forceFluffToProp = TRUE; // Always do client side fluff - faster, and syncs properly. jba.
	}

	if( loadingSaveGame ) {
		// Loading a loadingSaveGame, need to add the trees to the client. jba. [8/11/2003]
		for (MapObject *pMapObj = MapObject::getFirstMapObject(); pMapObj; pMapObj = pMapObj->getNext())
		{
			// get thing template based from map object name
			const ThingTemplate *thingTemplate = pMapObj->getThingTemplate();
			if( thingTemplate == nullptr )
				continue;
			// don't create trees and shrubs if this is one and we have that option off
			if( thingTemplate->isKindOf( KINDOF_SHRUBBERY ) && !useTrees )
				continue;

			Coord3D pos = *pMapObj->getLocation();
			pos.z += TheTerrainLogic->getGroundHeight( pos.x, pos.y );
			Real angle = normalizeAngle(pMapObj->getAngle());

			if (thingTemplate->isKindOf(KINDOF_OPTIMIZED_TREE)) {
				createOptimizedTree(thingTemplate, &pos, angle);
			}
		}
	}
	else
	{
		Int progressCount = LOAD_PROGRESS_LOOP_ALL_THE_FREAKN_OBJECTS;
		Int timer = static_cast<UnsignedInt>(m_clock.monotonic_nanoseconds() / 1000000u);
		for (MapObject *pMapObj = MapObject::getFirstMapObject(); pMapObj; pMapObj = pMapObj->getNext())
		{

			if (pMapObj->getFlag(FLAG_BRIDGE_FLAGS) || pMapObj->getFlag(FLAG_ROAD_FLAGS)) {
				continue;	// roads & bridges are special cased in the terrain side.
			}
			//Kris: Added this function so that we can rename objects and preserve them. If the patch
			//      entry is missing for a particular unit, it doesn't get added to the world.
			handleNameChange( pMapObj );

			// get thing template based from map object name
			const ThingTemplate *thingTemplate = pMapObj->getThingTemplate();

			//
			// if no template continue, some map objects don't have thing templates like
			// lights (handled in the device).  Objects that are test objects have a
			// test string (designated with *** and the define TEST_STRING) and will
			// have temporary templates created 'on the fly' during a
			// ThingFactory->findTemplate() call when loading from the map file
			//
			if( thingTemplate == nullptr )
				continue;

			if (thingTemplate->isBridgeLike())
				continue; // bridges have to be added earlier.

			// don't create trees and shrubs if this is one and we have that option off
			if( thingTemplate->isKindOf( KINDOF_SHRUBBERY ) && !useTrees )
				continue;

			Coord3D pos = *pMapObj->getLocation();
			pos.z += TheTerrainLogic->getGroundHeight( pos.x, pos.y );
			Real angle = normalizeAngle(pMapObj->getAngle());

			if (thingTemplate->isKindOf(KINDOF_OPTIMIZED_TREE)) {
				createOptimizedTree(thingTemplate, &pos, angle);
				continue;
			}

#if 1
			Bool isProp = thingTemplate->isKindOf(KINDOF_PROP);
			Bool isFluff = false;
			if (thingTemplate->isKindOf(KINDOF_CLEARED_BY_BUILD)) {
				if (thingTemplate->getFenceWidth() == 0.0f) {
					// Objects that are cleared by building, but aren't fences, are fluff. jba. [7/14/2003]
					isFluff = true;
				}
			}
			if (isProp || (isFluff && forceFluffToProp)) {
				TheTerrainVisual->addProp(thingTemplate, &pos, angle);
				continue;
			}
#endif

			// Get the team information
			engine::debug::invariant((pMapObj->getProperties()->getType(TheKey_originalOwner) == Dict::DICT_ASCIISTRING), "pMapObj->getProperties()->getType(TheKey_originalOwner) == Dict::DICT_ASCIISTRING", __FILE__, __LINE__, "unit %s has no original owner specified (obsolete map file)",pMapObj->getName().str());
			AsciiString originalOwner = pMapObj->getProperties()->getAsciiString(TheKey_originalOwner);
			Team *team = ThePlayerList->validateTeam(originalOwner);

			// create new object in the world
			Object *obj = TheThingFactory->newObject( thingTemplate, team ); //, OBJECT_STATUS_LOADING_FROM_MAP );
			if( obj )
			{
				if(pMapObj->getFlag(FLAG_DRAWS_IN_MIRROR) || obj->isKindOf(KINDOF_CAN_CAST_REFLECTIONS))
				{
					Drawable* draw = obj->getDrawable();
					if(draw)
						draw->setDrawableStatus( DRAWABLE_STATUS_DRAWS_IN_MIRROR );
				}
				obj->setOrientation(angle);
				obj->setPosition( &pos );

				// Do this after positioning the object, because we may place objects in response to keys
				// in the map object properties.
				// update this object instance with properties from the map object
				obj->updateObjValuesFromMapProperties( pMapObj->getProperties() );

				PathfindLayerEnum	layer = TheTerrainLogic->getLayerForDestination(&pos);
				obj->setLayer(layer);
				// Now onCreates were called at the constructor.  This magically created
				// thing needs to be considered as Built for Game specific stuff.
				for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
				{
					CreateModuleInterface* create = (*m)->getCreate();
					if (!create)
						continue;
					create->onBuildComplete();
				}

				// Since the team now has members, activate it.
				team->setActive();
				TheAI->pathfinder()->addObjectToPathfindMap( obj );

			}

			if(static_cast<UnsignedInt>(m_clock.monotonic_nanoseconds() / 1000000u) > timer + 500)
			{
				if(progressCount < LOAD_PROGRESS_MAX_ALL_THE_FREAKN_OBJECTS)
					progressCount ++;
				updateLoadProgress(progressCount);
				timer = static_cast<UnsignedInt>(m_clock.monotonic_nanoseconds() / 1000000u);
			}

		}

	}


	// place initial network buildings/units
	if (TheGameInfo && !loadingSaveGame)
	{
		Int progressCount = LOAD_PROGRESS_LOOP_INITIAL_NETWORK_BUILDINGS;
		for (int i=0; i<MAX_SLOTS; ++i)
		{
			GameSlot *slot = TheGameInfo->getSlot(i);

			if (!slot || !slot->isOccupied())
				continue;

			Player *player = ThePlayerList->getPlayerFromSlotIndex(i);

			if (slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			{
				slot->setPlayerTemplate(0);
				const PlayerTemplate *pt = ThePlayerTemplateStore->findPlayerTemplate( TheNameKeyGenerator->nameToKey("FactionObserver") );
				if (pt)
				{
					for (Int j=0; j<ThePlayerTemplateStore->getPlayerTemplateCount(); ++j)
					{
						if (pt == ThePlayerTemplateStore->getNthPlayerTemplate(j))
						{
							slot->setPlayerTemplate(j);
							break;
						}
					}
				}
				engine::debug::log_info("Setting observer's playerTemplate to %d in slot %d", slot->getPlayerTemplate(), i);
			}
			else
			{
				const PlayerTemplate* pt = nullptr;
				pt = ThePlayerTemplateStore->getNthPlayerTemplate(slot->getPlayerTemplate());

				// Prevent from loading the disabled Generals, in case your game peer hacked their GUI.
				// The game will start, but the cheater will be instantly defeated because he has no troops.
				// This is also enforced at GUI setup (GUIUtil.cpp and UserPreferences.cpp).
				// @todo: unlock these when something rad happens

				// TheSuperHackers @logic-client-separation helmutbuhler 11/04/2025
				// TheChallengeGenerals belongs to client, we shouldn't depend on that here.
				Bool disallowLockedGenerals = TRUE;
				const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName(pt->getName());
				Bool startsLocked = general ? !general->isStartingEnabled() : FALSE;
				if (disallowLockedGenerals && startsLocked)
					continue;

				// prevent from loading disallowed templates, in case your peer hacked their GUI.


        // So that the global flag for restricting factions to "OLD" is applied only in the appropriate context!
        // Trouble was that skirmish games would get no command centers upon start, if this was set true in a GameSpyMenu
        if ( isInInternetGame() )
        {
				  if ( TheGameInfo->oldFactionsOnly() && !pt->isOldFaction() )
				    continue;
        }




				placeNetworkBuildingsForPlayer(i, slot, player, pt);
			}

			updateLoadProgress(progressCount++);

		}
	}
	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_INITIAL_NETWORK_BUILDINGS);

	//
	// tell the client to pre-load some assets that we will use such as faction things we
	// will build and various damage states for all the structures on the map so that we
	// don't have big pauses when building those objects or switching to those states
	//
	if( TheGlobalData->m_preloadAssets )
	{
		if (TheGlobalData->m_preloadEverything)
		{
			for (Int td = TIME_OF_DAY_FIRST; td < TIME_OF_DAY_COUNT; ++td)
				TheGameClient->preloadAssets((TimeOfDay)td);
		}
		else
		{
			TheGameClient->preloadAssets( TheGlobalData->m_timeOfDay );
		}
	}

	//put this here somewhat randomly.
	TheControlBar->hideCommunicator( FALSE );

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_PRELOAD_ASSETS);

	TheTacticalView->setAngleToDefault();
	TheTacticalView->setPitchToDefault();
	TheTacticalView->setZoomToDefault();

	if( TheRecorder )
		TheRecorder->initControls();

	// Note - WorldBuilderDoc.cpp also uses initial camera position, so if changed, update both.  jba
	// Note - We construct the multiplayer start spot name manually here, so change this if you
	//        change TheKey_Player_1_Start etc.  mdc
	AsciiString startingCamName = TheNameKeyGenerator->keyToName(TheKey_InitialCameraPosition);
	if (TheGameInfo)
	{
		GameSlot *slot = TheGameInfo->getSlot(localSlot);
		engine::debug::invariant((slot), "slot", __FILE__, __LINE__, "Starting a LAN game without ourselves!");

		if (slot->isHuman())
		{
			Int startPos = slot->getStartPos();
			startingCamName.format("Player_%d_Start", startPos+1); // start pos waypoints are 1-based
			engine::debug::log_info("Using %s as the multiplayer initial camera position", startingCamName.str());
		}
	}

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_STARTING_CAMERA);

	Waypoint *way = findNamedWaypoint(startingCamName);
	if (way)
	{
		Coord3D pos = *way->getLocation();
		TheTacticalView->lookAt( &pos );
	}
	else
	{
		// Just look somewhere.  lookAt does some terrain specific setup, so it is good
		// to call it.  jba
		Coord3D pos;
		pos.x = 50;
		pos.y = 50;
		pos.z = 0;
		TheTacticalView->lookAt( &pos );
		engine::debug::log_info("Failed to find initial camera position waypoint %s", startingCamName.str());
	}

	// Set up the camera height based on the map height & globalData.
	TheTacticalView->initHeightForMap();
	TheTacticalView->setAngleToDefault();
	TheTacticalView->setPitchToDefault();
	TheTacticalView->setZoomToDefault();

	// update the loadscreen
	updateLoadProgress(LOAD_PROGRESS_POST_STARTING_CAMERA_2);

	// update partition info - We need to do the initial update so that it can be queried
	// during the first frame.  jba.
	ThePartitionManager->update();



	// final step, run newMap for all players
	if( loadingSaveGame == FALSE )
		ThePlayerList->newMap();

	if ( isChallengeCampaign )
	{
		// Establish local player relationships with other teams as
		// they're set up for "ThePlayer" in challenge mode map.
		// Some designers have used "ThePlayer" as an empty player for Generals'
		// Challenge maps which they reference in script and by which they set
		// player relationships.  The skirmish local player will receive these
		// relationships.  If there is no "ThePlayer", we have to assume that
		// all players on the map are the local player's enemy, except neutral
		// and civilian players.
		engine::debug::invariant((localPlayer), "localPlayer", __FILE__, __LINE__, "Local player has not been established for Challenge map.");
		Player *placeholderThePlayer = ThePlayerList->findPlayerWithNameKey(NAMEKEY("ThePlayer"));
		engine::debug::invariant((placeholderThePlayer), "placeholderThePlayer", __FILE__, __LINE__, "Challenge maps without player \"ThePlayer\" assume that the local player is mutual enemies with all other players except the neutral and civilian players.");

		if (placeholderThePlayer)
		{
			PlayerMaskType thePlayerEnemysMask = ThePlayerList->getPlayersWithRelationship(placeholderThePlayer->getPlayerIndex(), ALLOW_ENEMIES);
			while( thePlayerEnemysMask )
			{
				// set local player enemy relationships based on "ThePlayer" enemy relationships
				Player *thePlayerEnemy = ThePlayerList->getEachPlayerFromMask(thePlayerEnemysMask);
				thePlayerEnemy->setPlayerRelationship(localPlayer, ENEMIES);
				localPlayer->setPlayerRelationship(thePlayerEnemy, ENEMIES);
			}
		}
		else
		{
			// This map has no "ThePlayer" by which to model the local player's
			// relationships, so make assumptions.
			for (Int i = 0; i < ThePlayerList->getPlayerCount(); i++)
			{
				Relationship rel = NEUTRAL;
				Player *thatPlayer = ThePlayerList->getNthPlayer(i);
				if (thatPlayer == localPlayer)
				{
					rel = ALLIES;
				}
				else if (thatPlayer != ThePlayerList->getNeutralPlayer()
					&& thatPlayer != ThePlayerList->findPlayerWithNameKey(NAMEKEY("PlyrCivilian")))
				{
					rel = ENEMIES;
				}
				thatPlayer->setPlayerRelationship(localPlayer, rel);
				localPlayer->setPlayerRelationship(thatPlayer, rel);
			}
		}
	}


	// reset all the skill points in a single player game
	if(loadingSaveGame == FALSE && isInSinglePlayerGame())
	{
		for (Int i=0; i<MAX_PLAYER_COUNT; ++i)
		{
			Player *pPlayer = ThePlayerList->getNthPlayer(i);
			if (pPlayer && pPlayer->getPlayerType() != PLAYER_HUMAN)
				pPlayer = nullptr;

			if (pPlayer)
			{
				pPlayer->addSkillPoints(m_rankPointsToAddAtGameStart);
				engine::debug::log_info("GameLogic::startNewGame() - adding m_rankPointsToAddAtGameStart==%d to player %d(%ls)",
					m_rankPointsToAddAtGameStart, i, pPlayer->getPlayerDisplayName().str());
			}
		}
	}

	updateLoadProgress(LOAD_PROGRESS_END);

	if(isInMultiplayerGame() && TheNetwork)
	{
		TheNetwork->loadProgressComplete();
		TheNetwork->liteupdate();
	}

	while(!isProgressComplete())
	{
		updateLoadProgress(101); // keep greater then 100
		testTimeOut();
	m_threading.sleep_for_microseconds(100000);
	}

	// if we're in a load game, don't fade yet
	if(loadingSaveGame == FALSE && TheTransitionHandler != nullptr && m_loadScreen)
	{
		TheFramePacer->reset();
		TheTransitionHandler->setGroup("FadeWholeScreen");
		while(!TheTransitionHandler->isFinished())
		{
			TheWindowManager->update();
			if(!TheTransitionHandler->isFinished())
			{
				TheDisplay->draw();
				setFPMode();
				TheFramePacer->update();
			}

		}
	}

	if(m_loadScreen)
	{
		TheMouse->setVisibility(TRUE);

/*
		//
		// delete load screen only when not loading a save game, for save games we still
		// have more work to do and the load screen will be deleted elsewhere after
		// we're all done with the load game progress
		//
		if( loadingSaveGame == FALSE )
*/
			deleteLoadScreen();

	}


	if(m_gameMode == GAME_SHELL)
	{
		if (!TheGlobalData->m_headless)
		{
			if(TheShell->getScreenCount() == 0)
				TheShell->push( "Menus/MainMenu.wnd" );
			else if (TheShell->top())
			{
				TheShell->top()->hide(FALSE);
				TheShell->top()->bringForward();
			}
			HideControlBar();
		}
	}
	else
	{

//		TheShell->hideShell();
		if(TheStatsCollector)
		{
			TheStatsCollector->reset();
		}
		else if( TheGlobalData->m_playStats > 0)
		{
			TheStatsCollector = NEW StatsCollector;
			TheStatsCollector->reset();
		}

///		ShowControlBar(FALSE);

		// explicitly set the Control bar to Observer Mode
		if(m_gameMode == GAME_REPLAY )
		{
			rts::changeLocalPlayer(observerPlayer);

			engine::debug::log_info("Start of a replay game %ls, %d", localPlayer->getPlayerDisplayName().str(), localPlayer->getPlayerIndex());
		}
		else
		{
			TheControlBar->setControlBarSchemeByPlayer(localPlayer);
			TheControlBar->initSpecialPowershortcutBar(localPlayer);
		}
//		ShowControlBar();

	}
	TheTacticalView->setOkToAdjustHeight(TRUE);

// If defined, the game times various units.
#ifdef DO_UNIT_TIMINGS
	g_UT_curThing = TheThingFactory->firstTemplate();
	g_UT_startTiming = true;
	g_UT_gotUnit = false;
	g_UT_timingLog = fopen("TimingLog.txt", "w");
	g_UT_commaLog = fopen("TimingCDL.txt", "w");
	fputs("Full,100*ms,NoPart-NoSpawn,,No Spawn,100*ms,Logic,100*ms,Thing,Model,Kind,Side,DrawCalls All,DrawCalls NoPart-NoSpawn,DrawCalls NoSpawn\n", g_UT_commaLog);

	// Turn off shadows
	TheWritableGlobalData->m_useShadowVolumes = false;
#ifdef DEBUG_CRASHING
	TheWritableGlobalData->m_debugIgnoreAsserts = TRUE;
#endif

	// Just look somewhere.  lookAt does some terrain specific setup, so it is good
	// to call it.  jba
	Coord3D thePos;
	thePos.x = 50;
	thePos.y = 50;
	thePos.z = 0;
		TheTacticalView->lookAt( &thePos );
#endif


	// @todo remove this hack
//	TheGlobalData->m_inGame = TRUE;

	// If we are now starting a multiplayer or skirmish game, let us set the local players selectionto be the command center
	// We'll ask the Recorder, so we survive replays
	if( TheRecorder->isMultiplayer() )
	{
		// Iterate through each player's objects, and ask if the object
		//is a command center, and if so, select it for that player
		for (Int i = 0; i < MAX_PLAYER_COUNT; ++i)
		{
			Player *player = ThePlayerList->getNthPlayer(i);
			if (player && player->isPlayerActive())
			{
				// we need to iterate their objects, and select the first Command Center we find
				Bool alreadyFound = FALSE;
				player->iterateObjects(findAndSelectCommandCenter, &alreadyFound);
			}
		}
	}

	if(m_gameMode == GAME_SHELL)
	{
		HideControlBar();
	}
	else
	{
		ShowControlBar(FALSE);
	}

#ifdef DO_UNIT_TIMINGS
	// Turn off the UI
	HideControlBar();
#endif
	TheWritableGlobalData->m_loadScreenRender = FALSE;	///< mark to resume rendering as normal

	// if we're in a gamespy game, mark us as playing
	if (TheGameSpyBuddyMessageQueue && TheGameSpyGame && isInInternetGame())
	{
		BuddyRequest req;
		req.buddyRequestType = BuddyRequest::BUDDYREQUEST_SETSTATUS;
		req.arg.status.status = GP_PLAYING;
		strcpy(req.arg.status.statusString, "Playing");
		strlcpy(req.arg.status.locationString, WideCharStringToMultiByte(TheGameSpyGame->getGameName().str()).c_str(),
			ARRAY_SIZE(req.arg.status.locationString));
		TheGameSpyBuddyMessageQueue->addRequest(req);
	}

  if( loadingSaveGame == FALSE )
  {
    // Drawables need to do some work on level start; give them a chance to do it
    Drawable * drawable = TheGameClient->getDrawableList();

    while ( drawable != nullptr )
    {
      drawable->onLevelStart();
      drawable = drawable->getNextDrawable();
    }
  }

	//ReAllows quit menu to work during loading scene
	//setGameLoading(FALSE);
	setLoadingMap( FALSE );


	//Assume that getting this far means we've successfully entered an online game.
	//Add an additional disconnection to player stats in case he doesn't complete this game. -MW
	if (TheGameSpyInfo)
		TheGameSpyInfo->updateAdditionalGameSpyDisconnections(1);


  if ( isInReplayGame() && TheInGameUI && TheGameText )
  {
		TheInGameUI->messageNoFormat( TheGameText->FETCH_OR_SUBSTITUTE( "GUI:FastForwardInstructions", L"Press F to toggle Fast Forward" ) );
  }

#ifdef true
	AsciiString message;
	message.format("GameStart: %s", TheGlobalData->m_mapName.str());
	engine::profiling::message(message.str());
#endif
}

//-----------------------------------------------------------------------------------------
void GameLogic::createOptimizedTree(const ThingTemplate *thingTemplate, Coord3D *pos, Real angle)
{
	// Opt trees and props just get drawables to tell the client about it, then deleted. jba [6/5/2003]
	// This way there is no logic object to slow down partition manager and core logic stuff.
	// TheSuperHackers @info Destroying the drawable will register the tree in the tree buffer.
	Drawable *draw = TheThingFactory->newDrawable(thingTemplate);
	if (draw) {
		draw->setOrientation(angle);
		draw->setPosition(pos);
		TheGameClient->destroyDrawable(draw);
	}
}

//-----------------------------------------------------------------------------------------
static void findAndSelectCommandCenter(Object *obj, void* alreadyFound)
{
	if (!((*(Bool*)alreadyFound)) && obj->isKindOf(KINDOF_COMMANDCENTER) )
	{
		((*(Bool*)alreadyFound)) = TRUE;
		TheGameLogic->selectObject(obj, TRUE, obj->getControllingPlayer()->getPlayerMask(), obj->isLocallyControlled());

	}
}
//-----------------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------------





// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::loadMapINI( AsciiString mapName )
{

	if (!TheMapCache) {
		// Need the map cache to get the map and user map directories.
		return;
	}

	//
	// if map name begins with a "SAVE_DIRECTORY\", then the map refers to a map
	// that has been extracted from a save game file ... in that case we need to get
	// the pristine map name string in order to manipulate and load the right map.ini
	// for that map from it's original location
	//
	const char* pristineMapName = TheGameState->isInSaveDirectory(mapName.str())
		? TheGameState->getSaveGameInfo()->pristineMapName.str()
		: mapName.str();

	char filename[_MAX_PATH];
	strlcpy(filename, pristineMapName, ARRAY_SIZE(filename));

	// sanity
	int length = strlen(filename);
	if (length < 4) {
		return;
	}

	// back up over the ".map" extension and to the first directory separator
	char *extension = filename + length - 4;
	while ((extension > filename) && (*extension != '\\') && (*extension != '/')) {
		--extension;
	}
	*extension = 0;


	char fullFledgeFilename[_MAX_PATH];
	snprintf(fullFledgeFilename, ARRAY_SIZE(fullFledgeFilename), "%s\\map.ini", filename);
	if (TheFileSystem->doesFileExist(fullFledgeFilename)) {
		engine::debug::log_info("Loading map.ini");
		INI ini;
		ini.load( AsciiString(fullFledgeFilename), INI_LOAD_CREATE_OVERRIDES, nullptr );
	}

	// TheSuperHackers @todo Implement ini load directory for map folder.
	// Requires adjustments in map transfer.

	snprintf(fullFledgeFilename, ARRAY_SIZE(fullFledgeFilename), "%s\\solo.ini", filename);
	if (TheFileSystem->doesFileExist(fullFledgeFilename)) {
		engine::debug::log_info("Loading solo.ini");
		INI ini;
		ini.load( AsciiString(fullFledgeFilename), INI_LOAD_CREATE_OVERRIDES, nullptr );
	}

	// No error here. There could've just *not* been a map.ini file.

	// now look for a string file
	snprintf(fullFledgeFilename, ARRAY_SIZE(fullFledgeFilename), "%s\\map.str", filename);

	if (TheFileSystem->doesFileExist(fullFledgeFilename)) {
		TheGameText->initMapStringFile(fullFledgeFilename);
	}

	// we want to do this before doing the actual map load!
	if (TheDisplay)
	{
		const char* ASSET_USAGE_FILE_NAME = "AssetUsage.txt";
		snprintf(fullFledgeFilename, ARRAY_SIZE(fullFledgeFilename), "%s\\%s", filename, ASSET_USAGE_FILE_NAME);
		// note: call this EVEN IF THE FILE IN QUESTION DOES NOT EXIST.
		TheDisplay->doSmartAssetPurgeAndPreload(fullFledgeFilename);
	}

}

// ------------------------------------------------------------------------------------------------
/** Process the destroy list, destroying all pending objects.
 * The destroy list exists to ensure that all objects have a chance to
 * see each other at each simulation frame - the object list is the
 * same at the start of the update as it is at the end of the update. */
// ------------------------------------------------------------------------------------------------
void GameLogic::processDestroyList()
{
	//engine::profiling::Scope profile_scope_2551("processDestroyList")

	for( ObjectPointerListIterator iterator = m_objectsToDestroy.begin(); iterator != m_objectsToDestroy.end(); iterator++ )
	{
		Object* currentObject = (*iterator);

#ifdef ALLOW_NONSLEEPY_UPDATES
		for (std::list<UpdateModulePtr>::iterator it = m_normalUpdates.begin(); it != m_normalUpdates.end(); /* nothing */)
		{
			if ((*it)->friend_getObject() == currentObject)
			{
				it = m_normalUpdates.erase(it);
			}
			else
			{
				++it;
			}
		}
#endif

		for (BehaviorModule** module=currentObject->getBehaviorModules(); *module; ++module)
		{
#ifdef DIRECT_UPDATEMODULE_ACCESS
			auto update=(UpdateModulePtr)((*module)->getUpdate());
#else
			auto update=(*module)->getUpdate();
#endif
			if (update && update->friend_getIndexInLogic()>=0) eraseSleepyUpdate(update);
		}
		currentObject->removeFromList(&m_objList);//remove from object list

		// remove object from lookup table
		removeObjectFromLookupTable( currentObject );

		Object::friend_deleteInstance(currentObject);//actual delete
	}

	m_objectsToDestroy.clear();//list full of bad pointers now, clear it.  If anyone's deletion resulted
	//in the request for a new deletion (sub-object), the new object was added to the end of this list.
}

//-------------------------------------------------------------------------------------------------
/** Process the command list passed to the logic from the network */
//-------------------------------------------------------------------------------------------------
void GameLogic::processCommandList( CommandList *list )
{
	m_cachedCRCs.clear();
	m_shouldValidateCRCs = FALSE;

	GameMessage* msg;

	for( msg = list->getFirstMessage(); msg; msg = msg->next() )
	{
#ifdef RTS_DEBUG
		engine::debug::invariant((msg != nullptr && msg != (GameMessage*)0xdeadbeef), "msg != nullptr && msg != (GameMessage*)0xdeadbeef", __FILE__, __LINE__, "bad msg");
#endif
		logicMessageDispatcher( msg, nullptr );
	}

	if (m_shouldValidateCRCs && !TheNetwork->sawCRCMismatch())
	{
		Bool sawCRCMismatch = FALSE;
		Int numPlayers = 0;
		engine::debug::invariant((TheNetwork), "TheNetwork", __FILE__, __LINE__, "No Network!");
		if (TheNetwork)
		{
			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				if (TheNetwork->isPlayerConnected(i))
					++numPlayers;
			}

			if (m_cachedCRCs.size() < numPlayers)
			{
				engine::debug::invariant(false, "debug failure", __FILE__, __LINE__, "Not enough CRCs!");
				sawCRCMismatch = TRUE;
			}
			else
			{
				Bool hasReferenceCRC = FALSE;
				UnsignedInt referenceCRC = 0;

				for (CachedCRCMap::const_iterator it = m_cachedCRCs.begin(); it != m_cachedCRCs.end(); ++it)
				{
					// TheSuperHackers @bugfix Caball009 14/06/2026 Check if player is still connected,
					// to avoid spurious mismatches at low CRC intervals, e.g. every frame.
					const Int slotIndex = ThePlayerList->getSlotIndex(it->first);
					if (slotIndex >= 0 && !TheNetwork->isPlayerConnected(slotIndex))
						continue;

					const UnsignedInt crc = it->second;

					if (!hasReferenceCRC)
					{
						hasReferenceCRC = TRUE;
						referenceCRC = crc;
						continue;
					}

					if (referenceCRC != crc)
					{
						engine::debug::invariant(false, "debug failure", __FILE__, __LINE__, "CRC mismatch!");
						sawCRCMismatch = TRUE;
					}
				}
			}
		}

		if (sawCRCMismatch)
		{
			engine::debug::log_info("CRC Mismatch - saw %d CRCs from %d players", m_cachedCRCs.size(), numPlayers);
			for (CachedCRCMap::const_iterator crcIt = m_cachedCRCs.begin(); crcIt != m_cachedCRCs.end(); ++crcIt)
			{
				Player *player = ThePlayerList->getNthPlayer(crcIt->first);
				engine::debug::log_info("CRC from player %d (%ls) = %X", crcIt->first,
					player?player->getPlayerDisplayName().str():L"<NONE>", crcIt->second);
			}
			TheNetwork->setSawCRCMismatch();
		}
	}

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool GameLogic::isIntroMoviePlaying()
{
	/// @todo remove this hack
	return m_startNewGame && TheDisplay->isMoviePlaying();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::selectObject(Object *obj, Bool createNewSelection, PlayerMaskType playerMask, Bool affectClient)
{
	if (!obj)
	{
		return;
	}

	if (!obj->isMassSelectable() && !createNewSelection)
	{
		engine::debug::log_info("GameLogic::selectObject() - Object attempted to be added to selection, but isn't mass-selectable.");
		return;
	}

	while( playerMask )
	{
		Player *player = ThePlayerList->getEachPlayerFromMask(playerMask);
		if( !player )
		{
			return;
		}

		engine::debug::log_info( "Creating AIGroup in GameLogic::selectObject()" );
		AIGroupPtr group = TheAI->createGroup();
		group->add(obj);

		// add all selected agents to the AI group
		if (createNewSelection)
		{
#if RETAIL_COMPATIBLE_AIGROUP
			player->setCurrentlySelectedAIGroup(group);
#else
			player->setCurrentlySelectedAIGroup(group.Peek());
#endif
		}
		else
		{
#if RETAIL_COMPATIBLE_AIGROUP
			player->addAIGroupToCurrentSelection(group);
#else
			player->addAIGroupToCurrentSelection(group.Peek());
#endif
		}

#if RETAIL_COMPATIBLE_AIGROUP
		TheAI->destroyGroup(group);
#else
		group->removeAll();
#endif

		if( affectClient )
		{
			Drawable *draw = obj->getDrawable();
			if( draw )
			{
				TheInGameUI->selectDrawable(draw);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::deselectObject(Object *obj, PlayerMaskType playerMask, Bool affectClient)
{
	if (!obj) {
		return;
	}

	while (playerMask) {
		Player *player = ThePlayerList->getEachPlayerFromMask(playerMask);
		if (!player) {
			return;
		}

		engine::debug::log_info( "Removing a unit from a selected group in GameLogic::deselectObject()" );
		AIGroupPtr group = TheAI->createGroup();
#if RETAIL_COMPATIBLE_AIGROUP
		player->getCurrentSelectionAsAIGroup(group);
#else
		player->getCurrentSelectionAsAIGroup(group.Peek());
#endif

		if (group) {
			Bool emptied = group->remove(obj);

			// Set this to be the currently selected group.
			if (!emptied) {
#if RETAIL_COMPATIBLE_AIGROUP
				player->setCurrentlySelectedAIGroup(group);
				// Then, cleanup the group.
				TheAI->destroyGroup(group);
#else
				player->setCurrentlySelectedAIGroup(group.Peek());
				// Then, cleanup the group.
				group->removeAll();
#endif
			} else {
				// nullptr will clear the group.
				player->setCurrentlySelectedAIGroup(nullptr);
			}

			if (affectClient) {
				Drawable *draw = obj->getDrawable();
				if (draw) {
					TheInGameUI->deselectDrawable(draw);
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------
inline void GameLogic::validateSleepyUpdate() const
{
#ifdef DEBUG_CRASHING
    m_scheduledUpdates->queue.forEach([&](auto index,UpdateModulePtr module) {
        engine::debug::invariant((module->friend_getIndexInLogic()==static_cast<Int>(index)), "module->friend_getIndexInLogic()==static_cast<Int>(index)", __FILE__, __LINE__, "Scheduled owner index mismatch");
        engine::debug::invariant((module->friend_getPriority()==m_scheduledUpdates->queue.priority(index)), "module->friend_getPriority()==m_scheduledUpdates->queue.priority(index)", __FILE__, __LINE__, "Scheduled deadline mismatch");
    });
#endif
}

#if defined(RTS_DEBUG)
Int GameLogic::getNumberSleepyUpdates() const { return static_cast<Int>(m_scheduledUpdates->queue.size()); }
#endif

void GameLogic::eraseSleepyUpdate(UpdateModulePtr module)
{
    if (!m_scheduledUpdates->queue.erase(module->friend_getIndexInLogic(),module))
        engine::debug::panic("%s", "Scheduled module ownership mismatch during removal");
    module->friend_setIndexInLogic(-1);
}

void GameLogic::pushSleepyUpdate(UpdateModulePtr module)
{
    const auto index=m_scheduledUpdates->queue.insert(module,module->friend_getPriority());
    module->friend_setIndexInLogic(static_cast<Int>(index));
}

UpdateModulePtr GameLogic::peekSleepyUpdate() const
{
    return m_scheduledUpdates->queue.front();
}

void GameLogic::friend_awakenUpdateModule(Object* obj, UpdateModulePtr u, UnsignedInt whenToWakeUp)
{
	//engine::profiling::Scope profile_scope_2832("friend_awakenUpdateModule")
	UnsignedInt now = getFrame();
	engine::debug::invariant((whenToWakeUp >= now), "whenToWakeUp >= now", __FILE__, __LINE__, "setWakeFrame frame is in the past... are you sure this is what you want?");

	if (u == m_curUpdateModule)
	{
		engine::debug::invariant(false, "debug failure", __FILE__, __LINE__, "You should not call setWakeFrame() from inside your update(), because it will be ignored, in favor of the return code from update.");
		return;
	}

	if (whenToWakeUp == u->friend_getNextCallFrame())
		return;	// my, that was easy

	if ((now > 0) && (u->friend_getNextCallFrame() == now) && (whenToWakeUp == now + 1))
	{
		// subtle but important case: if we already awake, and someone calls
		// setWakeFrame(self, UPDATE_SLEEP_NONE), we don't want to reset our wake frame,
		// since that would prevent us from getting called THIS frame. since UPDATE_SLEEP_NONE
		// really means "wake up as soon as possible", we don't want to change our status
		// if we are already awake. (srj)
		return;
	}

	Int idx = u->friend_getIndexInLogic();
	if (obj->isInList(&m_objList))
	{
        if (!m_scheduledUpdates->queue.contains(static_cast<std::size_t>(idx),u))
        {
            engine::debug::panic("%s", "Scheduled module ownership mismatch during wakeup");
            return;
        }

		// update the value.
		u->friend_setNextCallFrame(whenToWakeUp);

		// Move the registered module to its new deadline.
		m_scheduledUpdates->queue.reschedule(idx,u,u->friend_getPriority());

		// validate. (harmless except in debug mode)
		validateSleepyUpdate();

		return;
	}
	else
	{
		if (idx != -1)
		{
			engine::debug::panic("%s", "fatal error! sleepy update module index mismatch.");
			return;
		}

		// this can happen if stuff happens during object initialization. fortunately,
		// it's easy to deal with:
		u->friend_setNextCallFrame(whenToWakeUp);
		return;
	}
}







// ------------------------------------------------------------------------------------------------
#ifdef DO_UNIT_TIMINGS

void drawGraph( const char* style, Real scale, double value )
{
  for ( Int t = 0; t < value*scale; ++t)
  {
    engine::debug::log_info(style);
    if ( t%200 == 199 )
      engine::debug::log_info("...");
  }

  engine::debug::log_info("\n");

}


	enum {TIME_FRAMES=20};
	enum {SETTLE_FRAMES=10};
static void unitTimings()
{
	static Int settleFrames = 0;
	static Int timeFrames = 0;
	enum { INFANTRY, VEHICLE, STRUCTURE, OTHER, END};
	static Int unitTypes = INFANTRY;
	AsciiString sides[16];


	const Int UNIT_X = 10;
	const Int UNIT_Y = 10;
	const Int TOTAL_UNITS = UNIT_X*UNIT_Y;


	const Int UNIT_SPACING = 30;
	const Int UNIT_BORDER = 30;


	Int sideCount = 0;
  #define no_SINGLE_UNIT "AmericaInfantryRanger"

#define DO_FACTION
#ifdef DO_FACTION
	sides[sideCount++] = "America";
	sides[sideCount++] = "China";
	sides[sideCount++] = "GLA";

  //The MissionDisk new 'sides'
	sides[sideCount++] = "AmericaSuperWeaponGeneral";
	sides[sideCount++] = "AmericaLaserGeneral";
	sides[sideCount++] = "AmericaAirForceGeneral";
	sides[sideCount++] = "ChinaTankGeneral";
	sides[sideCount++] = "ChinaInfantryGeneral";
	sides[sideCount++] = "ChinaNukeGeneral";
	sides[sideCount++] = "GLAToxinGeneral";
	sides[sideCount++] = "GLADemolitionGeneral";
	sides[sideCount++] = "GLAStealthGeneral";

//  sides[sideCount++] = "*"; // wildcard for unspecified side


#endif

#define DO_CIVILIAN
#ifdef DO_CIVILIAN
	sides[sideCount++] = "Civilian";
#endif

#define DO_SINGLE_KIND
#ifdef DO_SINGLE_KIND
	static KindOfType goalKind = KINDOF_STRUCTURE;
	//static KindOfType goalKind = KINDOF_INFANTRY;
	//static KindOfType goalKind = KINDOF_VEHICLE;
#endif

	sides[sideCount] = "";
	static bool veryFirstTime = true;


	static Int side = 0;

	static std::uint64_t startTime64;
	static std::uint64_t endTime64,freq64;
	static int drawCallTotal;
	static enum { LOGIC, NO_PARTICLES, NO_SPAWN, ALL} mode;
	static double timeAll, timeAllNoAnim, timeNoPart, timeNoSpawn, timeLogic, timeLogicNoAnim;
	static float drawCallAll,drawCallNoPart,drawCallNoSpawn,drawCallLogic;

	if (settleFrames>0) {
		settleFrames--;
		if (settleFrames>0) return;

		startTime64 = m_clock.monotonic_nanoseconds();
		freq64 = 1000000000u;
		timeFrames = TIME_FRAMES;

		// reset the draw counter
		drawCallTotal = 0;
		return;
	}
	if (timeFrames>0) {
		drawCallTotal += TheDisplay->getLastFrameDrawCalls();
		timeFrames--;
		if (timeFrames>0) return;

		endTime64 = m_clock.monotonic_nanoseconds();
		double timeToUpdate = ((double)(endTime64-startTime64) / (double)(freq64));

//		Real timeToUpdateMicrosec = timeToUpdate*1E6/(TIME_FRAMES * TOTAL_UNITS);
		timeToUpdate *= 100.0f/TIME_FRAMES;


		if (mode == LOGIC) {
			timeLogic = timeToUpdate;
			drawCallLogic = (float)drawCallTotal / (float)(TIME_FRAMES * TOTAL_UNITS);  // 100 units for TIME_FRAMES

			mode = ALL;
			settleFrames = SETTLE_FRAMES;
			//g_timing_no_anim = true;
			Coord3D thePos;
			thePos.x = 50;
			thePos.y = 50;
			thePos.z = 0;
			TheTacticalView->lookAt( &thePos );
			return;
		}
		if (mode == ALL) {
			timeAll = timeToUpdate;
			drawCallAll = (float)drawCallTotal / (float)(TIME_FRAMES * TOTAL_UNITS);  // 100 units for TIME_FRAMES

			mode = NO_PARTICLES;
			settleFrames = SETTLE_FRAMES;
			if (TheParticleSystemManager->getParticleCount()>1) {
				TheParticleSystemManager->reset();
				engine::debug::log_info("Starting noParticles - ");
			}
			return;
		}
		if (mode == NO_PARTICLES) {
			timeNoPart = timeToUpdate;
			drawCallNoPart = (float)drawCallTotal / (float)(TIME_FRAMES * TOTAL_UNITS);  // 100 units for TIME_FRAMES

			mode = NO_SPAWN;
			Object *obj = TheGameLogic->getFirstObject();
			Bool gotSpawn;
			while (obj) {
				if (obj->getTemplate() != g_UT_curThing) {
					TheGameLogic->destroyObject(obj);
					gotSpawn = true;
				}
				obj = obj->getNextObject();
			}
			if (gotSpawn) {
				engine::debug::log_info("Starting noSpawn - ");
				settleFrames = SETTLE_FRAMES;
				return;
			}
		}
		if (mode==NO_SPAWN) {
			timeNoSpawn = timeToUpdate;
			drawCallNoSpawn = (float)drawCallTotal / (float)(TIME_FRAMES * TOTAL_UNITS);  // 100 units for TIME_FRAMES
		}
		if (g_UT_curThing==nullptr) return;


		char remark[2048];
    Real graphScale = 20.0f;
		AsciiString thingName = g_UT_curThing->getName();
		if (veryFirstTime) {
			thingName = "No Object";
		}

    sprintf(remark, "All %f: (%d ms) for %d %s's\n", timeAll, (Int)(timeAll*1000/TIME_FRAMES), TOTAL_UNITS, thingName.str() );
    engine::debug::log_info(remark);
    drawGraph( "@", graphScale, timeAll );

		sprintf(remark, "Without Particles %f\n", timeNoPart);
    engine::debug::log_info(remark);
    drawGraph( "@", graphScale, timeNoPart );

 		sprintf(remark, "Without Spawn %f  \n", timeNoSpawn );
    engine::debug::log_info(remark);
    drawGraph( "@", graphScale, timeNoSpawn );

 		sprintf(remark, "Logic %f \n", timeLogic);
    engine::debug::log_info(remark);
    drawGraph( "@", graphScale, timeLogic );


		sprintf(remark, "DrawCalls for %s \n", thingName.str() ) ;
		engine::debug::log_info(remark);

		sprintf(remark, "All %f\n", drawCallAll );
		engine::debug::log_info(remark);
    drawGraph( "#", graphScale, drawCallAll );

		sprintf(remark, "Without Particles %f\n", drawCallNoPart );
		engine::debug::log_info(remark);
    drawGraph( "#", graphScale, drawCallNoPart );

		sprintf(remark, "Without Spawn %f \n", drawCallNoSpawn );
		engine::debug::log_info(remark);
    drawGraph( "#", graphScale, drawCallNoSpawn );

		sprintf(remark, "Draw Call Logic %f \n", drawCallLogic );
		engine::debug::log_info(remark);
    drawGraph( "#", graphScale, drawCallLogic );


		if (g_UT_timingLog) {
			fputs(remark, g_UT_timingLog);
		}
		if (g_UT_commaLog) {
			AsciiString type;
			if (unitTypes == INFANTRY) {
				type="Infantry";
			}	else if (unitTypes == VEHICLE) {
				type="Vehicle";
			}	else if (unitTypes == STRUCTURE) {
				type="Structure";
			}	else {
				type="Other";
			}
			AsciiString modelName;
			ModelConditionFlags state;
			state.clear();
			const ModuleInfo& mi = g_UT_curThing->getDrawModuleInfo();
			if (mi.getCount() > 0)
			{
				const ModuleData* mdd = mi.getNthData(0);
				const W3DModelDrawModuleData* md = mdd ? mdd->getAsW3DModelDrawModuleData() : nullptr;
				if (md)
				{
					modelName = md->getBestModelNameForWB(state);
				}
			}
			if (veryFirstTime) {
				modelName = "**NO MODEL**";
				veryFirstTime = false;
			}
			sprintf(remark, "%f,%d,%f,%d,%f,%d,%f,%d,%s,%s,%s,%s,%f,%f,%f\n", timeAll,
			(Int)(timeAll*1000/TIME_FRAMES),timeNoPart,
			(Int)(timeNoPart*1000/TIME_FRAMES),timeNoSpawn,
			(Int)(timeNoSpawn*1000/TIME_FRAMES),timeLogic,
			(Int)(timeLogic*1000/TIME_FRAMES), thingName.str(), modelName.str(), type.str(),
			sides[side].str(),
			drawCallAll,drawCallNoPart,drawCallNoSpawn);
			fputs(remark, g_UT_commaLog);
		}
		TheParticleSystemManager->reset();
		g_UT_gotUnit = false;
	}

#ifdef SINGLE_UNIT
	if (g_UT_curThing && g_UT_startTiming) {
		if (g_UT_curThing->getName()==SINGLE_UNIT) {
			if (g_UT_timingLog) {
				fclose(g_UT_timingLog);
				g_UT_timingLog = nullptr;
			}
			if (g_UT_commaLog) {
				fclose(g_UT_commaLog);
				g_UT_commaLog = nullptr;
			}
			return;
		}
		while (g_UT_curThing->friend_getNextTemplate()
			&& g_UT_curThing->friend_getNextTemplate()->getName()!=SINGLE_UNIT)
			g_UT_curThing = g_UT_curThing->friend_getNextTemplate();

	}
#endif

	Object *obj = TheGameLogic->getFirstObject();
	while (obj) {
		TheGameLogic->destroyObject(obj);
		obj = obj->getNextObject();
	}

	if (g_UT_startTiming && g_UT_curThing && !g_UT_gotUnit) {
		TheWritableGlobalData->m_framesPerSecondLimit = 10000;
		TheWritableGlobalData->m_useFpsLimit = false;
		while (!g_UT_gotUnit) {
			if (veryFirstTime) {
				g_UT_gotUnit = true;
				break;
			}
			g_UT_curThing = g_UT_curThing->friend_getNextTemplate();

			if (g_UT_curThing == nullptr) {
				unitTypes++;
				if (unitTypes==END) {
					side++;
					unitTypes = INFANTRY;
					if (sides[side].isEmpty() ) // end of sides list
          {
						g_UT_startTiming = false;
						if (g_UT_timingLog)
            {
							fclose(g_UT_timingLog);
							g_UT_timingLog = nullptr;
						}
						if (g_UT_commaLog)
            {
							fclose(g_UT_commaLog);
							g_UT_commaLog = nullptr;
						}
						break;
					}
				}
				g_UT_curThing = TheThingFactory->firstTemplate();
			}

			const ThingTemplate* btt = g_UT_curThing;

#ifndef SINGLE_UNIT
      Bool unspecified = FALSE;
			if (btt->getDefaultOwningSide() != sides[side])
      {
        if (sides[side] == "*")
        {
          if ( btt->getDefaultOwningSide().isEmpty() )// wildcard for unspecified side
            unspecified = TRUE;
          else
            continue;
        }
        else
    		  continue;

			}

			if (unitTypes == INFANTRY) {
				if (!btt->isKindOf(KINDOF_INFANTRY)) continue;
			}	else if (unitTypes == VEHICLE) {
				if (!btt->isKindOf(KINDOF_VEHICLE)) continue;
			}	else if (unitTypes == STRUCTURE) {
				if (!btt->isKindOf(KINDOF_STRUCTURE)) continue;
			}	else {
				if (btt->isKindOf(KINDOF_INFANTRY)) continue;
				if (btt->isKindOf(KINDOF_VEHICLE)) continue;
				if (btt->isKindOf(KINDOF_STRUCTURE)) continue;
			}
#endif
#ifdef DO_SINGLE_KIND
			if (!btt->isKindOf(goalKind)) continue;
#endif


      static const char *const illegalTemplateNames[] =
      {
	      "EMPPulseBomb",
	      "GLAAngryMobRockProjectileObject",
	      "ClusterMinesBomb",
	      "BlackNapalmFirestormSmall",
	      "CabooseFullOfTerrorists",
	      "GLAAngryMobMolotovCocktailProjectileObject",
	      "Firestorm",
	      "Avalanche",
	      "InfernoTankShell",
	      "ChinaArtilleryBarrageShell",
	      "ChinaTankOverlordBattleBunker",
	      "ChinaTankOverlordPropagandaTower",
	      "ChinaTankOverlordGattlingCannon",
	      "CINE",
	      "GLAInfantryAngryMobNexus",
	      "AircraftCarrier",
        "GermanMuseum",
        "Cin_",
        "Amb_",
        "Ambient",
        "GC_",
		"SpecialEffectsTrainCrashObject",
	      nullptr
      };

      Bool skip = FALSE;

      for ( Int test = 0; test < sizeof( illegalTemplateNames ) ; ++test )
      {
        if ( illegalTemplateNames[test] == nullptr )
          break;

        if (btt->getName().startsWith(illegalTemplateNames[test]))
        {
          skip = TRUE;
          break;
        }
        if (btt->getName().endsWith(illegalTemplateNames[test]))
        {
          skip = TRUE;
          break;
        }
        if (btt->getName() == illegalTemplateNames[test] )
        {
          skip = TRUE;
          break;
        }
      }

      if ( skip )
        continue;

//			if (btt->getName() endsWith("EMPPulseBomb")) continue; // 100 overloads system.
//			if (btt->getName() endsWith("GLAAngryMobRockProjectileObject")) continue; // 100 overloads system.
//			if (btt->getName() endsWith("ClusterMinesBomb")) continue; // 100 overloads system.
//			if (btt->getName() endsWith("BlackNapalmFirestormSmall")) continue; // 100 overloads system.
//			if (btt->getName() endsWith("CabooseFullOfTerrorists")) continue; // 100 overloads system.
//			if (btt->getName() endsWith("GLAAngryMobMolotovCocktailProjectileObject")) continue; // 100 overloads system.
//			if (btt->getName().startsWith("Firestorm"))	continue;	// 100 crashes
//			if (btt->getName().startsWith("Avalanche"))	continue;	// 100 crashes
//			if (btt->getName().startsWith("InfernoTankShell"))	continue;	// 100 crashes
//
//			if (btt->getName() endsWith("ChinaArtilleryBarrageShell") continue; // 100 takes really, freaking long. Doesn't crash jba.
//			if (btt->getName() endsWith("ChinaTankOverlordBattleBunker") continue; // 100 seems to hang gth.
//			if (btt->getName() endsWith("ChinaTankOverlordPropagandaTower") continue; // 100 seems to hang gth.
//			if (btt->getName() endsWith("ChinaTankOverlordGattlingCannon") continue; // 100 seems to hang gth.
//			if (btt->getName().startsWith("CINE")) continue;
//			if (btt->getName() endsWith("GLAInfantryAngryMobNexus") continue;
//
//      //missiondisk perps
//
//      if (btt->getName() == "AmericaAircraftCarrier") continue;



#ifdef SINGLE_UNIT
			if (btt->getName()!=SINGLE_UNIT) {
				engine::debug::log_info("Skipping %s", btt->getName().str());
				continue;
			}
#endif
			engine::debug::log_info("\n===Doing thing %s ===", btt->getName().str());


#define dont_DO_ATTACK
#ifdef DO_ATTACK
			Coord3D attackedPos;
			attackedPos.x = UNIT_SPACING * 4.5 + UNIT_BORDER;
			attackedPos.y = UNIT_SPACING * 4.5 + UNIT_BORDER;
			attackedPos.z = TheTerrainLogic->getGroundHeight( attackedPos.x, attackedPos.y );

#endif

			Int i, j;
			for (i=0; i<10; i++) {
				for (j=0; j<10; j++) {
					Coord3D pos;
					pos.x = UNIT_SPACING*i+UNIT_BORDER;
					pos.y = UNIT_SPACING*j+UNIT_BORDER;
					pos.z = TheTerrainLogic->getGroundHeight( pos.x, pos.y );
					Team *team = ThePlayerList->getNthPlayer(1)->getDefaultTeam();
					Object *obj = TheThingFactory->newObject( btt, team );
					if (obj==nullptr) break;
#define dont_DO_TREE	1
#ifdef DO_TREE
					TheGameLogic->destroyObject(obj);
					externalAddTree(pos, 1.0f, 0.0f, "TreeOakFall1");
					g_UT_gotUnit = true;
#else
					if (obj)
					{
						g_UT_gotUnit = true;

						obj->setOrientation(0);
						obj->setPosition( &pos );

						// Now onCreates were called at the constructor.  This magically created
						// thing needs to be considered as Built for Game specific stuff.
						for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
						{
							CreateModuleInterface* create = (*m)->getCreate();
							if (!create)
								continue;
							create->onBuildComplete();
						}
						// Since the team now has members, activate it.
						team->setActive();
						TheAI->pathfinder()->addObjectToPathfindMap(obj);
#ifdef DO_ATTACK
						AIUpdateInterface *myAI = obj->getAI();
						if(myAI)
						{
							myAI->aiAttackPosition(&attackedPos, 9999, CMD_FROM_AI);
						}
#endif
					}
#endif
				}
			}
		}
		if (g_UT_gotUnit) {
			settleFrames = TIME_FRAMES/2;
			Coord3D thePos;
			thePos.x = 5000;
			thePos.y = 50;
			thePos.z = 0;
			TheTacticalView->lookAt( &thePos );
			mode = LOGIC;
			return;
		}
	}
}
#endif


// ------------------------------------------------------------------------------------------------
/** Update all objects in the world by invoking their update() methods. */
// ------------------------------------------------------------------------------------------------
void GameLogic::update()
{
	auto& capture=navigation::diagnostics::frameCapture();
	capture.setModuleNameResolver([](unsigned key)->std::string {
		return TheNameKeyGenerator ? TheNameKeyGenerator->keyToName(NameKeyType(key)).str() : "";
	});
	engine::profiling::Scope profile_scope_3415("GameLogic_update");
	engine::profiling::Scope profile_scope_3416("Unnamed", 0x4CAF50);

	LatchRestore<Bool> inUpdateLatch(m_isInUpdate, TRUE);
#ifdef DO_UNIT_TIMINGS
	unitTimings();
#endif

	setFPMode();

	/// @todo remove this hack
	if ( m_startNewGame && !TheDisplay->isMoviePlaying())
	{

#if defined(RTS_PROFILE_TRACY)
    
#endif
		startNewGame( FALSE );
#if defined(RTS_PROFILE_TRACY)
    
#endif
		m_startNewGame = FALSE;

	}

	// send the current time to the GameClient
	UnsignedInt now = getFrame();
	TheGameClient->setFrame(now);

	engine::profiling::plot("LogicFrame", static_cast<int64_t>(now));

	// update (execute) scripts
	{
		auto timing=capture.measure("logic.scripts",now);
		TheScriptEngine->update();
	}

	// TheSuperHackers @info Updates the frozen time status because it may have changed after the script engine update.
	TheFramePacer->setTimeFrozen(TheGameEngine->isTimeFrozen());

	if (TheFramePacer->isTimeFrozen())
		return;

	// Note - TerrainLogic update needs to happen after ScriptEngine update, but before object updates.  jba.
	// This way changes in bridges are noted in the script engine before being cleared in TerrainLogic->update
	{
		auto timing=capture.measure("logic.terrain",now);
		TheTerrainLogic->update();
	}

	// force CRC calculation, so we can keep a cache of the last N CRCs.  We do this right where the recorder
	// would be getting the CRC anyway, so replays can get the CRCs from the exact instant in time as the original.
	Bool isMPGameOrReplay = (TheRecorder && TheRecorder->isMultiplayer() && getGameMode() != GAME_SHELL && getGameMode() != GAME_NONE);
	Bool isSoloGameOrReplay = (TheRecorder && !TheRecorder->isMultiplayer() && getGameMode() != GAME_SHELL && getGameMode() != GAME_NONE);
	Bool generateForMP = (isMPGameOrReplay && (m_frame % TheGameInfo->getCRCInterval()) == 0);
#ifdef DEBUG_CRC
	Bool generateForSolo = isSoloGameOrReplay && ((m_frame && (m_frame%100 == 0)) ||
		(getFrame() >= TheCRCFirstFrameToLog && getFrame() < TheCRCLastFrameToLog && ((m_frame % REPLAY_CRC_INTERVAL) == 0)));
#else
	Bool generateForSolo = isSoloGameOrReplay && ((m_frame % REPLAY_CRC_INTERVAL) == 0);
#endif // DEBUG_CRC

	if (generateForSolo || generateForMP)
	{
		auto timing=capture.measure("logic.crc",now);
		m_CRC = getCRC( CRC_RECALC );
		bool isPlayback = (TheRecorder && TheRecorder->isPlaybackMode());

		GameMessage *msg = newInstance(GameMessage)(GameMessage::MSG_LOGIC_CRC);
		msg->appendIntegerArgument(m_CRC);
		msg->appendBooleanArgument(isPlayback);

		// TheSuperHackers @info helmutbuhler 13/04/2025
		// During replay simulation, we bypass TheMessageStream and instead put the CRC message
		// directly into TheCommandList because we don't update TheMessageStream during simulation.
		GameMessageList *messageList = TheMessageStream;
		if (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_SIMULATION_PLAYBACK)
			messageList = TheCommandList;
		messageList->appendMessage(msg);

		engine::debug::log_info("Appended %sCRC on frame %d: %8.8X", isPlayback ? "Playback " : "", m_frame, m_CRC);
	}

	// collect stats
	if(TheStatsCollector)
	{
		TheStatsCollector->update();
	}

	// Update the Recorder
	{
		auto timing=capture.measure("logic.recorder",now);
		TheRecorder->update();
	}

	// process client commands
	{
		auto timing=capture.measure("logic.commands",now);
		processCommandList( TheCommandList );
	}

#ifdef ALLOW_NONSLEEPY_UPDATES
	{
		for (std::list<UpdateModulePtr>::const_iterator it = m_normalUpdates.begin(); it != m_normalUpdates.end(); ++it)
		{
			UpdateModulePtr u = *it;
			DisabledMaskType dis = u->friend_getObject()->getDisabledFlags();
#if RETAIL_COMPATIBLE_CRC
			if (!dis.any() || dis.anyIntersectionWith(u->getDisabledTypesToProcess()))
#else
			// TheSuperHackers @bugfix Stubbjax 15/03/2026 The disabled-types-to-process mask is now exclusive.
			// Previously, if the disabled mask had any bits in common with the disabled-types-to-process mask,
			// the update would be processed. Now, if any *other* bits are set in the disabled mask, the update
			// is no longer processed.
			if (u->getDisabledTypesToProcess().testForAll(dis))
#endif
			{
				engine::profiling::Scope profile_scope_3551("GameLogic_update_normal");

				m_curUpdateModule = u;

					UpdateSleepTime sleep = u->update();
					engine::debug::invariant((sleep == UPDATE_SLEEP_NONE), "sleep == UPDATE_SLEEP_NONE", __FILE__, __LINE__, "you must return SLEEPNONE from all nonsleepy modules");

				m_curUpdateModule = nullptr;
			}
		}
	}
#endif

	{
		while (!m_scheduledUpdates->queue.empty())
		{
			UpdateModulePtr u = peekSleepyUpdate();

			if (!u)
			{
				engine::debug::invariant(false, "debug failure", __FILE__, __LINE__, "Null update. should not happen.");
				continue;
			}

			// we're done, everyone else is sleeping.
			// break from the loop BEFORE we pop this item off.
			if (u->friend_getNextCallFrame() > now)
			{
				break;
			}

			UpdateSleepTime sleepLen = UPDATE_SLEEP_NONE;	// default, if it is disabled.

			DisabledMaskType dis = u->friend_getObject()->getDisabledFlags();
#if RETAIL_COMPATIBLE_CRC
			if (!dis.any() || dis.anyIntersectionWith(u->getDisabledTypesToProcess()))
#else
			// TheSuperHackers @bugfix Stubbjax 15/03/2026 The disabled-types-to-process mask is now exclusive.
			// Previously, if the disabled mask had any bits in common with the disabled-types-to-process mask,
			// the update would be processed. Now, if any *other* bits are set in the disabled mask, the update
			// is no longer processed.
			if (u->getDisabledTypesToProcess().testForAll(dis))
#endif
			{
				engine::profiling::Scope profile_scope_3599("GameLogic_update_sleepy");

				//engine::debug::log_info("calling update %08lx (%d %d)...",update,update->friend_getNextCallFrame(),update->friend_getNextCallPhase());
				m_curUpdateModule = u;

				{
					auto timing=capture.measure("logic.object",now,
						unsigned(u->friend_getObject()->getID()),unsigned(u->getModuleNameKey()));
					sleepLen = u->update();
				}
				engine::debug::invariant((sleepLen > 0), "sleepLen > 0", __FILE__, __LINE__, "you may not return 0 from update");
				if (sleepLen < 1)
					sleepLen = UPDATE_SLEEP_NONE;

				m_curUpdateModule = nullptr;

			}

			// else defer it till next frame and re-push it
			u->friend_setNextCallFrame(now + sleepLen);
			m_scheduledUpdates->queue.reschedule(u->friend_getIndexInLogic(),u,u->friend_getPriority());
		}
	}

	validateSleepyUpdate();

	// update the Artificial Intelligence system
	{
		auto timing=capture.measure("logic.ai",now);
		TheAI->update();
	}

	// production updates
	{
		auto timing=capture.measure("logic.production",now);
		TheBuildAssistant->update();
	}

	// update partition info
	{
		auto timing=capture.measure("logic.partition",now);
		ThePartitionManager->update();
	}

	//
	// End of frame clean-up
	//

	// destroy all pending objects
	processDestroyList();

	// reset the command list, destroying all messages
	TheCommandList->reset();

	TheWeaponStore->update();
	TheLocomotorStore->update();
	TheVictoryConditions->update();

	{
		//Handle disabled statii (and re-enable objects once frame matches)
		for( Object *obj = m_objList; obj; obj = obj->getNextObject() )
		{
			if( obj->isDisabled() )
			{
				obj->checkDisabledStatus();
			}
		}
	}





	// increment world time
	if (!m_startNewGame)
	{
		m_frame++;
		m_hasUpdated = TRUE;
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::preUpdate()
{
	m_hasUpdated = FALSE;

	if (m_pauseFrame == m_frame && m_pauseFrame != 0)
	{
		m_pauseFrame = 0;
		Bool pause = TRUE;
		Bool pauseMusic = FALSE;
		Bool pauseInput = FALSE;
		setGamePaused(pause, pauseMusic, pauseInput);
	}
}

// ------------------------------------------------------------------------------------------------
/** Return the first object in the world list */
// ------------------------------------------------------------------------------------------------
Object *GameLogic::getFirstObject()
{
	return m_objList;
}

// ------------------------------------------------------------------------------------------------
/** Return a new unique object id. */
// ------------------------------------------------------------------------------------------------
ObjectID GameLogic::allocateObjectID()
{
	/// @todo Find unused value in current object set
	ObjectID ret = m_nextObjID;
	m_nextObjID = (ObjectID)((UnsignedInt)m_nextObjID + 1);
	return ret;
}

// ------------------------------------------------------------------------------------------------
/** Add object ID to the lookup table */
// ------------------------------------------------------------------------------------------------
void GameLogic::addObjectToLookupTable( Object *obj )
{

	// sanity
	if( obj == nullptr )
		return;

	// add to lookup
//	m_objHash[ obj->getID() ] = obj;
	ObjectID newID = obj->getID();
	while( newID >= m_objVector.size() ) // Fail case is hella rare, so faster to double up on size() call
		m_objVector.resize(m_objVector.size() * 2, nullptr);

	m_objVector[ newID ] = obj;

}

// ------------------------------------------------------------------------------------------------
/** Remove object from the ID lookup table */
// ------------------------------------------------------------------------------------------------
void GameLogic::removeObjectFromLookupTable( Object *obj )
{

	// sanity
	// TheSuperHackers @fix Mauller/Xezon 24/04/2025 Prevent out of range access to vector lookup table
	if( obj == nullptr || static_cast<size_t>(obj->getID()) >= m_objVector.size() )
		return;

	// remove from lookup table
//	m_objHash.erase( obj->getID() );
	m_objVector[ obj->getID() ] = nullptr;

}

// ------------------------------------------------------------------------------------------------
/** Given an object, register it with the GameLogic and give it a unique ID. */
// ------------------------------------------------------------------------------------------------
void GameLogic::registerObject( Object *obj )
{

	// add the object to the global list
	obj->prependToList(&m_objList);

	// add object to lookup table
	addObjectToLookupTable( obj );

	UnsignedInt now = getFrame();
	if (now == 0)
		now = 1;
	for (BehaviorModule** b = obj->getBehaviorModules(); *b; ++b)
	{
#ifdef DIRECT_UPDATEMODULE_ACCESS
		// evil, but necessary at this point. (srj)
		UpdateModulePtr u = (UpdateModulePtr)((*b)->getUpdate());
#else
		UpdateModulePtr u = (*b)->getUpdate();
#endif
		if (!u)
			continue;

		UnsignedInt when = u->friend_getNextCallFrame();
#ifdef ALLOW_NONSLEEPY_UPDATES
		if (when == 0)
		{
			// zero is the magic value for "never sleeps"
			m_normalUpdates.push_back(u);
		}
		else
#else
		// note that 'when' can be zero here for any update module
		// that didn't bother to call setWakeFrame() in its ctor.
		// this is legal.
		if (when == 0)
			u->friend_setNextCallFrame(now);
#endif
		{
			engine::debug::invariant((u->friend_getNextCallFrame() >= now), "u->friend_getNextCallFrame() >= now", __FILE__, __LINE__, "you may not specify a zero initial sleep time for sleepy modules (%d %d)",u->friend_getNextCallFrame(),now);
			pushSleepyUpdate(u);
		}
	}

}

// ------------------------------------------------------------------------------------------------
/** create an object based on the thing template, the reason that this is
	* here and not in the ThingFactory is so that we can mirror what the
	* creation process is on the client side because clients have specific
	* device dependent drawables (such as a W3DDrawable derived from Drawable).
	* here we will allocate an object of the correct type based on thing
	* template properties
	*
	* if we want to have the thing manager actually contain the pools of
	* object and drawable storage it seems OK to have it be friends with the
	* GameLogic/Client for those purposes, or we could put the allocation pools
	* in the GameLogic and GameClient themselves */
// ------------------------------------------------------------------------------------------------
Object *GameLogic::friend_createObject( const ThingTemplate *thing, const ObjectStatusMaskType &statusBits, Team *team )
{
	Object *obj;

	obj = newInstance(Object)( thing, statusBits, team );

	return obj;
}

// ------------------------------------------------------------------------------------------------
/** Mark the object as destroyed, and place on list for deletion at the end of the next update.
 * This is the only interface to destroy objects - objects cannot be directly deleted. */
// ------------------------------------------------------------------------------------------------
void GameLogic::destroyObject( Object *obj )
{
	engine::debug::invariant((obj != nullptr), "obj != nullptr", __FILE__, __LINE__, "destroying null object");

	// if already flagged for destruction, ignore
	if (!obj || obj->isDestroyed())
		return;

	// run the object onDestroy event if provided
	for (BehaviorModule** m = obj->getBehaviorModules(); *m; ++m)
	{
		DestroyModuleInterface* destroy = (*m)->getDestroy();
		if (destroy)
			destroy->onDestroy();
	}

	// mark object as destroyed
	obj->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_DESTROYED ) );

	// We desperately need to stop here, or else the destructor of the statemachine will try to do
	// stopping logic, which uses virtual functions and deleted modules, which will crash us.
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if( ai )
	{
		ai->setLocomotorGoalNone();
		ai->destroyPath();
	}

	// add to end of destruction list, in case something is being destroyed and trying to destroy subobjects
	m_objectsToDestroy.push_back(obj);

	// run any on destroy logic internal to the object
	obj->onDestroy();

	// remove wall pieces from the pathfinder
	if( obj->isKindOf( KINDOF_WALK_ON_TOP_OF_WALL ) )
		TheAI->pathfinder()->removeWallPiece( obj );

	//Clean up special power shortcut bars
	if( obj->hasAnySpecialPower() )
	{
		if( obj->isLocallyControlled() )
		{
			TheControlBar->markUIDirty();
		}
	}


}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
Bool inCRCGen = FALSE;
UnsignedInt GameLogic::getCRC( Int mode, AsciiString deepCRCFileName )
{
	if (mode != CRC_RECALC)
		return m_CRC;

	setFPMode();

	LatchRestore<Bool> latch(inCRCGen, !isInGameLogicUpdate());

	XferCRC *xferCRC;
	AsciiString marker;
	if (deepCRCFileName.isNotEmpty())
	{
		xferCRC = NEW XferDeepCRC;
		xferCRC->open(deepCRCFileName.str());
	}
	else
	{
		AsciiString crcName;
#ifdef DEBUG_CRC
		// TheSuperHackers @info helmutbuhler 04/09/2025
		// This allows you to save the binary data that is involved in the crc calculation
		// to a binary file per frame.
		// This was apparently used early in development and isn't that useful, because diffing
		// that binary data is very difficult. The CRC logging is much easier to diff and also more
		// granular than this because it can capture changes between two frames.
		if (isInGameLogicUpdate() && g_keepCRCSaves && m_frame < 5)
		{
			xferCRC = NEW XferDeepCRC;
			crcName.format("logicFrame%d.crc", (m_frame%5));
		}
		else
#endif // DEBUG_CRC
		{
			xferCRC = NEW XferCRC;
			crcName = "lightCRC";
		}
		xferCRC->open(crcName);
	}

	// calculate CRCs
	Object *obj;
	engine::debug::invariant((this == TheGameLogic), "this == TheGameLogic", __FILE__, __LINE__, "Not in GameLogic");
	if (isInGameLogicUpdate())
	{
		engine::debug::log_info("CRC at start of frame %d is 0x%8.8X", m_frame, xferCRC->getCRC());
	}

	marker = "MARKER:Objects";
	xferCRC->xferAsciiString(&marker);
	for( obj = m_objList; obj; obj=obj->getNextObject() )
	{
		xferCRC->xferSnapshot( obj );
	}
	UnsignedInt seed = GetGameLogicRandomSeedCRC();
	if (isInGameLogicUpdate())
	{
		engine::debug::log_info("CRC after objects for frame %d is 0x%8.8X", m_frame, xferCRC->getCRC());
	}

	if (isInGameLogicUpdate())
	{
		engine::debug::log_info("RandomSeed: %d", seed);
	}
	if (xferCRC->getXferMode() == XFER_CRC)
	{
		xferCRC->xferUnsignedInt( &seed );
	}
	marker = "MARKER:ThePartitionManager";
	xferCRC->xferAsciiString(&marker);
	xferCRC->xferSnapshot( ThePartitionManager );
	if (isInGameLogicUpdate())
	{
		engine::debug::log_info("CRC after partition manager for frame %d is 0x%8.8X", m_frame, xferCRC->getCRC());
	}

#ifdef DEBUG_CRC
	if ((g_crcModuleDataFromClient && !isInGameLogicUpdate()) ||
		(g_crcModuleDataFromLogic && isInGameLogicUpdate()))
	{
		marker = "MARKER:TheModuleFactory";
		xferCRC->xferAsciiString(&marker);
		xferCRC->xferSnapshot( TheModuleFactory );
		if (isInGameLogicUpdate())
		{
			engine::debug::log_info("CRC after module factory for frame %d is 0x%8.8X", m_frame, xferCRC->getCRC());
		}
	}
#endif // DEBUG_CRC

	marker = "MARKER:ThePlayerList";
	xferCRC->xferAsciiString(&marker);
	xferCRC->xferSnapshot( ThePlayerList );
	if (isInGameLogicUpdate())
	{
		engine::debug::log_info("CRC after PlayerList for frame %d is 0x%8.8X", m_frame, xferCRC->getCRC());
	}

	marker = "MARKER:TheAI";
	xferCRC->xferAsciiString(&marker);
	xferCRC->xferSnapshot( TheAI );
	if (isInGameLogicUpdate())
	{
		engine::debug::log_info("CRC after AI for frame %d is 0x%8.8X", m_frame, xferCRC->getCRC());
	}

	if (xferCRC->getXferMode() == XFER_SAVE)
	{
		marker = "MARKER:GameSave";
		xferCRC->xferAsciiString(&marker);
		TheGameState->friend_xferSaveDataForCRC(xferCRC, SNAPSHOT_DEEPCRC_LOGICONLY);
	}

	xferCRC->close();

	UnsignedInt theCRC = xferCRC->getCRC();

	delete xferCRC;
	xferCRC = nullptr;

	if (isInGameLogicUpdate())
	{
		engine::debug::log_info("CRC for frame %d is 0x%8.8X", m_frame, theCRC);
	}
	return theCRC;
}

// ------------------------------------------------------------------------------------------------
void GameLogic::exitGame()
{
	// TheSuperHackers @fix The logic update must not be halted to process the game exit message.
	setGamePaused(FALSE);
	TheScriptEngine->forceUnfreezeTime();
	TheScriptEngine->doUnfreezeTime();

	TheMessageStream->appendMessage(GameMessage::MSG_CLEAR_GAME_DATA);

#ifdef true
	AsciiString message;
	message.format("GameEnd: %s", TheGlobalData->m_mapName.str());
	engine::profiling::message(message.str());
#endif
}

// ------------------------------------------------------------------------------------------------

void GameLogic::quit(Bool toDesktop)
{
	const Bool isNotLoading = (!isLoadingMap() && !isLoadingSave());

	if (isInGame())
	{
		if (isInInteractiveGame())
		{
			if (canOpenQuitMenu())
			{
				ToggleQuitMenu();
				return;
			}
			
			if (isInMultiplayerGame() && !isInSkirmishGame() && TheGameInfo && !TheGameInfo->isSandbox())
			{
				GameMessage *msg = TheMessageStream->appendMessage(GameMessage::MSG_SELF_DESTRUCT);
				msg->appendBooleanArgument(TRUE);
			}
		}

		if (TheRecorder && TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
		{
			TheRecorder->stopRecording();
		}

		setGamePaused(FALSE);

		if (TheScriptEngine && isNotLoading)
		{
			TheScriptEngine->forceUnfreezeTime();
			TheScriptEngine->doUnfreezeTime();
		}

		if (toDesktop)
		{
			if (isInMultiplayerGame())
			{
				m_quitToDesktopAfterMatch = TRUE;
				if (isNotLoading)
				{
					exitGame();
				}
			}
			else
			{
				if (isNotLoading)
				{
					clearGameData();
				}
			}
		}
		else
		{
			exitGame();
		}
	}

	if (toDesktop)
	{
		if (!isInMultiplayerGame())
		{
			TheGameEngine->setQuitting(TRUE);
		}
	}

	if (TheInGameUI)
	{
		TheInGameUI->setClientQuiet(TRUE);
	}
}

// ------------------------------------------------------------------------------------------------
/** A new GameLogic object has been constructed, therefore create
 * a corresponding drawable and bind them together. */
// ------------------------------------------------------------------------------------------------
void GameLogic::sendObjectCreated( Object *obj )
{
	Drawable *draw = TheThingFactory->newDrawable(obj->getTemplate());

/// @todo COLIN ... shouldn't we have a check here for existing drawable!!!!!

	// bind drawable to object and object to drawable
	bindObjectAndDrawable(obj, draw);

}

// ------------------------------------------------------------------------------------------------
void GameLogic::bindObjectAndDrawable(Object* obj, Drawable* draw)
{
	draw->friend_bindToObject( obj );
	obj->friend_bindToDrawable( draw );
}

// ------------------------------------------------------------------------------------------------
/** Send notification of object destruction. */
// ------------------------------------------------------------------------------------------------
void GameLogic::sendObjectDestroyed( Object *obj )
{
	// Because this implementation is a bridge between the Logic and Interface,
	// we must take extra care to handle such cases as when the system it
	// shutting down.
	if(TheGameClient == nullptr)
		return;

	// destroy the drawable
	Drawable *draw = obj->getDrawable();
	if(draw)
	{
		TheGameClient->destroyDrawable( draw );
	}

	// erase the binding of the drawable to this object
	obj->friend_bindToDrawable( nullptr );

}

// ------------------------------------------------------------------------------------------------
/** Return if the game is paused or not */
// ------------------------------------------------------------------------------------------------
Bool GameLogic::isGamePaused()
{
	return m_gamePaused;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::setGamePausedInFrame( UnsignedInt frame, Bool disableLogicTimeScale )
{
	if (frame >= m_frame)
	{
		m_pauseFrame = frame;

		if (disableLogicTimeScale)
		{
			m_logicTimeScaleEnabledMemory = TheFramePacer->isLogicTimeScaleEnabled();
			TheFramePacer->enableLogicTimeScale(FALSE);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::setGamePaused( Bool paused, Bool pauseMusic, Bool pauseInput )
{
	// We need to ignore an unpause called when we are unpaused or else:
	// Mouse is hidden for some reason (script or something)
	// GamePaused
	// Remember that we were hidden
	// Show mouse
	// GameUnpaused
	// Set mouse the way it was

	// Time passes, mouse is unhidden

	// GameUnpaused
	// Set mouse the way it "was" <--- Was counting on right answer being set in Pause.

	pauseGameLogic(paused);
	pauseGameSound(paused);
	pauseGameMusic(paused && pauseMusic);
	pauseGameInput(paused && pauseInput);

	updateDisplayBusyState();
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::pauseGameLogic(Bool paused)
{
	m_gamePaused = paused;

	if (!paused && m_logicTimeScaleEnabledMemory)
	{
		m_logicTimeScaleEnabledMemory = FALSE;
		TheFramePacer->enableLogicTimeScale(TRUE);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::pauseGameSound(Bool paused)
{
	if(m_pauseSound == paused)
		return;

	m_pauseSound = paused;

	if(paused)
	{
		TheAudio->pauseAudio((AudioAffect)(AudioAffect_All & ~AudioAffect_Music));

#if 0 // Kris added this code some time ago. I'm not sure why -- the pauseAudio should stop the
      // ambients by itself. Everything seems to work fine without it and it's messing up my
      // custom ambient code. Hopefully he can explain it to me, but until he gets back, I'm
      // disabling it. -Ian

		//Stop all ambient sounds!
		Drawable *drawable = TheGameClient->getDrawableList();
		while( drawable )
		{
			drawable->stopAmbientSound();
			drawable = drawable->getNextDrawable();
		}
#endif
	}
	else
	{
		TheAudio->resumeAudio((AudioAffect)(AudioAffect_All & ~AudioAffect_Music));

#if 0
		//Start all ambient sounds!
		Drawable *drawable = TheGameClient->getDrawableList();
		while( drawable )
		{
			drawable->startAmbientSound();
			drawable = drawable->getNextDrawable();
		}
#endif
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::pauseGameMusic(Bool paused)
{
	if(m_pauseMusic == paused)
		return;

	m_pauseMusic = paused;

	if(paused)
	{
		TheAudio->pauseAudio(AudioAffect_Music);
	}
	else
	{
		TheAudio->resumeAudio(AudioAffect_Music);
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::pauseGameInput(Bool paused)
{
	if(m_pauseInput == paused)
		return;

	m_pauseInput = paused;

	TheMouse->onGamePaused(paused);

	if(paused)
	{
		// remember the state of the mouse/input so we can return to the same state once we "unpause"
		m_inputEnabledMemory = TheInGameUI->getInputEnabled();
		m_mouseVisibleMemory = TheMouse->getVisibility();

		// Make sure the mouse is visible and the cursor is an arrow
		TheMouse->setVisibility(TRUE);
		TheMouse->setCursor( Mouse::ARROW );

		// if Input is enabled, disable it
		if(m_inputEnabledMemory)
		{
			TheInGameUI->setInputEnabled(FALSE);
		}
	}
	else
	{
		// set the mouse/input states to what they were before we paused.
		TheMouse->setVisibility(m_mouseVisibleMemory);
		if(m_inputEnabledMemory)
		{
			TheInGameUI->setInputEnabled(TRUE);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::processProgress(Int playerId, Int percentage)
{
	if(m_loadScreen)
		m_loadScreen->processProgress(playerId, percentage);
	lastHeardFrom(playerId);
}

// ------------------------------------------------------------------------------------------------
/** Whenever we get a progress complete packet for a Net Game,
	* Set a flag that that player is ready */
// ------------------------------------------------------------------------------------------------
void GameLogic::processProgressComplete(Int playerId)
{
	if(playerId < 0 || playerId >= MAX_SLOTS)
	{
		engine::debug::invariant(false, "debug failure", __FILE__, __LINE__, "GameLogic::processProgressComplete, Invalid playerid was passed in %d", playerId);
		return;
	}
	if(m_progressComplete[playerId] == TRUE)
	{
		engine::debug::log_info("GameLogic::processProgressComplete, playerId %d is marked TRUE already yet we're trying to mark him as true again", playerId);
		return;
	}
	engine::debug::log_info("Progress Complete for Player %d", playerId);
	m_progressComplete[playerId] = TRUE;
	lastHeardFrom(playerId);
}

// ------------------------------------------------------------------------------------------------
/// @TODO: Add check to account for timeouts
// ------------------------------------------------------------------------------------------------
Bool GameLogic::isProgressComplete()
{
	//If we're not in a network game, always return true
	if(!isInMultiplayerGame() || !TheNetwork || m_forceGameStartByTimeOut)
		return TRUE;

	// Only loop on the Number of players we got in here
	for(Int i =0; i < MAX_SLOTS; ++i)
	{
		if(!m_progressComplete[i])
			return FALSE;
	}
	return TRUE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::lastHeardFrom( Int playerId )
{
	if( playerId < 0 || playerId >= MAX_SLOTS)
		return;
	m_progressCompleteTimeout[playerId] = static_cast<UnsignedInt>(m_clock.monotonic_nanoseconds() / 1000000u);
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::testTimeOut()
{
	// if everyone is loaded, lets just load the game like normal.
	if(isProgressComplete())
		return;

	Int curTime = static_cast<UnsignedInt>(m_clock.monotonic_nanoseconds() / 1000000u);
	// Loop and test everyone in our game.
	for(Int i =0; i < MAX_SLOTS; ++i)
	{
		// If they've completed their progress, ignore them
		if(m_progressComplete[i])
			continue;
		if(	m_progressCompleteTimeout[i] + PROGRESS_COMPLETE_TIMEOUT > curTime )
			return;
	}
	// if we made it this far, that means everyone has timed out.
	m_forceGameStartByTimeOut = TRUE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::timeOutGameStart()
{
	engine::debug::log_info("We got the Force TimeOut Start Message");
	m_forceGameStartByTimeOut = TRUE;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::initTimeOutValues()
{
	if (!TheNetwork)
		return;
	for(Int i = 0; i < TheNetwork->getNumPlayers(); ++i)
	{
		m_progressCompleteTimeout[i] = static_cast<UnsignedInt>(m_clock.monotonic_nanoseconds() / 1000000u);
	}
}

// ------------------------------------------------------------------------------------------------
/** returns the total number of objects in the world */
// ------------------------------------------------------------------------------------------------
UnsignedInt GameLogic::getObjectCount()
{
	UnsignedInt totalObjects = 0;
	Object *obj;
	for( obj = getFirstObject(); obj; obj = obj->getNextObject() )
	{
		++totalObjects;
	}
	return totalObjects;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
GhostObjectManager *GameLogic::createGhostObjectManager(bool dummy)
{
	if (dummy)
		return NEW GhostObjectManagerDummy;
	return NEW GhostObjectManager;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
TerrainLogic *GameLogic::createTerrainLogic()
{
	return NEW TerrainLogic;
}

// ------------------------------------------------------------------------------------------------
void GameLogic::setBuildableStatusOverride(const ThingTemplate* tt, BuildableStatus bs)
{
	if (tt)
	{
		m_thingTemplateBuildableOverrides[tt->getName()] = bs;
	}
}

// ------------------------------------------------------------------------------------------------
Bool GameLogic::findBuildableStatusOverride(const ThingTemplate* tt, BuildableStatus& bs) const
{
	if (tt)
	{
		BuildableMap::const_iterator it = m_thingTemplateBuildableOverrides.find(tt->getName());
		if (it != m_thingTemplateBuildableOverrides.end())
		{
			bs = it->second;
			return true;
		}
	}
	return false;
}

// ------------------------------------------------------------------------------------------------
void GameLogic::setControlBarOverride(const AsciiString& commandSetName, Int slot, ConstCommandButtonPtr commandButton)
{
	char buf[256];
	buf[0] = '0' + slot;
	strcpy(&buf[1], commandSetName.str());
	m_controlBarOverrides[buf] = commandButton;
}

// ------------------------------------------------------------------------------------------------
Bool GameLogic::findControlBarOverride(const AsciiString& commandSetName, Int slot, ConstCommandButtonPtr& commandButton) const
{
	char buf[256];
	buf[0] = '0' + slot;
	strcpy(&buf[1], commandSetName.str());

	ControlBarOverrideMap::const_iterator it = m_controlBarOverrides.find(buf);
	if (it != m_controlBarOverrides.end())
	{
		commandButton = it->second;	// could be null.
		return true;
	}

	// leave commandButton unmodified.
	return false;
}


// ------------------------------------------------------------------------------------------------
/** Light CRC */
// ------------------------------------------------------------------------------------------------
void GameLogic::crc( Xfer *xfer )
{

}

// ------------------------------------------------------------------------------------------------
/** Given a string name, find the object TOC entry (if any) associated with it */
// ------------------------------------------------------------------------------------------------
GameLogic::ObjectTOCEntry *GameLogic::findTOCEntryByName( AsciiString name )
{

	for( ObjectTOCListIterator it = m_objectTOC.begin(); it != m_objectTOC.end(); ++it )
		if( (*it).name == name )
			return &(*it);

	return nullptr;

}

// ------------------------------------------------------------------------------------------------
/** Given a object TOC identifier, find the object TOC if any */
// ------------------------------------------------------------------------------------------------
GameLogic::ObjectTOCEntry *GameLogic::findTOCEntryById( UnsignedShort id )
{

	for( ObjectTOCListIterator it = m_objectTOC.begin(); it != m_objectTOC.end(); ++it )
		if( (*it).id == id )
			return &(*it);

	return nullptr;

}

// ------------------------------------------------------------------------------------------------
/** Add an object TOC entry */
// ------------------------------------------------------------------------------------------------
void GameLogic::addTOCEntry( AsciiString name, UnsignedShort id )
{

	ObjectTOCEntry tocEntry;
	tocEntry.name = name;
	tocEntry.id = id;
	m_objectTOC.push_back( tocEntry );

}

// ------------------------------------------------------------------------------------------------
/** Xfer object table of contents */
// ------------------------------------------------------------------------------------------------
void GameLogic::xferObjectTOC( Xfer *xfer )
{

	// version
	const XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// clear our current table of contents
	m_objectTOC.clear();

	// xfer the table
	UnsignedInt tocCount = 0;
	if( xfer->getXferMode() == XFER_SAVE )
	{
		AsciiString templateName;

		// generate a new TOC based on the objects that are in the map
		for( Object *obj = getFirstObject(); obj; obj = obj->getNextObject() )
		{

			// get the name we're working with
			templateName = obj->getTemplate()->getName();

			// if is this object name already in the TOC, skip it
			if( findTOCEntryByName( templateName ) != nullptr )
				continue;

			// add this entry to the TOC
			addTOCEntry( obj->getTemplate()->getName(), ++tocCount );

		}

		// xfer entries in the TOC
		xfer->xferUnsignedInt( &tocCount );

		// xfer each TOC entry
		ObjectTOCListIterator it;
		ObjectTOCEntry *tocEntry;
		for( it = m_objectTOC.begin(); it != m_objectTOC.end(); ++it )
		{

			// get this toc entry
			tocEntry = &(*it);

			// xfer the name
			xfer->xferAsciiString( &tocEntry->name );

			// xfer the paired id
			xfer->xferUnsignedShort( &tocEntry->id );

		}

	}
	else
	{
		AsciiString templateName;
		UnsignedShort id;

		// how many entries are we going to read
		xfer->xferUnsignedInt( &tocCount );

		// read all the entries
		for( UnsignedInt i = 0; i < tocCount; ++i )
		{

			// read the name
			xfer->xferAsciiString( &templateName );

			// read the id
			xfer->xferUnsignedShort( &id );

			// add this to the TOC
			addTOCEntry( templateName, id );

		}

	}

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void GameLogic::prepareLogicForObjectLoad()
{

	//
	// this is a band-aid :(
	// when loading from a map file, objects were created for the bridges, towers, walls etc.
	// since we're going to load that data from the save game file we will destroy any of
	// those bridge objects and tower objects so that we can create them from the save data
	//
	Object *obj, *next;
	for( obj = getFirstObject(); obj; obj = next )
	{

		// get next
		next = obj->getNextObject();

		// is this a bridge object?
		if( obj->isKindOf( KINDOF_BRIDGE ) )
		{
			Bridge *bridge = TheTerrainLogic->findBridgeAt( obj->getPosition() );

			// sanity
			engine::debug::invariant((bridge), "bridge", __FILE__, __LINE__, "GameLogic::prepareLogicForObjectLoad - Unable to find bridge" );

			// get the old object that is in the bridge info
			const BridgeInfo *bridgeInfo = bridge->peekBridgeInfo();
			Object *oldObject = findObjectByID( bridgeInfo->bridgeObjectID );
			engine::debug::invariant((oldObject), "oldObject", __FILE__, __LINE__, "GameLogic::prepareLogicForObjectLoad - Unable to find old bridge object");
			engine::debug::invariant((oldObject == obj), "oldObject == obj", __FILE__, __LINE__, "GameLogic::prepareLogicForObjectLoad - obj != oldObject");

			//
			// destroy the 4 towers that are attached to this old object (they will be loaded from
			// the save game file
			//
			Object *oldTower;
			for( Int i = 0; i < BRIDGE_MAX_TOWERS; ++i )
			{

				oldTower = findObjectByID( bridgeInfo->towerObjectID[ i ] );
				if (oldTower) {
					destroyObject( oldTower );
				}

			}

			// destroy the old bridge object
			destroyObject( oldObject );

		}
		else if( obj->isKindOf( KINDOF_WALK_ON_TOP_OF_WALL ) )
		{

			// destroy walk on top of wall things too
			destroyObject( obj );

		}

	}

	// process the destruction of these objects immediately before we proceed with the load process
	processDestroyList();

	// there should be no objects anywhere
	engine::debug::invariant((getFirstObject() == nullptr), "getFirstObject() == nullptr", __FILE__, __LINE__, "GameLogic::prepareLogicForObjectLoad - There are still objects loaded in the engine, but it should be empty (Top is '%s')",
										 getFirstObject()->getTemplate()->getName().str());

}

// ------------------------------------------------------------------------------------------------
/** Load/Save game logic to xfer
	*	Version Info:
	* 1: Initial version
	* 2: Added m_isScoringEnabled flag (BGC)
	* 3: Added polygon triggers (CBD)
	* 4: Added block markers around object data, no version checking is done and therefore
	*		 this version breaks compatibility with previous versions. (CBD)
	* 5: Added xfering the BuildAssistant's sell list.
	* 9: Added m_rankPointsToAddAtGameStart, or else on a load game, your RestartGame button will forget your exp
  * 10: xfer m_superweaponRestriction
  * 11: TheSuperHackers @fix Save objects in reverse order so they load in correct order
	*/
// ------------------------------------------------------------------------------------------------
void GameLogic::xfer( Xfer *xfer )
{

	// version
#if RETAIL_COMPATIBLE_XFER_SAVE
	const XferVersion currentVersion = 10;
#else
	const XferVersion currentVersion = 11;
#endif
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// logic frame number
	xfer->xferUnsignedInt( &m_frame );

	//
	// note that we do not do the id counter here, we did it in the game state block because
	// it's important to do that part very early in the load process
	//
	// !!!DON'T DO THIS!!! ----> xfer->xferObjectID( &m_nextObjectID ); <---- !!!DON'T DO THIS!!!

	//
	// xfer a table of contents that contains thing template and identifier pairs.  this
	// table of contents is good for this save file only as unique numbers are only
	// generated and stored for the actual things that are on this map
	//
	xferObjectTOC( xfer );

	// when loading, we need to clean up bridges and get them ready for a load
	if( xfer->getXferMode() == XFER_LOAD )
		prepareLogicForObjectLoad();

	// object count
	UnsignedInt objectCount = getObjectCount();
	xfer->xferUnsignedInt( &objectCount );

	// object data
	Object *obj;
	ObjectTOCEntry *tocEntry;
	if( xfer->getXferMode() == XFER_SAVE )
	{
#if !RETAIL_COMPATIBLE_XFER_SAVE
		// TheSuperHackers @fix bobtista 07/03/2026 Save objects in reverse order (newest first)
		// so they load in the correct order (oldest objects at head of list).
		Object *lastObj = nullptr;
		for( obj = getFirstObject(); obj; obj = obj->getNextObject() )
			lastObj = obj;

		for( obj = lastObj; obj; obj = obj->getPrevObject() )
#else
		for( obj = getFirstObject(); obj; obj = obj->getNextObject() )
#endif
		{

			// get the object TOC entry for this template
			tocEntry = findTOCEntryByName( obj->getTemplate()->getName() );
			if( tocEntry == nullptr )
			{

				engine::debug::invariant(false, "debug failure", __FILE__, __LINE__,  "GameLogic::xfer - Object TOC entry not found for '%s'", obj->getTemplate()->getName().str() );
				throw SC_INVALID_DATA;

			}

			// transfer TOC id entry
			xfer->xferUnsignedShort( &tocEntry->id );

			// begin a block of data
			xfer->beginBlock();

			// write object data
			xfer->xferSnapshot( obj );

			// end a block of data
			xfer->endBlock();

		}

	}
	else
	{
		Team *defaultTeam = ThePlayerList->getNeutralPlayer()->getDefaultTeam();
		const ThingTemplate *thingTemplate;

		// read all objects
		Int objectDataSize;
		UnsignedShort tocID;
		ObjectTOCEntry *tocEntry;
		for( UnsignedInt i = 0; i < objectCount; ++i )
		{

			// read toc entry identifier
			xfer->xferUnsignedShort( &tocID );

			// find Object TOC entry with this identifier
			tocEntry = findTOCEntryById( tocID );
			if( tocEntry == nullptr )
			{

				engine::debug::invariant(false, "debug failure", __FILE__, __LINE__,  "GameLogic::xfer - No TOC entry match for id '%d'", tocID );
				throw SC_INVALID_DATA;

			}

			// a block of data has begun
			objectDataSize = xfer->beginBlock();

			// find matching thing template
			thingTemplate = TheThingFactory->findTemplate( tocEntry->name );
			if( thingTemplate == nullptr )
			{

				engine::debug::invariant(false, "debug failure", __FILE__, __LINE__,  "GameLogic::xfer - Unrecognized thing template name '%s', skipping.  ENGINEERS - Are you *sure* it's OK to be ignoring this object from the save file???  Think hard about it!",
											tocEntry->name.str() );
				xfer->skip( objectDataSize );
				continue;

			}

			// create new object
			obj = TheThingFactory->newObject( thingTemplate, defaultTeam );

			// xfer the rest of the object data
			xfer->xferSnapshot( obj );

			// end of block of data (not necessary in a load, but looks symettrically nice)
			xfer->endBlock();

			// special case for wall pieces, need to add them to the pathfinder
			if( obj->isKindOf( KINDOF_WALK_ON_TOP_OF_WALL ) )
				TheAI->pathfinder()->addWallPiece( obj );

		}

		// TheSuperHackers @fix bobtista 07/03/2026 Reverse object list after load.
		// Objects are prepended during creation, which reverses the saved order.
		// Version 11+ saves in reverse order so they load in the correct order.
		if ( version <= 10 )
		{
			Object *prev = nullptr;
			Object *current = m_objList;
			Object *next = nullptr;
			while ( current != nullptr )
			{
				next = current->getNextObject();
				current->friend_setNextObject( prev );
				current->friend_setPrevObject( next );
				prev = current;
				current = next;
			}
			m_objList = prev;
		}

	}

	// campaign info
	xfer->xferSnapshot( TheCampaignManager );

	// cave system info
	xfer->xferSnapshot( TheCaveSystem );

	// is scoring enabled
	if( version >= 2 )
		xfer->xferBool(&m_isScoringEnabled);

	// polygon triggers
	if( version >= 3 )
	{
		PolygonTrigger *poly;

		// count the number of polygon triggers we have
		UnsignedInt triggerCount = 0;
		for( poly = PolygonTrigger::getFirstPolygonTrigger(); poly; poly = poly->getNext() )
			triggerCount++;

		// sanity count
		UnsignedInt sanityTriggerCount = triggerCount;

		// xfer count
		xfer->xferUnsignedInt( &triggerCount );

		//
		// since the save game loaded should have exactly the same number of polygon triggers
		// in it that we did from loading the map this is just a sanity check here
		//
		if( sanityTriggerCount != triggerCount )
		{

			engine::debug::invariant(false, "debug failure", __FILE__, __LINE__,  "GameLogic::xfer - Polygon trigger count mismatch.  Save file has a count of '%d', but map had '%d' triggers",
										sanityTriggerCount, triggerCount );
			throw SC_INVALID_DATA;

		}

		// xfer each of the polygon triggers
		if( xfer->getXferMode() == XFER_SAVE )
		{
			Int triggerID;

			for( poly = PolygonTrigger::getFirstPolygonTrigger(); poly; poly = poly->getNext() )
			{

				// write polygon ID
				triggerID = poly->getID();
				xfer->xferInt( &triggerID );

				// xfer polygon data
				xfer->xferSnapshot( poly );

			}

		}
		else
		{
			Int triggerID;

			// loop through all triggers
			for( UnsignedInt i = 0; i < triggerCount; ++i )
			{

				// read ID
				xfer->xferInt( &triggerID );

				// find this polygon trigger
				poly = PolygonTrigger::getPolygonTriggerByID( triggerID );

				// sanity
				if( poly == nullptr )
				{

					engine::debug::invariant(false, "debug failure", __FILE__, __LINE__,  "GameLogic::xfer - Unable to find polygon trigger with id '%d'",
												triggerID );
					throw SC_INVALID_DATA;

				}

				// xfer polygon data
				xfer->xferSnapshot( poly );

			}

			//
			// force a recalculation of the pathfinding cause some of these polygon triggers
			// are water which can move during run-time, and therefore affecting the area
			// that objects can move on. Also, map objects need recalculation. jba.
			//
			TheAI->pathfinder()->newMap();

		}

	}

	// note that version=4 is the same as version=3
	if (version >= 5)
	{
		xfer->xferInt(&m_rankLevelLimit);
	}

	if (version>=6) {
		// We need the list of buildings in process of being sold.
		TheBuildAssistant->xferTheSellList(xfer);
	}

	if (version >= 7)
	{
		if( xfer->getXferMode() == XFER_SAVE )
		{
			for (BuildableMap::const_iterator it = m_thingTemplateBuildableOverrides.begin(); it != m_thingTemplateBuildableOverrides.end(); ++it )
			{
				AsciiString name = it->first;
				BuildableStatus bs = it->second;
				xfer->xferAsciiString(&name);
				xfer->xferUser(&bs, sizeof(bs));
			}
			AsciiString empty;
			xfer->xferAsciiString(&empty);
		}
		else if (xfer->getXferMode() == XFER_LOAD)
		{
			if (m_thingTemplateBuildableOverrides.empty() == false)
			{
				engine::debug::invariant(false, "debug failure", __FILE__, __LINE__,  "GameLogic::xfer - m_thingTemplateBuildableOverrides should be empty, but is not");
				throw SC_INVALID_DATA;
			}

			for (;;)
			{
				AsciiString name;
				xfer->xferAsciiString(&name);
				if (name.isEmpty())
					break;
				BuildableStatus bs;
				xfer->xferUser(&bs, sizeof(bs));
				m_thingTemplateBuildableOverrides[name] = bs;
			}
		}
	}

	if (version >= 8)
	{
		xfer->xferBool(&m_showBehindBuildingMarkers);
		xfer->xferBool(&m_drawIconUI);
		xfer->xferBool(&m_showDynamicLOD);
		xfer->xferInt(&m_scriptHulkMaxLifetimeOverride);

		if( xfer->getXferMode() == XFER_SAVE )
		{
			for (ControlBarOverrideMap::const_iterator it = m_controlBarOverrides.begin(); it != m_controlBarOverrides.end(); ++it )
			{
				AsciiString name = it->first;
				AsciiString value = it->second ? it->second->getName() : AsciiString::TheEmptyString;
				xfer->xferAsciiString(&name);
				xfer->xferAsciiString(&value);
			}
			AsciiString empty;
			xfer->xferAsciiString(&empty);
		}
		else if (xfer->getXferMode() == XFER_LOAD)
		{
			if (m_controlBarOverrides.empty() == false)
			{
				engine::debug::invariant(false, "debug failure", __FILE__, __LINE__,  "GameLogic::xfer - m_controlBarOverrides should be empty, but is not");
				throw SC_INVALID_DATA;
			}

			for (;;)
			{
				AsciiString name;
				xfer->xferAsciiString(&name);
				if (name.isEmpty())
					break;
				AsciiString value;
				xfer->xferAsciiString(&value);
				ConstCommandButtonPtr button = nullptr;
				if (value.isNotEmpty())
				{
					button = TheControlBar->findCommandButton(value);
					engine::debug::invariant((button != nullptr), "button != nullptr", __FILE__, __LINE__, "Could not find button %s",value.str());
				}
				m_controlBarOverrides[name] = button;
			}
		}
	}

	if (version >= 9)
	{
		xfer->xferInt(&m_rankPointsToAddAtGameStart);
	}

  if ( version >= 10 )
  {
    xfer->xferUnsignedShort( &m_superweaponRestriction );
  }
  else if ( xfer->getXferMode() == XFER_LOAD )
  {
    m_superweaponRestriction = 0;
  }
}

// ------------------------------------------------------------------------------------------------
/** Load post process entry point */
// ------------------------------------------------------------------------------------------------
void GameLogic::loadPostProcess()
{

	//
	// the act of loading objects can (theoretically) as a side effect create other objects,
	// our m_nextObjID that we maintain to give objects unique ID is also continually
	// climbing higher and higher due to us allocating objects during load (even though
	// those objects have their ids overwritten with data from the file.  To prevent the
	// m_nextObjID from getting un-necessarily high we will set it to the next available
	// id from the objects that are now in the world and actually in use
	//
	m_nextObjID = INVALID_ID;
	Object *obj;
	for( obj = getFirstObject(); obj; obj = obj->getNextObject() )
		if( obj->getID() >= m_nextObjID )
			m_nextObjID = (ObjectID)((UnsignedInt)obj->getID() + 1);

	// blow away the sleepy update and normal update module lists
	m_scheduledUpdates->queue.forEach([](auto,UpdateModulePtr module) { module->friend_setIndexInLogic(-1); });
	m_scheduledUpdates->queue.clear();
#ifdef ALLOW_NONSLEEPY_UPDATES
	m_normalUpdates.clear();
#else
	UnsignedInt now = getFrame();
	if (now == 0)
		now = 1;
#endif

	// go through all objects, examine each update module and put it on the appropriate update list
	for( obj = getFirstObject(); obj; obj = obj->getNextObject() )
	{

		// get the update list of modules for this object
		for( BehaviorModule** b = obj->getBehaviorModules(); *b; ++b )
		{
#ifdef DIRECT_UPDATEMODULE_ACCESS
			// evil, but necessary at this point. (srj)
			UpdateModulePtr u = (UpdateModulePtr)((*b)->getUpdate());
#else
			UpdateModulePtr u = (*b)->getUpdate();
#endif
			if (!u)
				continue;

			engine::debug::invariant((u->friend_getIndexInLogic() == -1), "u->friend_getIndexInLogic() == -1", __FILE__, __LINE__, "Hmm, expected index to be -1 here");

			// check each update module
			UnsignedInt when = u->friend_getNextCallFrame();
#ifdef ALLOW_NONSLEEPY_UPDATES
			if( when == 0 )
			{
				// zero if the magic value for "never sleeps"
				m_normalUpdates.push_back(u);
			}
			else
#else
			// note that 'when' will only be zero for legacy save files.
			if (when == 0)
				u->friend_setNextCallFrame(now);
#endif
			{
				pushSleepyUpdate(u);
			}

		}

	}

	validateSleepyUpdate();

}
