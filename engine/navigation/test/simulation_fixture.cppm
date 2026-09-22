module;

#include <filesystem>
#include <cmath>
#include <chrono>
#include <vector>
#include <array>
#include <cstdint>
#include <bit>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <thread>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <optional>
#include <limits>
#include <cstdio>
#include "PreRTS.h"
#include "Common/CommandLine.h"
#include "Common/CriticalSection.h"
#include "Common/FramePacer.h"
#include "Common/GlobalData.h"
#include "Common/PlayerTemplate.h"
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/ThingFactory.h"
#include "Common/version.h"
#include "BuildVersion.h"
#include "GeneratedVersion.h"
#include "GameNetwork/GameInfo.h"
#include "Common/GameState.h"
#include "GameClient/Drawable.h"
#include "GameLogic/AI.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/Module/SupplyTruckAIUpdate.h"
#include "GameLogic/Module/SupplyWarehouseDockUpdate.h"
#include "Common/NameKeyGenerator.h"
#include "Common/XferSave.h"
#include "Common/XferLoad.h"
#include "Common/XferCRC.h"
#include "GameLogic/Module/CreateModule.h"
#include "engine/navigation/pathfinder_api.h"
#include "SDL3Device/Common/SDL3GameEngine.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "GameClient/TerrainVisual.h"

export module engine.navigation.simulation_fixture;
import engine.debug;
import engine.navigation.pathfinder;
import engine.navigation.dynamic_request_queue;
import engine.navigation.scheduling.continuation;
import engine.navigation.movement.destination.reservations;

extern "C++" {
const Char* g_strFile = "data\\Generals.str";
const Char* g_csfFile = "data\\%s\\Generals.csf";
const char* gAppPrefix = "";
GameEngine* CreateGameEngine() { return NEW SDL3GameEngine; }
}

export namespace navigation::testing {
struct QueueSnapshotResult { bool fifo=false,crc=false,commandState=false; unsigned adopted=0,moved=0; };
struct CommandResponse { unsigned firstRouteTicks=0,replacementRouteTicks=0; bool moved=false,latestGoal=false; std::string detail; };
struct ReplayRouteSample {
    std::string unitType;
    unsigned work=0,nodes=0,searchSlices=0,maximumSliceWork=0,maximumPendingPollWork=0;
    std::uint64_t digest=0;
    double firstMilliseconds=0,maximumWarmMilliseconds=0;
    double maximumSearchSliceMilliseconds=0,maximumDispatchMilliseconds=0,maximumOwnerPollMilliseconds=0;
    bool found=false,repeatable=true;
};
struct GroupMovement {
    unsigned created = 0, alive = 0, advanced = 0, closer = 0, abandoned = 0;
    unsigned captureAudits=0;
    unsigned waiting = 0, invalidInitialGoals = 0;
    unsigned pendingAfterFirstUpdate = 0;
    std::uint64_t navigationWork = 0;
    unsigned maximumNavigationWork = 0, budgetExhaustedUpdates = 0;
    double pathfindQueueMilliseconds = 0, worstPathfindQueueMilliseconds = 0;
    double requestMilliseconds = 0, worstRequestMilliseconds = 0;
    double dispatchMilliseconds = 0, worstDispatchMilliseconds = 0;
    double maximumSliceMilliseconds = 0;
    std::uint32_t maximumSliceObject = 0;
    std::uint64_t maximumSliceWork = 0;
    std::int32_t maximumSliceStartX = 0, maximumSliceStartY = 0;
    std::int32_t maximumSliceGoalX = 0, maximumSliceGoalY = 0;
    double sliceMilliseconds=0,commitMilliseconds=0;
    double commandMilliseconds = 0, updateMilliseconds = 0, worstUpdateMilliseconds = 0;
    std::uint64_t digest = 14695981039346656037ull;
    std::vector<std::string> stalled;
    std::vector<unsigned> frameCRCs;
};
struct DrivingStability {
    unsigned reversals=0,maximumReversals=0,blocked=0;
    float progress=0,maximumTurn=0;
    std::vector<std::string> details;
};
struct RequestLifecycle {
    bool waiting = false, noFailureFallback = false, latestDestination = false;
    bool completed = false, destructionCancelled = false, staleIdDiscarded = false;
};
struct SaveOracleCommand {
    std::uint32_t objectId=0, targetId=0, otherTargetId=0;
    unsigned frame=0;
    int command=-1, source=0, intValue=0;
    Coord3D position{};
    std::vector<Coord3D> coordinates;
};
struct SaveOracleReport {
    bool loaded=false;
    unsigned objectCount=0, frames=0;
    std::size_t commandCount=0;
    std::uint64_t commandDigest=14695981039346656037ull;
    std::uint64_t frameDigest=14695981039346656037ull;
    double loadMilliseconds=0, totalUpdateMilliseconds=0, worstUpdateMilliseconds=0;
    double maximumPathQueueMilliseconds=0,
        maximumPathSliceMilliseconds=0,
        maximumPathCommitMilliseconds=0, maximumPathRequestMilliseconds=0,
        maximumPathDispatchMilliseconds=0;
    std::uint32_t maximumPathSliceObject=0;
    std::uint64_t maximumPathSliceWork=0;
    std::uint64_t navigationWork=0, maximumNavigationWork=0;
    std::vector<SaveOracleCommand> commands;
    std::vector<std::size_t> commandCountsPerFrame;
    unsigned commandFrame=0;
};
// Own the real engine in a dedicated process. Do not share its singleton state
// with the smaller terrain fixtures, or write to a player's Documents folder.
extern "C++" class Simulation {
    std::filesystem::path dataRoot;

    static std::uint64_t mix(std::uint64_t state,std::uint64_t value) {
        state^=value;
        return state*1099511628211ull;
    }

    static void captureSaveCommand(ObjectID objectId,const AICommandParms* parms,void* context) {
        auto& report=*static_cast<SaveOracleReport*>(context);
        SaveOracleCommand command;
        command.objectId=static_cast<std::uint32_t>(objectId);
        command.frame=report.commandFrame;
        command.command=static_cast<int>(parms->m_cmd);
        command.source=static_cast<int>(parms->m_cmdSource);
        command.position=parms->m_pos;
        command.targetId=parms->m_obj ? static_cast<std::uint32_t>(parms->m_obj->getID()) : 0;
        command.otherTargetId=parms->m_otherObj ? static_cast<std::uint32_t>(parms->m_otherObj->getID()) : 0;
        command.intValue=parms->m_intValue;
        command.coordinates=parms->m_coords;
        report.commands.push_back(command);
        ++report.commandCount;
        report.commandDigest=mix(report.commandDigest,command.objectId);
        report.commandDigest=mix(report.commandDigest,static_cast<std::uint32_t>(command.command));
        report.commandDigest=mix(report.commandDigest,static_cast<std::uint32_t>(command.source));
        report.commandDigest=mix(report.commandDigest,command.targetId);
        report.commandDigest=mix(report.commandDigest,command.otherTargetId);
        report.commandDigest=mix(report.commandDigest,static_cast<std::uint32_t>(command.intValue));
        for (const float value:{command.position.x,command.position.y,command.position.z})
            report.commandDigest=mix(report.commandDigest,std::bit_cast<std::uint32_t>(value));
        for (const auto& coordinate:command.coordinates)
            for (const float value:{coordinate.x,coordinate.y,coordinate.z})
                report.commandDigest=mix(report.commandDigest,std::bit_cast<std::uint32_t>(value));
    }

public:
    Simulation() {
        if (TheGameEngine || TheGlobalData)
            throw std::logic_error("Simulation requires an isolated engine process");
        static CriticalSection locks[5];
        TheAsciiStringCriticalSection = &locks[0];
        TheUnicodeStringCriticalSection = &locks[1];
        TheDmaCriticalSection = &locks[2];
        TheMemoryPoolCriticalSection = &locks[3];
        TheDebugLogCriticalSection = &locks[4];
        initMemoryManager();
        dataRoot = std::filesystem::temp_directory_path() /
            ("generals-navigation-simulation-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(dataRoot);
        TheWritableGlobalData = NEW GlobalData(dataRoot.string().c_str());
        CommandLine::parseCommandLineForStartup();
        TheWritableGlobalData->m_headless = true;
        TheWritableGlobalData->m_playIntro = false;
        TheWritableGlobalData->m_playSizzle = false;
        TheWritableGlobalData->m_shellMapOn = false;
        engine::debug::set_headless(true);
        TheVersion = NEW Version;
        TheVersion->setVersion(VERSION_MAJOR, VERSION_MINOR, VERSION_BUILDNUM, VERSION_LOCALBUILDNUM,
            AsciiString(VERSION_BUILDUSER), AsciiString(VERSION_BUILDLOC),
            AsciiString(__TIME__), AsciiString(__DATE__));
        TheFramePacer = new FramePacer;
        TheFramePacer->enableFramesPerSecondLimit(false);
        TheGameEngine = CreateGameEngine();
        TheGameEngine->setIsActive(true);
        TheGameEngine->init();
    }
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    ~Simulation() {
        delete TheFramePacer;
        TheFramePacer = nullptr;
        delete TheGameEngine;
        TheGameEngine = nullptr;
        // GameEngine owns and tears down the gameplay subsystems, but the
        // process-level GlobalData allocation is created by this fixture.
        // Release it here so the next Boost case can construct a genuinely
        // isolated engine in the same test process.
        if (TheWritableGlobalData) {
            TheWritableGlobalData->reset();
            delete TheWritableGlobalData;
            TheWritableGlobalData = nullptr;
        }
        delete TheVersion;
        TheVersion = nullptr;
    }
    bool hasUnitTemplate(const char* name) const {
        return TheThingFactory && TheThingFactory->findTemplate(name) != nullptr;
    }
    void loadTwilightFlame(bool saveCompatibleSkirmish=false) {
        if (!TheSkirmishGameInfo) TheSkirmishGameInfo = NEW SkirmishGameInfo;
        auto* game = TheSkirmishGameInfo;
        game->enterGame();
        game->setMap("Maps\\Twilight Flame\\Twilight Flame.map");
        game->setSeed(0x13579);
        game->setLocalIP(saveCompatibleSkirmish?0:1);
        const int faction = ThePlayerTemplateStore->getTemplateNumByName("FactionAmerica");
        if (faction < 0) throw std::runtime_error("America player template is missing");
        for (int i = 0; i < MAX_SLOTS; ++i) {
            auto* slot = game->getSlot(i);
            if (!slot) throw std::runtime_error("Engine did not initialize skirmish slots");
            slot->setState(i==0 ? SLOT_PLAYER : i==1 ?
                (saveCompatibleSkirmish?SLOT_EASY_AI:SLOT_PLAYER) : SLOT_CLOSED,
                UnicodeString(i == 0 ? L"Navigation player" : L"Navigation opponent"),
                saveCompatibleSkirmish?0:i+1);
            slot->setPlayerTemplate(faction);
            slot->setColor(i);
            slot->setStartPos(i);
            slot->setTeamNumber(0);
        }
        TheWritableGlobalData->m_mapName = game->getMap();
        game->startGame(0);
        if (game->getLocalSlotNum() != 0)
            throw std::runtime_error("Skirmish fixture has no local player slot");
        InitRandom(game->getSeed());
        TheGameLogic->prepareNewGame(GAME_SKIRMISH, DIFFICULTY_NORMAL, 0);
        // The normal new-game dispatch first arms loading; the next update loads it.
        TheGameLogic->startNewGame(false);
        TheGameLogic->UPDATE();
        if (!TheGameLogic->isInGame() || TheGameLogic->isLoadingMap())
            throw std::runtime_error("Twilight Flame did not finish loading");
    }
    SaveOracleReport replaySaveOracle(const std::filesystem::path& saveFile,unsigned frames=120,
        bool resetPlanner=false,bool startupSeed=false) {
        if (!std::filesystem::exists(saveFile))
            throw std::runtime_error("Save oracle does not exist: " + saveFile.string());
        const auto leaf=saveFile.filename();
        const auto saveDirectory=dataRoot/"Save";
        std::filesystem::create_directories(saveDirectory);
        std::filesystem::copy_file(saveFile,saveDirectory/leaf,
            std::filesystem::copy_options::overwrite_existing);

        SaveOracleReport report;
        // Save-oracle replay must not inherit the process-start wall-clock
        // seed.  The gameplay save format predates explicit RNG snapshots;
        // use a fixed logic seed here so command and CRC differences expose
        // navigation state, not unrelated random AI scheduling.
        InitRandom(startupSeed ? 123u : 0x4e415649u);
        TheWritableGlobalData->m_fixedSeed=startupSeed ? 0x4e415649 : -1;
        const auto loadStart=std::chrono::steady_clock::now();
        TheWritableGlobalData->m_loadSaveGame=leaf.string().c_str();
        TheGameState->loadQueuedSaveGame();
        report.loadMilliseconds=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-loadStart).count();
        if (!TheGameLogic->isInGame())
            throw std::runtime_error("Save oracle did not load into an active game");
        // A save restoration replaces native cells and objects while the
        // process-local planner may still own snapshots from an earlier
        // replay. Invalidate at this dependency boundary before any request
        // can observe those pointers. This is required for same-process
        // planner-reset determinism and mirrors a fresh engine instance.
        TheAI->pathfinder()->invalidateNavigationSnapshots();
        if (resetPlanner) resetNavigationPlanner();
        report.loaded=true;
        report.objectCount=objectCount();

        struct ObserverGuard {
            ~ObserverGuard() { AIUpdateInterface::setCommandObserver(nullptr,nullptr); }
        } observerGuard;
        AIUpdateInterface::setCommandObserver(&captureSaveCommand,&report);
        for (unsigned frame=0;frame<frames;++frame) {
            report.commandFrame=frame;
            const auto start=std::chrono::steady_clock::now();
            TheGameLogic->UPDATE();
            const auto elapsed=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-start).count();
            report.totalUpdateMilliseconds+=elapsed;
            report.worstUpdateMilliseconds=std::max(report.worstUpdateMilliseconds,elapsed);
            const auto navigation=TheAI->pathfinder()->getNavigationStats();
            report.maximumPathQueueMilliseconds=std::max(report.maximumPathQueueMilliseconds,
                navigation.lastQueueNanoseconds/1'000'000.0);
            report.maximumPathSliceMilliseconds=std::max(report.maximumPathSliceMilliseconds,
                navigation.maximumSliceNanoseconds/1'000'000.0);
            report.maximumPathSliceObject=navigation.maximumSliceObject;
            report.maximumPathSliceWork=navigation.maximumSliceWork;
            report.maximumPathCommitMilliseconds=std::max(report.maximumPathCommitMilliseconds,
                navigation.lastCommitNanoseconds/1'000'000.0);
            report.maximumPathRequestMilliseconds=std::max(report.maximumPathRequestMilliseconds,
                navigation.lastRequestNanoseconds/1'000'000.0);
            report.maximumPathDispatchMilliseconds=std::max(report.maximumPathDispatchMilliseconds,
                navigation.lastDispatchNanoseconds/1'000'000.0);
            report.navigationWork+=TheAI->pathfinder()->m_cumulativeCellsAllocated;
            report.maximumNavigationWork=std::max(report.maximumNavigationWork,
                static_cast<std::uint64_t>(TheAI->pathfinder()->m_cumulativeCellsAllocated));
            report.frameDigest=mix(report.frameDigest,TheGameLogic->getCRC(CRC_RECALC));
            report.commandCountsPerFrame.push_back(report.commandCount);
            ++report.frames;
        }
        return report;
    }
    void resetNavigationPlanner() {
        auto* pf=TheAI->pathfinder();
        pf->m_groundPlanner=std::make_unique<GroundRoutePlanner>(*pf);
        if (pf->m_isMapReady) pf->m_groundPlanner->warmStaticSnapshot();
    }
    unsigned objectCount() const {
        unsigned count = 0;
        for (auto* object = TheGameLogic->getFirstObject(); object; object = object->getNextObject()) ++count;
        return count;
    }
    bool moveObjectlessVehiclePreview() {
        // Headless drawing does not emit tracks. Exercise the actual crashing
        // entry points explicitly, including an uncapped track carried over
        // when a render object is rebound to a preview.
        class PreviewTracks final : public TerrainTracksRenderObjClass {
        public:
            bool rejectsObjectlessOwner(const Drawable* owner) {
                setOwnerDrawable(owner);
                m_haveAnchor = false;
                addEdgeToTrack(1000, 1000);
                if (m_haveAnchor) return false;
                m_haveAnchor = true;
                m_haveCap = false;
                m_activeEdgeCount = 2;
                addEdgeToTrack(1020, 1000);
                if (!m_haveAnchor || m_haveCap || m_activeEdgeCount != 2) return false;
                addCapEdgeToTrack(1020, 1000);
                return !m_haveCap && m_activeEdgeCount == 2;
            }
        } tracks;
        bool safe = tracks.rejectsObjectlessOwner(nullptr);
        const auto objectsBefore = objectCount();
        for (unsigned cycle = 0; cycle < 8; ++cycle) {
            for (const char* name : {"AmericaVehicleHumvee", "AmericaTankPaladin", "AmericaInfantryRanger"}) {
                const auto* unit = TheThingFactory->findTemplate(name);
                if (!unit) return false;
                auto* preview = TheThingFactory->newDrawable(unit, DRAWABLE_STATUS_NO_STATE_PARTICLES);
                if (!preview) return false;
                const auto id = preview->getID();
                safe &= preview->getObject() == nullptr && tracks.rejectsObjectlessOwner(preview);
                tracks.setOwnerDrawable(nullptr);
                preview->setModelConditionState(MODELCONDITION_MOVING);
                for (int i = 0; i < 100; ++i) {
                    Coord3D position{1000.0f + i * 20.0f, 1000.0f, 0.0f};
                    position.z = TheTerrainLogic->getGroundHeight(position.x, position.y);
                    preview->setPosition(&position);
                    preview->setOrientation(i * 0.1f);
                    preview->setDrawableOpacity(0.35f * (100 - i) / 100.0f);
                    preview->setDrawableHidden((i & 1) != 0);
                }
                TheGameClient->destroyDrawable(preview);
                safe &= TheGameClient->findDrawableByID(id) == nullptr;
            }
        }
        return safe && objectCount() == objectsBefore;
    }
    float moveRealHumvee() {
        auto* object = TheThingFactory->newObject(
            TheThingFactory->findTemplate("AmericaVehicleHumvee"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        if (!object || !object->getAIUpdateInterface() || !object->getPhysics() || !object->getDrawable())
            throw std::runtime_error("Humvee requires the actual AI, physics, and drawable modules");
        Coord3D start{3125.0f, 385.0f, 0.0f};
        start.z = TheTerrainLogic->getGroundHeight(start.x, start.y);
        object->setPosition(&start);
        Coord3D goal{4475.0f, 1385.0f, 0.0f};
        goal.z = TheTerrainLogic->getGroundHeight(goal.x, goal.y);
        const auto id = object->getID();
        object->getAIUpdateInterface()->aiMoveToPosition(&goal, CMD_FROM_PLAYER);
        const auto firstFrame = TheGameLogic->getFrame();
        for (unsigned i = 0; i < 300; ++i) TheGameLogic->UPDATE();
        if (TheGameLogic->getFrame() != firstFrame + 300)
            throw std::runtime_error("Simulation did not advance 300 logic frames");
        object = TheGameLogic->findObjectByID(id);
        if (!object) throw std::runtime_error("Humvee disappeared during movement");
        const auto* end = object->getPosition();
        return std::hypot(end->x - start.x, end->y - start.y);
    }
    float terrainWidth() const {
        Region3D extent;
        TheTerrainLogic->getExtent(&extent);
        return extent.hi.x - extent.lo.x;
    }
    bool hijackingInvalidatesQueuedContacts() {
        auto* local=ThePlayerList->getLocalPlayer();
        Player* enemy=nullptr;
        for (Int index=0;index<ThePlayerList->getPlayerCount();++index) {
            auto* candidate=ThePlayerList->getNthPlayer(index);
            if (candidate && candidate!=local && candidate->isPlayableSide()) {
                enemy=candidate;
                break;
            }
        }
        if (!enemy) throw std::runtime_error("Missing collision fixture opponent");
        const auto previous=local->getRelationship(enemy->getDefaultTeam());
        local->setPlayerRelationship(enemy,ENEMIES);
        auto* hijacker=TheThingFactory->newObject(TheThingFactory->findTemplate("GLAInfantryHijacker"),
            local->getDefaultTeam());
        auto* vehicle=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleHumvee"),
            enemy->getDefaultTeam());
        Coord3D position{3125,385,0};
        position.z=TheTerrainLogic->getGroundHeight(position.x,position.y);
        hijacker->setPosition(&position); vehicle->setPosition(&position);
        std::vector<Object*> neighbors;
        for (unsigned i=0;i<4;++i) {
            auto* unit=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
                local->getDefaultTeam());
            unit->setPosition(&position); neighbors.push_back(unit);
        }
        // Publish initial visibility before issuing a player command: freshly
        // created targets have not yet been revealed by the partition update.
        ThePartitionManager->update();
        hijacker->getAIUpdateInterface()->aiEnter(vehicle,CMD_FROM_PLAYER);
        if (hijacker->getRelationship(vehicle)!=ENEMIES)
            throw std::runtime_error("Hijacking fixture requires enemy units");
        if (hijacker->getAIUpdateInterface()->getGoalObject()!=vehicle)
            throw std::runtime_error("Hijacker did not accept the vehicle entry order");
        if (hijacker->isAboveTerrain())
            throw std::runtime_error("Hijacking fixture must start on the ground");
        // The actual contact traversal calls the hijacker's collide module,
        // which unregisters it while other contacts still refer to its partition.
        ThePartitionManager->update();
        bool valid=vehicle->getStatusBits().test(OBJECT_STATUS_HIJACKED) &&
            vehicle->getControllingPlayer()==local && !hijacker->isDestroyed() &&
            !hijacker->friend_getPartitionData();
        std::vector<Object*> nearby;
        collectPartitionNeighbors(vehicle,80,nearby);
        valid=valid && std::find(nearby.begin(),nearby.end(),hijacker)==nearby.end();
        for (auto* unit:neighbors) TheGameLogic->destroyObject(unit);
        TheGameLogic->destroyObject(hijacker); TheGameLogic->destroyObject(vehicle);
        local->setPlayerRelationship(enemy,previous);
        TheGameLogic->UPDATE();
        return valid;
    }
    bool destroyedArmiesLeaveSurvivorUpdatesScheduled() {
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        const auto* ranger=TheThingFactory->findTemplate("AmericaInfantryRanger");
        auto* survivor=TheThingFactory->newObject(ranger,team);
        Coord3D start{3125,385,0},goal{3225,385,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        goal.z=TheTerrainLogic->getGroundHeight(goal.x,goal.y);
        survivor->setPosition(&start);
        std::vector<ObjectID> removed;
        for (unsigned i=0;i<1024;++i) {
            auto* unit=TheThingFactory->newObject(ranger,team);
            Coord3D position{3300.f+float(i%32)*20,500.f+float(i/32)*20,0};
            position.z=TheTerrainLogic->getGroundHeight(position.x,position.y);
            unit->setPosition(&position);
            removed.push_back(unit->getID());
            TheGameLogic->destroyObject(unit);
        }
        survivor->getAIUpdateInterface()->aiMoveToPosition(&goal,CMD_FROM_PLAYER);
        TheGameLogic->UPDATE();
        bool valid=true;
        for (auto id:removed) valid=valid && !TheGameLogic->findObjectByID(id);
        // Stale scheduled modules would run after their owning objects died.
        // Unrelated modules must remain scheduled and move the survivor.
        for (unsigned frame=0;frame<80;++frame) TheGameLogic->UPDATE();
        const auto& end=*survivor->getPosition();
        valid=valid && std::hypot(end.x-start.x,end.y-start.y)>20;
        TheGameLogic->destroyObject(survivor);
        TheGameLogic->UPDATE();
        return valid;
    }
    bool destroyedMoversCancelWeightedTasks() {
        GroundRoutePlanner planner(*TheAI->pathfinder());
        for (unsigned phase=0;phase<5;++phase) {
            auto* unit=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
                ThePlayerList->getLocalPlayer()->getDefaultTeam());
            Coord3D point{3125,385,0};
            point.z=TheTerrainLogic->getGroundHeight(point.x,point.y);
            unit->setPosition(&point);
            const auto id=unit->getID();
            Pathfinder::GroundRouteQuery query;
            query.object=unit;
            const auto reject=[unit](int,int,PathfindLayerEnum,unsigned)->std::optional<double> {
                if (unit->isDestroyed()) throw std::runtime_error("Weighted task queried a destroyed mover");
                return {};
            };
            const bool complete=phase==2 || phase==4;
            auto task=complete ? planner.weightedTask(query,point,point,false,{},{},{}) :
                planner.weightedTask(query,point,point,false,{},reject,{});
            if ((phase==1 || phase==3) && !task.resume()) throw std::runtime_error("Native mover query did not suspend");
            if (complete) while (task.resume()) {}
            TheGameLogic->destroyObject(unit);
            // First validation after removal must not dereference the captured pointer.
            if (phase==3) {
                TheGameLogic->UPDATE();
                if (TheGameLogic->findObjectByID(id)) return false;
            }
            const auto before=TheAI->pathfinder()->m_cumulativeCellsAllocated;
            // Adoption must independently reject a completed route, without resume().
            if (phase==4 && task.take().path) return false;
            if (task.resume() || task.take().path || TheAI->pathfinder()->m_cumulativeCellsAllocated!=before)
                return false;
            TheGameLogic->UPDATE();
            if (TheGameLogic->findObjectByID(id)) return false;
            if (task.resume() || task.take().path) return false;
        }
        return true;
    }
    bool deferredDestinationIsRevalidatedBeforePublication() {
        auto* pathfinder=TheAI->pathfinder();
        struct RestoreDeferred { Pathfinder* pf; Bool previous;
            ~RestoreDeferred() { pf->setGroundQueriesDeferred(previous); }
        } restore{pathfinder,pathfinder->groundQueriesDeferred()};
        pathfinder->setGroundQueriesDeferred(true); // Explicit test-only suspended query.
        GroundRoutePlanner planner(*pathfinder);
        auto* unit=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        Coord3D from{3125,385,0},to{3225,385,0};
        from.z=TheTerrainLogic->getGroundHeight(from.x,from.y);
        unit->setPosition(&from);
        Pathfinder::GroundRouteQuery query;
        query.object=unit;
        bool reserved=false,usedAlternative=false;
        unsigned observations=0;
        const auto rank=[&](int x,int y,PathfindLayerEnum layer,unsigned)->std::optional<double> {
            if (x!=321 || y!=38 || layer!=LAYER_GROUND) return {};
            ++observations;
            return reserved?std::nullopt:std::optional<double>{1};
        };
        const auto advance=[&] {
            pathfinder->m_cumulativeCellsAllocated=0;
            return planner.findWeighted(query,&from,&to,false,{},rank,ICoord2D{322,38},
                &usedAlternative,true);
        };
        bool valid=true;
        Path* published=nullptr;
        for (unsigned i=0;i<1000 && observations<2;++i) {
            published=advance();
            if (published) break;
            if (observations) reserved=true;
        }
        valid=!published && observations==2 && planner.hasPending(unit->getID()) && !usedAlternative;
        if (published) deleteInstance(published);
        reserved=false;
        published=nullptr;
        for (unsigned i=0;i<1000 && !published;++i) published=advance();
        valid=valid && published && usedAlternative && !planner.hasPending(unit->getID());
        if (published) {
            const auto& end=*published->getLastNode()->getPosition();
            valid=valid && end.x==3215 && end.y==385;
            deleteInstance(published);
        }
        TheGameLogic->destroyObject(unit);
        TheGameLogic->UPDATE();
        return valid;
    }
    bool replacementOrdersCancelWeightedTasks() {
        GroundRoutePlanner planner(*TheAI->pathfinder());
        auto* unit=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        Coord3D point{3125,385,0},goal{3225,385,0};
        point.z=TheTerrainLogic->getGroundHeight(point.x,point.y);
        goal.z=TheTerrainLogic->getGroundHeight(goal.x,goal.y);
        unit->setPosition(&point);
        auto* ai=unit->getAIUpdateInterface();
        Pathfinder::GroundRouteQuery query;
        query.object=unit;
        bool valid=true;
        for (unsigned phase=0;phase<4;++phase) {
            ai->aiMoveToPosition(&goal,CMD_FROM_PLAYER);
            auto task=planner.weightedTask(query,point,point,false,{},{},{});
            while (task.resume()) {}
            if (phase==0) ai->aiIdle(CMD_FROM_PLAYER);
            if (phase==1) ai->aiMoveToPosition(&goal,CMD_FROM_PLAYER); // Same position, new order.
            if (phase==2) ai->requestPath(&goal,true);
            if (phase==3) ai->destroyPath();
            const auto work=TheAI->pathfinder()->m_cumulativeCellsAllocated;
            valid=(!task.take().path) && valid;
            valid=(TheAI->pathfinder()->m_cumulativeCellsAllocated==work) && valid;
        }
        // A fresh query under the replacement order remains usable.
        auto replacement=planner.weightedTask(query,point,point,false,{},{},{});
        while (replacement.resume()) {}
        valid=bool(replacement.take().path) && valid;
        TheGameLogic->destroyObject(unit);
        TheGameLogic->UPDATE();
        return valid;
    }
    void collectPartitionNeighbors(const Object* origin,float radius,std::vector<Object*>& output) {
        output.clear();
        auto* iterator=ThePartitionManager->iterateObjectsInRange(origin,radius,FROM_CENTER_2D);
        MemoryPoolObjectHolder hold(iterator);
        for (auto* other=iterator->first();other;other=iterator->next()) output.push_back(other);
    }
    bool movingSmallUnitsKeepPartitionMembershipsCurrent() {
        auto* object=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        auto* observer=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        Coord3D observerPosition{3180,385,0};
        observerPosition.z=TheTerrainLogic->getGroundHeight(observerPosition.x,observerPosition.y);
        observer->setPosition(&observerPosition);
        if (!object->getGeometryInfo().getIsSmall())
            throw std::runtime_error("Partition regression requires small geometry");
        bool valid=true;
        std::vector<Object*> nearby;
        for (unsigned step=0;step<32;++step) {
            Coord3D position{3125.f+float(step)*3.25f,385.f+float(step%5),0};
            position.z=TheTerrainLogic->getGroundHeight(position.x,position.y);
            object->setPosition(&position);
            ThePartitionManager->update();
            for (auto* origin:{object,observer}) for (float radius:{1.f,20.f,80.f}) {
                collectPartitionNeighbors(origin,radius,nearby);
                std::vector<unsigned> actual,expected;
                for (auto* other:nearby) actual.push_back(unsigned(other->getID()));
                for (auto* other=TheGameLogic->getFirstObject();other;other=other->getNextObject()) {
                    if (other==origin || !other->friend_getPartitionData()) continue;
                    const auto* p=other->getPosition();
                    const float dx=p->x-origin->getPosition()->x,dy=p->y-origin->getPosition()->y;
                    if (dx*dx+dy*dy<radius*radius) expected.push_back(unsigned(other->getID()));
                }
                std::sort(actual.begin(),actual.end());std::sort(expected.begin(),expected.end());
                valid=(actual==expected) && valid;
            }
        }
        ThePartitionManager->unRegisterObject(object);
        collectPartitionNeighbors(observer,80,nearby);
        valid=valid && std::find(nearby.begin(),nearby.end(),object)==nearby.end();
        ThePartitionManager->registerObject(object);
        ThePartitionManager->update();
        collectPartitionNeighbors(observer,80,nearby);
        valid=valid && std::count(nearby.begin(),nearby.end(),object)==1;
        TheGameLogic->destroyObject(observer);
        TheGameLogic->destroyObject(object);
        TheGameLogic->UPDATE();
        return valid;
    }
    bool generalPartitionRangesMatchBruteForce() {
        ThePartitionManager->update();
        for (const Coord3D position: {Coord3D{3125,385,0},Coord3D{2580,2080,60},Coord3D{25,25,0}})
            for (float radius:{0.0f,1.0f,20.0f,100.0f,500.0f}) {
                std::vector<unsigned> expected,actual;
                for (auto* object=TheGameLogic->getFirstObject();object;object=object->getNextObject()) {
                    if (!object->friend_getPartitionData()) continue;
                    const auto* p=object->getPosition();
                    const float dx=p->x-position.x,dy=p->y-position.y;
                    if (dx*dx+dy*dy<radius*radius) expected.push_back(unsigned(object->getID()));
                }
                auto* iterator=ThePartitionManager->iterateObjectsInRange(&position,radius,FROM_CENTER_2D);
                MemoryPoolObjectHolder hold(iterator);
                for (auto* object=iterator->first();object;object=iterator->next()) actual.push_back(unsigned(object->getID()));
                std::sort(expected.begin(),expected.end());std::sort(actual.begin(),actual.end());
                if (expected!=actual) return false;
            }
        return true;
    }
    bool capturedCellsKeepHistoricalValues() {
        auto* unit=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        auto* pathfinder=TheAI->pathfinder();
        auto* cell=pathfinder->getCell(LAYER_GROUND,316,38);
        const auto oldPosition=cell->getPosUnit(),oldGoal=cell->getGoalUnit();
        const auto oldAircraftGoal=cell->getGoalAircraft();
        cell->setGoalUnit(INVALID_ID,{316,38});
        cell->setPosUnit(unit->getID(),{316,38});
        cell->setGoalUnit(unit->getID(),{316,38});
        cell->setGoalAircraft(unit->getID(),{316,38});
        MovementValidator validator(*pathfinder);
        const auto before=validator.captureCells(LAYER_GROUND);
        cell->setGoalUnit(INVALID_ID,{316,38});
        cell->setPosUnit(INVALID_ID,{316,38});
        cell->setGoalAircraft(INVALID_ID,{316,38});
        const auto after=validator.captureCells(LAYER_GROUND);
        const auto* saved=before.cell(316,38);
        const auto* current=after.cell(316,38);
        const bool retained=saved && current && before.layer()==unsigned(LAYER_GROUND) &&
            saved->occupancy.fixed && saved->occupancy.unit==unsigned(unit->getID()) &&
            saved->goal==unsigned(unit->getID()) && saved->terrain==unsigned(cell->getType()) &&
            saved->connection==unsigned(cell->getConnectLayer()) && saved->pinched==bool(cell->getPinched()) &&
            saved->fence==bool(cell->isObstacleFence()) &&
            saved->aircraftReserved && saved->aircraftGoal==unsigned(unit->getID()) &&
            !current->aircraftReserved && current->aircraftGoal==0 &&
            current->occupancy.empty && current->goal==unsigned(INVALID_ID) &&
            validator.captureCells(LAYER_INVALID).cell(316,38)==nullptr;
        cell->setPosUnit(oldPosition,{316,38});
        cell->setGoalUnit(oldGoal,{316,38});
        cell->setGoalAircraft(oldAircraftGoal,{316,38});
        TheGameLogic->destroyObject(unit);
        TheGameLogic->UPDATE();
        return retained;
    }
    bool destinationReservationsMatchCapturedState() {
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        auto* mover=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),team);
        auto* ally=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),team);
        auto* aircraft=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleChinook"),team);
        auto* ai=aircraft->getAIUpdateInterface();
        ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
        if (!ai->isAircraftThatAdjustsDestination()) throw std::runtime_error("Chinook needs its aircraft locomotor");
        auto* pathfinder=TheAI->pathfinder();
        auto* cell=pathfinder->getCell(LAYER_GROUND,316,38);
        if (cell->getType()!=PathfindCell::CELL_CLEAR) throw std::runtime_error("Destination fixture requires clear terrain");
        const auto oldPosition=cell->getPosUnit(),oldGoal=cell->getGoalUnit(),oldAircraft=cell->getGoalAircraft();
        cell->setGoalUnit(INVALID_ID,{316,38});
        cell->setPosUnit(ally->getID(),{316,38});
        cell->setGoalUnit(ally->getID(),{316,38});
        cell->setGoalAircraft(ally->getID(),{316,38});
        MovementValidator validator(*pathfinder);
        const auto units=MovementValidator::captureOccupants(MovementValidator::prepare(mover,0,true));
        const auto before=validator.captureCells(LAYER_GROUND);
        const DestinationQuery ground{unsigned(mover->getID()),0,0,true,true,false,true};
        const DestinationQuery air{unsigned(aircraft->getID()),0,0,true,true,true,true};
        const auto native=[&](const Object* object) { return pathfinder->checkDestination(object,316,38,LAYER_GROUND,0,true)!=0; };
        bool correct=!native(mover) && native(ally) && !native(nullptr) && !native(aircraft) &&
            !permitsDestinationCell(ground,before.destination(316,38),units) &&
            !permitsDestinationCell(air,before.destination(316,38),units);
        cell->setGoalAircraft(aircraft->getID(),{316,38});
        const auto reservedForSelf=validator.captureCells(LAYER_GROUND);
        correct &= native(aircraft) && permitsDestinationCell(air,reservedForSelf.destination(316,38),units) &&
            !permitsDestinationCell(air,before.destination(316,38),units);
        cell->setGoalUnit(INVALID_ID,{316,38});
        cell->setPosUnit(INVALID_ID,{316,38});
        cell->setGoalAircraft(INVALID_ID,{316,38});
        const auto cleared=validator.captureCells(LAYER_GROUND);
        correct &= native(mover) && native(aircraft) &&
            permitsDestinationCell(ground,cleared.destination(316,38),units) &&
            permitsDestinationCell(air,cleared.destination(316,38),units) &&
            !permitsDestinationCell(ground,before.destination(316,38),units);
        cell->setPosUnit(oldPosition,{316,38});
        cell->setGoalUnit(oldGoal,{316,38});
        cell->setGoalAircraft(oldAircraft,{316,38});
        TheGameLogic->destroyObject(aircraft);
        TheGameLogic->destroyObject(ally);
        TheGameLogic->destroyObject(mover);
        TheGameLogic->UPDATE();
        return correct;
    }
    bool capturedOccupantsOutliveNativeObjects() {
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        auto* mover=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaTankPaladin"),team);
        auto* target=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),team);
        const auto id=target->getID();
        const auto context=MovementValidator::prepare(mover,1,false);
        const auto before=MovementValidator::captureOccupants(context);
        const auto* captured=before.find(static_cast<std::uint32_t>(id));
        if (!captured) throw std::runtime_error("Native occupant was not captured");
        const bool matches=before.allied(captured) && before.infantry(captured) &&
            before.canMoveAside(captured) && !before.canCrush(captured) &&
            before.crushableLevel(captured)==target->getCrushableLevel();
        TheGameLogic->destroyObject(target);
        TheGameLogic->UPDATE();
        const auto after=MovementValidator::captureOccupants(context);
        const bool retained=before.find(static_cast<std::uint32_t>(id))==captured &&
            before.allied(captured) && before.infantry(captured) &&
            after.find(static_cast<std::uint32_t>(id))==nullptr;
        TheGameLogic->destroyObject(mover);
        TheGameLogic->UPDATE();
        return matches && retained;
    }
    bool lineChecksDistinguishFixedAndMovingOccupancy() {
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        auto* mover=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),team);
        auto* other=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleHumvee"),team);
        auto* pathfinder=TheAI->pathfinder();
        auto* cell=pathfinder->getCell(LAYER_GROUND,316,38);
        const auto previous=cell->getPosUnit();
        const auto previousGoal=cell->getGoalUnit();
        const Coord3D from{3125,385,0};
        bool matches=true;
        unsigned clear=0,blocked=0;
        for (auto id : {INVALID_ID,mover->getID(),other->getID()}) {
            cell->setGoalUnit(INVALID_ID,{316,38});
            cell->setPosUnit(id,{316,38});
            cell->setGoalUnit(id,{316,38});
            for (const Coord3D to : {Coord3D{3205,385,0},Coord3D{3205,405,0},
                Coord3D{3145,465,0},Coord3D{3105,465,0}}) {
                // Fixed occupancy has the same blocking result with or without
                // transient checks. Self-occupancy remains traversable.
                const bool full=pathfinder->isLinePassable(mover,LOCOMOTORSURFACE_GROUND,LAYER_GROUND,from,to,true,true);
                const bool fixedOnly=pathfinder->isLinePassable(mover,LOCOMOTORSURFACE_GROUND,LAYER_GROUND,from,to,false,true);
                matches &= full==fixedOnly;
                if (full) ++clear; else ++blocked;
            }
        }
        cell->setGoalUnit(INVALID_ID,{316,38});
        cell->setPosUnit(other->getID(),{316,38});
        const Coord3D across{3205,385,0};
        matches &= pathfinder->isLinePassable(mover,LOCOMOTORSURFACE_GROUND,LAYER_GROUND,from,across,false,true);
        matches &= !pathfinder->isLinePassable(mover,LOCOMOTORSURFACE_GROUND,LAYER_GROUND,from,across,true,true);
        cell->setPosUnit(previous,{316,38});
        cell->setGoalUnit(previousGoal,{316,38});
        TheGameLogic->destroyObject(other);
        TheGameLogic->destroyObject(mover);
        TheGameLogic->UPDATE();
        return matches && clear>0 && blocked>0;
    }
    bool overlappingNativeContactsAreInitialized() {
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        bool correct=true;
        for (const char* nameA:{"AmericaInfantryRanger","AmericaVehicleHumvee"})
            for (const char* nameB:{"AmericaInfantryRanger","AmericaVehicleHumvee"}) {
            auto* a=TheThingFactory->newObject(TheThingFactory->findTemplate(nameA),team);
            auto* b=TheThingFactory->newObject(TheThingFactory->findTemplate(nameB),team);
            const float nan=std::numeric_limits<float>::quiet_NaN();
            const Coord3D first{3125,385,0};
            a->setPosition(&first);
            for (float offset:{1.0f,0.0f,-1.0f}) {
                const Coord3D second{first.x+offset,first.y,first.z};
                b->setPosition(&second);
                CollideLocAndNormal output{{nan,nan,nan},{nan,nan,nan}};
                correct &= a->friend_getPartitionData()->friend_collidesWith(b->friend_getPartitionData(),&output);
                correct &= std::isfinite(output.loc.x) && std::isfinite(output.loc.y) && std::isfinite(output.loc.z);
                correct &= std::isfinite(output.normal.x) && std::isfinite(output.normal.y) && std::isfinite(output.normal.z);
                correct &= std::abs(output.normal.x*output.normal.x+output.normal.y*output.normal.y+
                    output.normal.z*output.normal.z-1.0f)<0.0001f;
            }
            TheGameLogic->destroyObject(b);
            TheGameLogic->destroyObject(a);
            TheGameLogic->UPDATE();
        }
        return correct;
    }
    bool lineChecksKeepCrusherCapabilitiesQueryLocal() {
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        auto* ranger=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),team);
        auto* tank=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaTankPaladin"),team);
        auto* pathfinder=TheAI->pathfinder();
        auto* cell=pathfinder->getCell(LAYER_GROUND,316,38);
        const auto previousType=cell->getType();
        const Coord3D from{3125,385,0},to{3205,385,0};
        bool matches=ranger->getCrusherLevel()==0 && tank->getCrusherLevel()>0;
        if (cell->getObstacleID()!=INVALID_ID)
            throw std::runtime_error("Fence crossing fixture requires an empty terrain cell");
        // A fence crossing on actual Twilight Flame terrain must use each
        // query's mover. Alternation catches caches shared between units.
        for (bool fence:{true,false,true}) {
            cell->setType(PathfindCell::CELL_CLEAR);
            matches &= cell->setTypeAsObstacle(ranger,fence,{316,38});
            for (const Object* mover:{tank,ranger,static_cast<Object*>(nullptr),tank,ranger}) {
                const bool expected=fence && mover==tank;
                matches &= bool(pathfinder->isLinePassable(mover,LOCOMOTORSURFACE_GROUND,
                    LAYER_GROUND,from,to,false,true))==expected;
            }
            matches &= cell->removeObstacle(ranger);
        }
        cell->setType(previousType);
        for (const Object* mover:{tank,ranger,static_cast<Object*>(nullptr)})
            matches &= pathfinder->isLinePassable(mover,LOCOMOTORSURFACE_GROUND,
                LAYER_GROUND,from,to,false,true);
        TheGameLogic->destroyObject(tank);
        TheGameLogic->destroyObject(ranger);
        TheGameLogic->UPDATE();
        return matches;
    }
    bool groundBlockingFootprintMatchesFullPolicy() {
        auto* object=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        auto* occupant=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        auto* pathfinder=TheAI->pathfinder();
        auto context=navigation::MovementValidator::prepare(object,1,false);
        navigation::MovementValidator validator(*pathfinder);
        bool matches=true;
        const auto compare=[&](int x,int y,bool transient) {
            TCheckMovementInfo full{},fast{};
            full.cell={x,y};full.layer=LAYER_GROUND;full.considerTransient=transient;
            fast=full;
            const bool expected=validator.check(context,full);
            const bool actual=validator.check(context,fast,false);
            return expected==actual && full.allyFixedCount==fast.allyFixedCount && full.enemyFixed==fast.enemyFixed;
        };
        const auto& extent=pathfinder->m_extent;
        for (int x:{extent.lo.x-1,extent.lo.x,extent.lo.x+1,extent.hi.x,extent.hi.x+1})
            for (int y:{extent.lo.y-1,extent.lo.y,extent.lo.y+1,extent.hi.y,extent.hi.y+1})
                for (bool transient:{false,true}) matches &= compare(x,y,transient);
        for (int y=1;y<=8;++y) for (int x=1;x<=8;++x) {
            auto* cell=pathfinder->getCell(LAYER_GROUND,x,y);
            const auto previous=cell->getPosUnit();
            const auto previousGoal=cell->getGoalUnit();
            cell->setGoalUnit(INVALID_ID,{x,y});
            cell->setPosUnit(occupant->getID(),{x,y});
            cell->setGoalUnit(occupant->getID(),{x,y});
            matches &= compare(x,y,false) && compare(x+1,y,false) && compare(x,y+1,false) &&
                compare(x+1,y+1,false) && compare(x,y,true);
            cell->setGoalUnit(INVALID_ID,{x,y});
            cell->setPosUnit(previous,{x,y});
            cell->setGoalUnit(previousGoal,{x,y});
        }
        TheGameLogic->destroyObject(occupant);
        TheGameLogic->destroyObject(object);
        TheGameLogic->UPDATE();
        return matches;
    }
    GroupMovement moveRangerAcrossNorthSlope() {
        auto* object = TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        if (!object || !object->getAIUpdateInterface()) throw std::runtime_error("Missing slope unit");
        Coord3D start{2865,145,0}, goal{3860.5f,710.5f,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        goal.z=TheTerrainLogic->getGroundHeight(goal.x,goal.y);
        object->setPosition(&start);
        auto* ai=object->getAIUpdateInterface();
        ai->aiMoveToPosition(&goal,CMD_FROM_PLAYER);
        GroupMovement result;
        result.created=1;
        std::string previous;
        for (unsigned frame=0;frame<600;++frame) {
            TheGameLogic->UPDATE();
            std::ostringstream state;
            state << "state=" << ai->getCurrentStateID() << " waiting=" << ai->isWaitingForPath()
                << " retry=" << ai->getRetryPath();
            if (auto* path=ai->getPath()) {
                if (const auto* last=path->getLastNode())
                    state << " pathGoal=" << last->getPosition()->x << ',' << last->getPosition()->y;
            }
            if (previous!=state.str() && result.stalled.size()<32) {
                previous=state.str();
                result.stalled.push_back("frame="+std::to_string(frame)+" "+previous);
            }
        }
        const auto& end=*object->getPosition();
        result.alive=1;
        result.advanced=std::hypot(end.x-start.x,end.y-start.y)>100;
        result.closer=std::hypot(end.x-goal.x,end.y-goal.y)+100<std::hypot(start.x-goal.x,start.y-goal.y);
        result.abandoned=ai->getCurrentStateID()==AI_IDLE && std::hypot(end.x-goal.x,end.y-goal.y)>100;
        return result;
    }
    bool adjustsRangerDestinationForClearance() {
        auto* object=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        if (!object || !object->getAIUpdateInterface()) throw std::runtime_error("Missing clearance unit");
        Coord3D start{2865,145,0}, goal{3860.5f,710.5f,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        object->setPosition(&start);
        auto* pathfinder=TheAI->pathfinder();
        unsigned examined=0;
        bool clear=true;
        for (int y=40;y<100 && clear;++y) for (int x=340;x<400 && clear;++x) {
            const auto* cell=pathfinder->getCell(LAYER_GROUND,x,y);
            if (!cell || cell->getType()!=PathfindCell::CELL_CLEAR ||
                pathfinder->clearCellForDiameter(false,x,y,LAYER_GROUND,2)>=2) continue;
            ++examined;
            goal={x*10.0f+0.5f,y*10.0f+0.5f,0};
            if (!pathfinder->adjustDestination(object,
                object->getAIUpdateInterface()->getLocomotorSet(),&goal,nullptr)) continue;
            // A Ranger occupies a two-cell-wide footprint, centered on a grid corner.
            const int adjustedX=int((goal.x+5)/10),adjustedY=int((goal.y+5)/10);
            clear=pathfinder->clearCellForDiameter(false,adjustedX,adjustedY,LAYER_GROUND,2)>=2;
        }
        TheGameLogic->destroyObject(object);
        TheGameLogic->UPDATE();
        if (!examined) throw std::runtime_error("Twilight cliff-edge regression has no candidates");
        return clear;
    }
    DrivingStability driveClearParallelLane(const char* typeName,unsigned count,float initialAngle) {
        auto* type=TheThingFactory->findTemplate(typeName);
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        std::vector<Object*> units;
        std::vector<float> lastAngle,lastTurn;
        std::vector<float> startX;
        std::vector<unsigned> reversals(count,0);
        DrivingStability result;
        AIGroupPtr group=count>2?TheAI->createGroup():nullptr;
        for (unsigned i=0;i<count;++i) {
            auto* unit=TheThingFactory->newObject(type,team);
            Coord3D start{3125-float(i/4)*40,385+float(i%4)*28,0},goal{3425,start.y,0};
            start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
            goal.z=TheTerrainLogic->getGroundHeight(goal.x,goal.y);
            unit->setPosition(&start);unit->setOrientation(initialAngle);
            const auto& locomotors=unit->getAIUpdateInterface()->getLocomotorSet();
            if (!TheAI->pathfinder()->isLinePassable(unit,locomotors.getValidSurfaces(),LAYER_GROUND,
                start,goal,false,true)) throw std::runtime_error("Clear-lane driving fixture is obstructed");
            if (group) group->add(unit);
            else unit->getAIUpdateInterface()->aiMoveToPosition(&goal,CMD_FROM_PLAYER);
            units.push_back(unit);lastAngle.push_back(initialAngle);lastTurn.push_back(0);
            startX.push_back(start.x);
        }
        if (group) {
            Coord3D goal{3425,425,0};goal.z=TheTerrainLogic->getGroundHeight(goal.x,goal.y);
            group->groupMoveToPosition(&goal,false,CMD_FROM_PLAYER);
        }
        for (unsigned frame=0;frame<120;++frame) {
            TheGameLogic->UPDATE();
            for (unsigned i=0;i<count;++i) {
                auto* unit=units[i];
                const float angle=unit->getOrientation();
                const float turn=std::remainder(angle-lastAngle[i],6.283185307179586f);
                if (frame>30 && unit->getPosition()->x<3380) {
                    result.maximumTurn=std::max(result.maximumTurn,std::abs(turn));
                    if (std::abs(turn)>.02f) {
                        if (turn*lastTurn[i]<0) {
                            ++result.reversals;
                            result.maximumReversals=std::max(result.maximumReversals,++reversals[i]);
                            if (reversals[i]>2 && result.details.size()<24) {
                                std::ostringstream detail;
                                const auto& position=*unit->getPosition();
                                const auto& velocity=*unit->getPhysics()->getVelocity();
                                detail << "frame=" << frame << " unit=" << i << " position=" << position.x << ',' << position.y
                                    << " heading=" << angle << " turn=" << turn << " velocity=" << velocity.x << ',' << velocity.y;
                                auto* ai=unit->getAIUpdateInterface();
                                detail << " waiting=" << ai->isWaitingForPath() << " state=" << ai->getCurrentStateID();
                                if (auto* path=ai->getPath()) {
                                    detail << " route=";
                                    for (const auto* node=path->getFirstNode();node;node=node->getNextOptimized())
                                        detail << node->getPosition()->x << ',' << node->getPosition()->y << ';';
                                }
                                result.details.push_back(detail.str());
                            }
                        }
                        lastTurn[i]=turn;
                    }
                    if (unit->getAIUpdateInterface()->getNumFramesBlocked()>0) ++result.blocked;
                }
                lastAngle[i]=angle;
            }
        }
        result.progress=10000;
        for (unsigned i=0;i<units.size();++i) {
            auto* unit=units[i];
            result.progress=std::min(result.progress,unit->getPosition()->x-startX[i]);
            TheGameLogic->destroyObject(unit);
        }
        TheGameLogic->UPDATE();
        return result;
    }
    bool followsProtectedCorner() {
        auto* object=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        if (!object || !object->getAIUpdateInterface()) throw std::runtime_error("Missing corner-following unit");
        Coord3D first{3125,385,0},corner{3135,385,0},last{3135,405,0},position{3132,385,0};
        for (auto* p : {&first,&corner,&last,&position}) p->z=TheTerrainLogic->getGroundHeight(p->x,p->y);
        object->setPosition(&position);
        const auto& locomotors=object->getAIUpdateInterface()->getLocomotorSet();
        if (!TheAI->pathfinder()->isLinePassable(object,locomotors.getValidSurfaces(),LAYER_GROUND,
            position,corner,false,true)) throw std::runtime_error("Corner regression approach is obstructed");
        bool matches=true;
        auto* path=newInstance(Path);
        path->appendNode(&first,LAYER_GROUND);
        path->appendNode(&corner,LAYER_GROUND);
        path->appendNode(&last,LAYER_GROUND);
        auto* bend=path->getFirstNode()->getNext();
        path->getFirstNode()->setNextOptimized(bend);
        bend->setNextOptimized(path->getLastNode());
        path->markOptimized();
        // Change metadata on the same cached route at an unchanged position.
        for (const bool optimize : {false,true,false,true}) {
            bend->setCanOptimize(optimize);
            ClosestPointOnPathInfo result{};
            path->computePointOnPath(object,locomotors,position,result);
            matches=matches && result.posOnPath.x==corner.x && result.posOnPath.y==(optimize?395.0f:corner.y);
        }
        deleteInstance(path);
        TheGameLogic->destroyObject(object);
        TheGameLogic->UPDATE();
        return matches;
    }
    bool dockAdmissionsRespectArrival(bool roundTrip=false) {
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        auto* warehouse=TheThingFactory->newObject(TheThingFactory->findTemplate("SupplyDock"),team);
        Coord3D location{3150,385,TheTerrainLogic->getGroundHeight(3150,385)};
        warehouse->setPosition(&location);
        auto* dock=static_cast<SupplyWarehouseDockUpdate*>(warehouse->findUpdateModule(
            TheNameKeyGenerator->nameToKey("SupplyWarehouseDockUpdate")));
        auto* first=TheThingFactory->newObject(TheThingFactory->findTemplate("ChinaVehicleSupplyTruck"),team);
        auto* waiting=TheThingFactory->newObject(TheThingFactory->findTemplate("ChinaVehicleSupplyTruck"),team);
        location.x+=150;first->setPosition(&location);location.y+=40;waiting->setPosition(&location);
        ThePartitionManager->update();
        Coord3D goal;int firstIndex=-1,waitingIndex=-1;
        if (!dock->reserveApproachPosition(first,&goal,&firstIndex) ||
            !dock->reserveApproachPosition(waiting,&goal,&waitingIndex))
            throw std::runtime_error("Dock fairness fixture could not reserve both approaches");
        dock->onApproachReached(first);dock->onApproachReached(waiting);dock->update();
        bool fair=dock->isClearToEnter(first);
        dock->cancelDock(first);
        // The returning truck can reuse the low numbered physical slot, but
        // must not jump ahead of a truck that has already been waiting.
        if (!dock->reserveApproachPosition(first,&goal,&firstIndex))
            throw std::runtime_error("Returning truck could not reserve a free approach");
        dock->onApproachReached(first);
        dock->onApproachReached(first);
        dock->onApproachReached(waiting);
        if (roundTrip) {
            const auto file=(dataRoot/"dock-admission.snapshot").string();
            XferSave save;save.open(file.c_str());save.xferSnapshot(dock);save.close();
            dock->cancelDock(first);dock->cancelDock(waiting);
            const auto schedulerSlot=dock->friend_getIndexInLogic();
            XferLoad load;load.setOptions(XO_NO_POST_PROCESSING);
            load.open(file.c_str());load.xferSnapshot(dock);load.close();
            // A whole-match load rebuilds scheduler ownership in GameLogic's
            // postprocess. This isolated module round-trip keeps its live slot.
            dock->friend_setIndexInLogic(schedulerSlot);
        }
        dock->update();
        fair &= dock->isClearToEnter(waiting) && !dock->isClearToEnter(first);
        dock->cancelDock(waiting);dock->update();
        fair &= dock->isClearToEnter(first);
        dock->cancelDock(first);
        dock->update();
        fair &= !dock->isClearToEnter(first) && !dock->isClearToEnter(waiting);
        // A displaced boneless-dock truck must be directed back into the
        // existing pickup range. Once in range, entering stays a no-op.
        dock->getEnterPosition(first,&goal);
        const Coord3D displaced=*first->getPosition();
        fair &= goal.x!=displaced.x || goal.y!=displaced.y;
        first->setPosition(&goal);ThePartitionManager->update();
        const Real pickupRange=2*first->getGeometryInfo().getBoundingCircleRadius();
        fair &= ThePartitionManager->getDistanceSquared(first,warehouse,FROM_BOUNDINGSPHERE_2D)<=sqr(pickupRange);
        Coord3D unchanged;dock->getEnterPosition(first,&unchanged);
        fair &= unchanged.x==goal.x && unchanged.y==goal.y;
        TheGameLogic->destroyObject(first);TheGameLogic->destroyObject(waiting);
        TheGameLogic->destroyObject(warehouse);TheGameLogic->UPDATE();
        return fair;
    }
    std::vector<std::string> supplyTrucksCompleteDockCycles(unsigned count,unsigned cycles=1) {
        auto* player=ThePlayerList->getLocalPlayer();
        auto* team=player->getDefaultTeam();
        const auto create=[&](const char* name,float x,float y) {
            auto* type=TheThingFactory->findTemplate(name);
            if (!type) throw std::runtime_error(std::string("Missing dock fixture template: ")+name);
            auto* object=TheThingFactory->newObject(type,team);
            Coord3D position{x,y,TheTerrainLogic->getGroundHeight(x,y)};
            object->setPosition(&position);
            for (BehaviorModule** module=object->getBehaviorModules();*module;++module)
                if (auto* create=(*module)->getCreate()) create->onBuildComplete();
            TheAI->pathfinder()->addObjectToPathfindMap(object);
            return object;
        };
        auto* warehouse=create("SupplyDock",3150,385);
        auto* stock=static_cast<SupplyWarehouseDockUpdate*>(warehouse->findUpdateModule(
            TheNameKeyGenerator->nameToKey("SupplyWarehouseDockUpdate")));
        if (!stock) throw std::runtime_error("Supply fixture has no warehouse inventory");
        // Faster trucks keep harvesting while the slower ones finish. Inventory
        // exhaustion must not masquerade as a movement failure on repeated runs.
        if (cycles>1) stock->setCashValue(1000000);
        auto* center=create("ChinaSupplyCenter",3500,385);
        std::vector<Object*> trucks;
        std::vector<bool> pickedUp(count);
        std::vector<unsigned> delivered(count);
        std::vector<unsigned> admissions(count),lastAdmission(count),maximumReadyWait(count),readySince(count);
        ObjectID previousActive=INVALID_ID;
        std::vector<int> previous(count);
        for (unsigned i=0;i<count;++i) {
            auto* truck=create("ChinaVehicleSupplyTruck",3295+40*(i%4),515+40*(i/4));
            if (!truck->getAIUpdateInterface()->getSupplyTruckAIInterface())
                throw std::runtime_error("Dock fixture truck has no harvesting interface");
            trucks.push_back(truck);
        }
        ThePartitionManager->update();
        for (auto* truck:trucks) truck->getAIUpdateInterface()->aiDock(warehouse,CMD_FROM_PLAYER);
        const auto before=player->getMoney()->countMoney();
        // Both docks admit one truck at a time. Scale the finite progress
        // deadline with the requested deliveries, including travel, loading,
        // unloading and queue recovery (the dock's own timeout is 30 seconds).
        const unsigned deadline=count*cycles*30*LOGICFRAMES_PER_SECOND;
        for (unsigned frame=0;frame<deadline;++frame) {
            TheGameLogic->UPDATE();
            bool complete=true;
            for (unsigned i=0;i<count;++i) {
                if (stock->m_activeDocker==trucks[i]->getID() && stock->m_activeDocker!=previousActive) {
                    ++admissions[i];lastAdmission[i]=frame;
                }
                const bool ready=std::find(stock->m_readyDockers.begin(),stock->m_readyDockers.end(),trucks[i]->getID())!=stock->m_readyDockers.end();
                if (ready) {
                    if (!readySince[i]) readySince[i]=frame+1;
                    maximumReadyWait[i]=std::max(maximumReadyWait[i],frame+2-readySince[i]);
                } else readySince[i]=0;
                const auto boxes=trucks[i]->getAIUpdateInterface()->getSupplyTruckAIInterface()->getNumberBoxes();
                if (boxes>0) pickedUp[i]=true;
                if (boxes==0 && previous[i]>0) ++delivered[i];
                previous[i]=boxes;
                complete=complete && delivered[i]>=cycles;
            }
            previousActive=stock->m_activeDocker;
            if (complete) break;
        }
        std::vector<std::string> failures;
        if (stock->getBoxesStored()==0) failures.push_back("Supply fixture exhausted warehouse inventory");
        const bool unfinished=std::any_of(delivered.begin(),delivered.end(),[&](unsigned n) { return n<cycles; });
        for (unsigned i=0;i<count && unfinished;++i) {
            auto* truck=trucks[i];auto* ai=truck->getAIUpdateInterface();
            const auto& position=*truck->getPosition();
            std::ostringstream message;
            message << "truck=" << i << " picked=" << pickedUp[i] << " deliveries=" << delivered[i] << '/' << cycles
                << " boxes=" << previous[i]
                << " admissions=" << admissions[i] << " last_admission=" << lastAdmission[i]
                << " maximum_ready_wait=" << maximumReadyWait[i]
                << " state=" << ai->getCurrentStateID() << " blocked=" << ai->getNumFramesBlocked()
                << " waiting=" << ai->isWaitingForPath() << " position=" << position.x << ',' << position.y
                << " goal=" << ai->getGoalPosition()->x << ',' << ai->getGoalPosition()->y
                << " ignored=" << unsigned(ai->getIgnoredObstacleID()) << " through=" << ai->canPathThroughUnits()
                << " warehouse_active=" << stock->isClearToEnter(truck)
                << " center_active=" << center->getDockUpdateInterface()->isClearToEnter(truck)
                << " z=" << position.z << " ground=" << TheTerrainLogic->getGroundHeight(position.x,position.y);
            if (auto* path=ai->getPath()) message << " pathEnd=" << path->getLastNode()->getPosition()->x
                << ',' << path->getLastNode()->getPosition()->y;
            const auto slot=std::find(stock->m_approachPositionOwners.begin(),stock->m_approachPositionOwners.end(),truck->getID());
            if (slot!=stock->m_approachPositionOwners.end()) {
                const auto index=std::size_t(slot-stock->m_approachPositionOwners.begin());
                message << " approach=" << stock->m_approachPositions[index].x << ',' << stock->m_approachPositions[index].y
                    << " reached=" << bool(stock->m_approachPositionReached[index]);
            }
            message << " ready=" << (std::find(stock->m_readyDockers.begin(),stock->m_readyDockers.end(),truck->getID())!=stock->m_readyDockers.end());
            failures.push_back(message.str());
        }
        if (player->getMoney()->countMoney()<=before) failures.push_back("No supplies delivered to the player");
        for (auto* truck:trucks) TheGameLogic->destroyObject(truck);
        TheGameLogic->destroyObject(center);TheGameLogic->destroyObject(warehouse);
        TheGameLogic->UPDATE();
        return failures;
    }
    bool weightedRoutesPreserveAllyYieldFlag() {
        auto* pf=TheAI->pathfinder();
        auto* type=TheThingFactory->findTemplate("AmericaVehicleHumvee");
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        auto* mover=TheThingFactory->newObject(type,team);
        auto* ally=TheThingFactory->newObject(type,team);
        Coord3D from{3125,385,0},to{3225,385,0};
        mover->setPosition(&from);
        auto* cell=pf->getCell(LAYER_GROUND,317,38);
        const auto previousUnit=cell->getPosUnit(),previousGoal=cell->getGoalUnit();
        const auto bounds=pf->m_logicalExtent;
        cell->setGoalUnit(INVALID_ID,{317,38});
        cell->setPosUnit(ally->getID(),{317,38});
        cell->setGoalUnit(ally->getID(),{317,38});
        if (cell->getFlags()!=PathfindCell::UNIT_PRESENT_FIXED)
            throw std::runtime_error("Yield fixture requires a fixed allied occupant");
        pf->m_logicalExtent={{312,38},{322,38}};
        GroundRoutePlanner planner(*pf);
        Pathfinder::GroundRouteQuery query;
        query.object=mover;query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        bool valid=true;
        for (bool through:{false,true}) {
            mover->getAIUpdateInterface()->setCanPathThroughUnits(through);
            GroundRoutePlanner::WeightedResult route(planner.findWeighted(query,&from,&to));
            if (!route.path) throw std::runtime_error("Yield flag fixture has no corridor route");
            valid=valid && (route.path->getBlockedByAlly()!=0)==!through;
        }
        pf->m_logicalExtent=bounds;
        cell->setGoalUnit(INVALID_ID,{317,38});
        cell->setPosUnit(previousUnit,{317,38});
        cell->setGoalUnit(previousGoal,{317,38});
        TheGameLogic->destroyObject(ally);TheGameLogic->destroyObject(mover);
        TheGameLogic->UPDATE();
        return valid;
    }
    bool dozerCanPlanThroughFriendlyConstruction() {
        auto* pf=TheAI->pathfinder();
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        auto* obstacle=TheThingFactory->newObject(TheThingFactory->findTemplate("ChinaSupplyCenter"),team);
        auto* cell=pf->getCell(LAYER_GROUND,317,38);
        if (cell->getType()!=PathfindCell::CELL_CLEAR)
            throw std::runtime_error("Dozer exception fixture requires clear terrain");
        if (!cell->setTypeAsObstacle(obstacle,false,{317,38}))
            throw std::runtime_error("Dozer exception obstacle was not inserted");
        const auto bounds=pf->m_logicalExtent;
        pf->m_logicalExtent={{312,38},{322,38}};
        bool valid=true;
        for (const char* name:{"ChinaVehicleDozer","AmericaVehicleHumvee"}) {
            auto* mover=TheThingFactory->newObject(TheThingFactory->findTemplate(name),team);
            Coord3D from{3125,385,0},to{3225,385,0};mover->setPosition(&from);
            Pathfinder::GroundRouteQuery query;query.object=mover;
            GroundRoutePlanner planner(*pf);
            GroundRoutePlanner::WeightedResult route(planner.findWeighted(query,&from,&to));
            valid=valid && bool(route.path)==bool(mover->isKindOf(KINDOF_DOZER));
            TheGameLogic->destroyObject(mover);
        }
        pf->m_logicalExtent=bounds;
        cell->removeObstacle(obstacle);
        TheGameLogic->destroyObject(obstacle);
        TheGameLogic->UPDATE();
        return valid;
    }
    bool legacySpecialRouteContracts() {
        auto* pf=TheAI->pathfinder();
        auto* type=TheThingFactory->findTemplate("AmericaInfantryRanger");
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        auto* mover=TheThingFactory->newObject(type,team);
        auto* victim=TheThingFactory->newObject(type,team);
        Coord3D start{3125,385,0},target{3425,385,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        target.z=TheTerrainLogic->getGroundHeight(target.x,target.y);
        mover->setPosition(&start);victim->setPosition(&target);
        ThePartitionManager->update();
        const auto& locomotors=mover->getAIUpdateInterface()->getLocomotorSet();
        using OwnedPath=std::unique_ptr<Path,GroundRoutePlanner::WeightedResult::DeletePath>;
        const auto safePath=[&](const Coord3D* first,const Coord3D* second,float radius) {
            // Establish the modern oracle from a fresh captured snapshot.
            // The preceding fixture cases may have retained a planner cache
            // across native object edits; comparing that stale cache with a
            // newly constructed planner made this test report a false
            // legacy-coordinate regression.
            resetNavigationPlanner();
            OwnedPath reference(pf->findSafePath(mover,locomotors,&start,first,second,radius));
            const auto work=pf->m_cumulativeCellsAllocated;
            for (unsigned repetition=0;repetition<3;++repetition) {
                resetNavigationPlanner();
                struct Restore {
                    Pathfinder* pf;
                    ~Restore() { pf->m_deferGroundQueries=FALSE; }
                } restore{pf};
                pf->m_deferGroundQueries=TRUE;
                OwnedPath actual;
                for (unsigned slice=0;slice<10000;++slice) {
                    pf->m_cumulativeCellsAllocated=0;
                    actual.reset(pf->findSafePath(mover,locomotors,&start,first,second,radius));
                    if (!pf->isGroundPathPending(mover->getID())) break;
                    pf->m_groundPlanner->commitCapturedSlice();
                    pf->m_groundPlanner->advanceCapturedSlice();

                }
                if (pf->isGroundPathPending(mover->getID()) || bool(actual)!=bool(reference)) {
                    std::ostringstream failure;
                    failure << "Captured flee request failed to match synchronous completion"
                        << " repetition=" << repetition
                        << " pending=" << pf->isGroundPathPending(mover->getID())
                        << " actual=" << bool(actual) << " reference=" << bool(reference);
                    throw std::runtime_error(failure.str());
                }
                const auto* expected=reference?reference->getFirstNode():nullptr;
                const auto* observed=actual?actual->getFirstNode():nullptr;
                while (expected && observed) {
                    const auto& a=*expected->getPosition();const auto& b=*observed->getPosition();
                    if (a.x!=b.x || a.y!=b.y || a.z!=b.z || expected->getLayer()!=observed->getLayer()) {
                        std::ostringstream failure;
                        failure << "Captured flee route changed native coordinates or layer"
                            << " repetition=" << repetition << " expected=" << a.x << ',' << a.y << ',' << a.z
                            << " observed=" << b.x << ',' << b.y << ',' << b.z;
                        throw std::runtime_error(failure.str());
                    }
                    expected=expected->getNext();observed=observed->getNext();
                }
                if (expected || observed) throw std::runtime_error("Captured flee route changed native node count");
            }
            pf->m_cumulativeCellsAllocated=work;
            return reference;
        };
        Coord3D threat=start;threat.x+=20;
        // Landing has stricter terrain rules than movement, regardless of the
        // incoming unit's ability to cross water or cliffs.
        const ICoord2D landingCoordinate{320,38};
        auto* landingCell=pf->getCell(LAYER_GROUND,landingCoordinate.x,landingCoordinate.y);
        const auto landingType=landingCell->getType();
        const auto landingPosition=landingCell->getPosUnit(),landingGoal=landingCell->getGoalUnit();
        landingCell->setGoalUnit(INVALID_ID,landingCoordinate);
        landingCell->setPosUnit(INVALID_ID,landingCoordinate);
        bool landingValid=true;
        for (auto terrain:{PathfindCell::CELL_CLEAR,PathfindCell::CELL_CLIFF,
                PathfindCell::CELL_WATER,PathfindCell::CELL_IMPASSABLE}) {
            landingCell->setType(terrain);
            Coord3D destination{1,2,3};
            const bool accepted=pf->checkForLanding(320,38,LAYER_GROUND,0,true,&destination);
            landingValid &= accepted==(terrain==PathfindCell::CELL_CLEAR);
            if (!accepted) landingValid &= destination.x==1 && destination.y==2 && destination.z==3;
        }
        landingCell->setType(PathfindCell::CELL_CLEAR);
        landingCell->setPosUnit(victim->getID(),landingCoordinate);
        landingCell->setGoalUnit(victim->getID(),landingCoordinate);
        Coord3D occupiedLanding{1,2,3};
        landingValid &= !pf->checkForLanding(320,38,LAYER_GROUND,0,true,&occupiedLanding);
        landingCell->setGoalUnit(INVALID_ID,landingCoordinate);
        landingCell->setPosUnit(landingPosition,landingCoordinate);
        landingCell->setGoalUnit(landingGoal,landingCoordinate);
        landingCell->setType(landingType);
        if (!landingValid) throw std::runtime_error("Landing accepted unsafe terrain/reservations or changed a rejected destination");
        OwnedPath safe=safePath(&start,&threat,60);
        if (!safe || !safe->getLastNode()) throw std::runtime_error("Two-threat safe path missing");
        const auto safeEnd=*safe->getLastNode()->getPosition();
        if (std::hypot(safeEnd.x-start.x,safeEnd.y-start.y)<=60 ||
            std::hypot(safeEnd.x-threat.x,safeEnd.y-threat.y)<=60)
            throw std::runtime_error("Safe route did not clear both threats");
        ICoord2D reservedCoordinate;pf->worldToCell(&safeEnd,&reservedCoordinate);
        auto* reserved=pf->getCell(LAYER_GROUND,reservedCoordinate.x,reservedCoordinate.y);
        const auto savedPosition=reserved->getPosUnit(),savedGoal=reserved->getGoalUnit();
        reserved->setGoalUnit(INVALID_ID,reservedCoordinate);
        reserved->setPosUnit(victim->getID(),reservedCoordinate);
        reserved->setGoalUnit(victim->getID(),reservedCoordinate);
        OwnedPath alternate=safePath(&start,&threat,60);
        reserved->setGoalUnit(INVALID_ID,reservedCoordinate);
        reserved->setPosUnit(savedPosition,reservedCoordinate);
        reserved->setGoalUnit(savedGoal,reservedCoordinate);
        if (!alternate || !alternate->getLastNode()) throw std::runtime_error("Occupied flee goal prevented alternatives");
        const auto alternateEnd=*alternate->getLastNode()->getPosition();
        if (alternateEnd.x==safeEnd.x && alternateEnd.y==safeEnd.y)
            throw std::runtime_error("Flee path reused an occupied allied destination");
        const auto workBefore=pf->m_cumulativeCellsAllocated;
        OwnedPath bounded=safePath(&start,&threat,1000000);
        if (!bounded || pf->m_cumulativeCellsAllocated-workBefore>8000)
            throw std::runtime_error("Unattainable flee radius caused an unbounded search");
        // An already safe unit may stay put; an enclosed unit still gets a
        // reachable escape point even when neither threat radius can be cleared.
        const Coord3D distantThreat{4500,4500,0};
        OwnedPath alreadySafe=safePath(&distantThreat,&distantThreat,60);
        if (!alreadySafe || std::hypot(alreadySafe->getLastNode()->getPosition()->x-start.x,
                alreadySafe->getLastNode()->getPosition()->y-start.y)>10)
            throw std::runtime_error("Already safe unit was sent away unnecessarily");
        std::vector<std::pair<PathfindCell*,PathfindCell::CellType>> wall;
        for (int x=309;x<=315;++x) for (int y=35;y<=41;++y) {
            if (x!=309 && x!=315 && y!=35 && y!=41) continue;
            auto* cell=pf->getCell(LAYER_GROUND,x,y);
            wall.emplace_back(cell,cell->getType());
            cell->setType(PathfindCell::CELL_CLIFF);
        }
        OwnedPath enclosed=safePath(&start,&threat,1000);
        for (const auto& [cell,type]:wall) cell->setType(type);
        pf->invalidateNavigationTopology();
        if (!enclosed || !enclosed->getLastNode()) throw std::runtime_error("Enclosed flee path missing");
        const auto enclosedEnd=*enclosed->getLastNode()->getPosition();
        if (std::hypot(enclosedEnd.x-start.x,enclosedEnd.y-start.y)<5 ||
            enclosedEnd.x<3100 || enclosedEnd.x>=3150 || enclosedEnd.y<360 || enclosedEnd.y>=410)
            throw std::runtime_error("Enclosed flee route did not make reachable progress");
        auto* weapon=mover->getCurrentWeapon();
        if (!weapon) throw std::runtime_error("Attack fixture has no weapon");
        // Retain the pre-optimization, materialized endpoint-set oracle. The
        // production query now checks endpoints lazily; its complete route
        // must still match this independently enumerated destination set.
        Pathfinder::GroundRouteQuery attackQuery;
        attackQuery.object=mover;attackQuery.acceptableSurfaces=locomotors.getValidSurfaces();
        attackQuery.allowReservedGoal=true;
        pf->getRadiusAndCenter(mover,attackQuery.radius,attackQuery.centerInCell);
        attackQuery.isHuman=mover->getControllingPlayer()->getPlayerType()!=PLAYER_COMPUTER;
        std::vector<std::array<int,3>> firingPositions;
        for (int layer=LAYER_GROUND;layer<=LAYER_LAST;++layer) {
            if (layer!=LAYER_GROUND && pf->m_layers[layer].isUnused()) continue;
            for (int x=pf->m_extent.lo.x;x<=pf->m_extent.hi.x;++x)
                for (int y=pf->m_extent.lo.y;y<=pf->m_extent.hi.y;++y) {
                    const auto candidateLayer=PathfindLayerEnum(layer);
                    auto* cell=pf->getCell(candidateLayer,x,y);
                    if (!cell) continue;
                    Coord3D point;pf->adjustCoordToCell(x,y,attackQuery.centerInCell,point,candidateLayer);
                    if (sqr(point.x-start.x)+sqr(point.y-start.y)<25 ||
                        !pf->validMovementPosition(FALSE,attackQuery.acceptableSurfaces,cell) ||
                        !pf->checkDestination(mover,x,y,candidateLayer,attackQuery.radius,attackQuery.centerInCell) ||
                        !weapon->isGoalPosWithinAttackRange(mover,&point,victim,&target) ||
                        pf->isAttackViewBlockedByObstacle(mover,point,victim,target)) continue;
                    firingPositions.push_back({x,y,layer});
                }
        }
        std::sort(firingPositions.begin(),firingPositions.end());
        OwnedPath attackReference(pf->m_groundPlanner->findWeighted(attackQuery,&start,&target,false,
            [&](int x,int y,PathfindLayerEnum layer) {
                return std::binary_search(firingPositions.begin(),firingPositions.end(),std::array<int,3>{x,y,int(layer)});
            },{},{},nullptr,false,nullptr,nullptr,nullptr,nullptr,2500));
        OwnedPath attack(pf->findAttackPath(mover,locomotors,&start,victim,&target,weapon));
        if (!attack || !attack->getLastNode()) throw std::runtime_error("Attack approach missing");
        if (!attackReference) throw std::runtime_error("Materialized attack oracle missing");
        auto* expectedNode=attackReference->getFirstNode();
        auto* actualNode=attack->getFirstNode();
        while (expectedNode && actualNode) {
            if (expectedNode->getLayer()!=actualNode->getLayer() ||
                *expectedNode->getPosition()!=*actualNode->getPosition())
                throw std::runtime_error("Lazy attack query changed its raw route");
            expectedNode=expectedNode->getNext();actualNode=actualNode->getNext();
        }
        if (expectedNode || actualNode) throw std::runtime_error("Lazy attack query changed its route length");
        const auto previouslyDeferred=pf->groundQueriesDeferred();
        pf->setGroundQueriesDeferred(true);
        OwnedPath slicedAttack;
        for (unsigned slice=0;slice<256;++slice) {
            // The request must own this coordinate after the call returns.
            Coord3D temporaryTarget=target;
            slicedAttack.reset(pf->findAttackPath(mover,locomotors,&start,victim,&temporaryTarget,weapon));
            temporaryTarget={-10000,-10000,-10000};
            if (!pf->isGroundPathPending(mover->getID())) break;
            pf->m_groundPlanner->advanceCapturedSlice();
            pf->m_groundPlanner->commitCapturedSlice();
        }
        pf->setGroundQueriesDeferred(previouslyDeferred);
        if (!slicedAttack || pf->isGroundPathPending(mover->getID()))
            throw std::runtime_error("Sliced attack query failed to complete");
        expectedNode=attackReference->getFirstNode();actualNode=slicedAttack->getFirstNode();
        while (expectedNode && actualNode) {
            if (expectedNode->getLayer()!=actualNode->getLayer() ||
                *expectedNode->getPosition()!=*actualNode->getPosition())
                throw std::runtime_error("Sliced attack query changed its raw route");
            expectedNode=expectedNode->getNext();actualNode=actualNode->getNext();
        }
        if (expectedNode || actualNode) throw std::runtime_error("Sliced attack query changed route length");
        const auto attackEnd=*attack->getLastNode()->getPosition();
        if (!weapon->isGoalPosWithinAttackRange(mover,&attackEnd,victim,&target) ||
            std::hypot(attackEnd.x-start.x,attackEnd.y-start.y)<5 ||
            pf->isAttackViewBlockedByObstacle(mover,attackEnd,victim,target))
            throw std::runtime_error("Attack route violates range, progress or visibility");
        ICoord2D firingCoordinate;pf->worldToCell(&attackEnd,&firingCoordinate);
        auto* firingCell=pf->getCell(LAYER_GROUND,firingCoordinate.x,firingCoordinate.y);
        const auto firingPosition=firingCell->getPosUnit(),firingGoal=firingCell->getGoalUnit();
        firingCell->setGoalUnit(INVALID_ID,firingCoordinate);
        firingCell->setPosUnit(victim->getID(),firingCoordinate);
        firingCell->setGoalUnit(victim->getID(),firingCoordinate);
        OwnedPath alternateAttack(pf->findAttackPath(mover,locomotors,&start,victim,&target,weapon));
        firingCell->setGoalUnit(INVALID_ID,firingCoordinate);
        firingCell->setPosUnit(firingPosition,firingCoordinate);
        firingCell->setGoalUnit(firingGoal,firingCoordinate);
        if (!alternateAttack || !alternateAttack->getLastNode())
            throw std::runtime_error("Occupied firing position prevented an alternative approach");
        const auto alternateAttackEnd=*alternateAttack->getLastNode()->getPosition();
        if ((alternateAttackEnd.x==attackEnd.x && alternateAttackEnd.y==attackEnd.y) ||
            !weapon->isGoalPosWithinAttackRange(mover,&alternateAttackEnd,victim,&target))
            throw std::runtime_error("Attack approach reused an occupied position or lost range");
        OwnedPath groundAttack(pf->findAttackPath(mover,locomotors,&start,nullptr,&target,weapon));
        if (!groundAttack || !groundAttack->getLastNode())
            throw std::runtime_error("Ground-position attack approach missing");
        const auto groundAttackEnd=*groundAttack->getLastNode()->getPosition();
        if (!weapon->isGoalPosWithinAttackRange(mover,&groundAttackEnd,nullptr,&target) ||
            pf->isAttackViewBlockedByObstacle(mover,groundAttackEnd,nullptr,target))
            throw std::runtime_error("Ground-position attack violates range or visibility");
        OwnedPath original(newInstance(Path));
        original->appendNode(&start,LAYER_GROUND);
        // Repair is a local query in DX9 (2,000 admitted neighbours). Give
        // the synthetic original route a nearby, correctly aligned raw node
        // to rejoin, as an actual searched route would have.
        Int repairRadius;Bool repairCentered;
        pf->getRadiusAndCenter(mover,repairRadius,repairCentered);
        Coord3D rejoin;pf->adjustCoordToCell(318,38,repairCentered,rejoin,LAYER_GROUND);
        original->appendNode(&rejoin,LAYER_GROUND);
        original->appendNode(&target,LAYER_GROUND);
        auto* originalSecond=original->getFirstNode()->getNext();
        for (bool blocked:{false,true}) {
            OwnedPath patch(pf->patchPath(mover,locomotors,original.get(),blocked));
            if (!patch || !patch->getLastNode()) throw std::runtime_error("Path repair missing");
            const auto end=*patch->getLastNode()->getPosition();
            if (std::hypot(end.x-target.x,end.y-target.y)>10 ||
                original->getFirstNode()->getNext()!=originalSecond)
                throw std::runtime_error("Path repair changed its destination or source path");
        }
        safe.reset();attack.reset();original.reset();
        TheGameLogic->destroyObject(victim);TheGameLogic->destroyObject(mover);
        TheGameLogic->UPDATE();
        return true;
    }
    bool patchPathRejoinsBeyondEnclosedNearestNode() {
        auto* pf=TheAI->pathfinder();
        auto* mover=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        Coord3D start{3125,385,0},nearest{3185,385,0},goal{3275,385,0};
        Int radius=0;Bool centered=TRUE;
        pf->getRadiusAndCenter(mover,radius,centered);
        for (auto* point:{&start,&nearest,&goal}) {
            ICoord2D cell;pf->worldToCell(point,&cell);
            pf->adjustCoordToCell(cell.x,cell.y,centered,*point,LAYER_GROUND);
        }
        mover->setPosition(&start);
        std::vector<std::pair<PathfindCell*,PathfindCell::CellType>> changed;
        for (int y=35;y<=41;++y) for (int x=315;x<=321;++x)
            if (x==315 || x==321 || y==35 || y==41) {
                auto* cell=pf->getCell(LAYER_GROUND,x,y);
                changed.emplace_back(cell,cell->getType());cell->setType(PathfindCell::CELL_IMPASSABLE);
            }
        const IRegion2D edited{{315,35},{321,41}};
        pf->invalidateNavigationTopology(edited);
        auto* original=newInstance(Path);
        original->appendNode(&start,LAYER_GROUND);
        original->appendNode(&nearest,LAYER_GROUND);
        original->appendNode(&goal,LAYER_GROUND);
        auto* repaired=pf->patchPath(mover,mover->getAIUpdateInterface()->getLocomotorSet(),original,FALSE);
        bool valid=repaired && repaired->getLastNode()->getPosition()->x==goal.x &&
            repaired->getLastNode()->getPosition()->y==goal.y;
        if (repaired) for (auto* node=repaired->getFirstNode();node;node=node->getNext())
            if (node->getPosition()->x==nearest.x && node->getPosition()->y==nearest.y) valid=false;
        if (repaired) deleteInstance(repaired);
        deleteInstance(original);
        for (const auto [cell,type]:changed) cell->setType(type);
        pf->invalidateNavigationTopology(edited);
        TheGameLogic->destroyObject(mover);TheGameLogic->UPDATE();
        return valid;
    }
    bool patchPathRejoinsUsableTail() {
        auto* pf=TheAI->pathfinder();
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        auto* mover=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),team);
        auto* blocker=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),team);
        if (!mover || !blocker || !mover->getAIUpdateInterface()) throw std::runtime_error("Missing patch fixture mover");
        const auto& locomotors=mover->getAIUpdateInterface()->getLocomotorSet();
        Coord3D start{3125,385,0},blocked{3175,385,0},rejoin{3225,385,0},goal{3275,385,0};
        for (auto* point:{&start,&blocked,&rejoin,&goal})
            point->z=TheTerrainLogic->getGroundHeight(point->x,point->y);
        mover->setPosition(&start);
        blocker->setPosition(&blocked);
        ThePartitionManager->update();

        auto* original=newInstance(Path);
        original->appendNode(&start,LAYER_GROUND);
        original->appendNode(&blocked,LAYER_GROUND);
        original->appendNode(&rejoin,LAYER_GROUND);
        original->appendNode(&goal,LAYER_GROUND);
        auto* repaired=pf->patchPath(mover,locomotors,original,FALSE);
        if (!repaired) throw std::runtime_error("Patch fixture produced no repaired path");
        bool valid=true;
        bool foundRejoin=false,foundGoal=false;
        if (repaired) for (auto* node=repaired->getFirstNode();node;node=node->getNext()) {
            foundRejoin=foundRejoin || (node->getPosition()->x==rejoin.x && node->getPosition()->y==rejoin.y);
            foundGoal=foundGoal || (node->getPosition()->x==goal.x && node->getPosition()->y==goal.y);
        }
        valid=valid && foundRejoin && foundGoal;
        if (repaired) deleteInstance(repaired);
        deleteInstance(original);
        TheGameLogic->destroyObject(blocker);
        TheGameLogic->destroyObject(mover);
        TheGameLogic->UPDATE();
        return valid;
    }
    std::vector<unsigned> specialRequestFrameCRCs() {
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        auto* type=TheThingFactory->findTemplate("AmericaInfantryRanger");
        auto* pf=TheAI->pathfinder();
        std::vector<ObjectID> units;
        for (unsigned i=0;i<32;++i) {
            auto* object=TheThingFactory->newObject(type,team);
            Coord3D start{3125.f+20*(i%8),385.f+20*(i/8),0},goal{3425,585,0};
            start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
            object->setPosition(&start);units.push_back(object->getID());
            auto* ai=object->getAIUpdateInterface();
            ai->aiMoveToPosition(&goal,CMD_FROM_PLAYER);
            if (i%2) ai->requestSafePath(object->getID());
            else ai->requestApproachPath(&goal);
            pf->queueForPath(object->getID());
        }
        std::vector<unsigned> crcs;
        for (unsigned frame=0;frame<600;++frame) {
            if (frame==1) {
                // Replace and destroy requests while their captured searches are pending.
                for (unsigned i=0;i<8;++i) {
                    auto* object=TheGameLogic->findObjectByID(units[i]);
                    if (i<4) TheGameLogic->destroyObject(object);
                    else {
                        Coord3D goal{3325,685,0};
                        object->getAIUpdateInterface()->aiMoveToPosition(&goal,CMD_FROM_PLAYER);
                    }
                }
            }
            TheGameLogic->UPDATE();
            crcs.push_back(TheGameLogic->getCRC(CRC_RECALC));
        }
        for (unsigned i=4;i<units.size();++i) {
            auto* object=TheGameLogic->findObjectByID(units[i]);
            if (!object || object->getAIUpdateInterface()->isWaitingForPath() || pf->isGroundPathPending(units[i])) {
                std::ostringstream failure;
                failure << "Mixed captured requests did not complete: index=" << i << " id=" << unsigned(units[i]);
                if (object) failure << " waiting=" << object->getAIUpdateInterface()->isWaitingForPath()
                    << " pending=" << pf->isGroundPathPending(units[i])
                    << " state=" << object->getAIUpdateInterface()->getCurrentStateID();
                throw std::runtime_error(failure.str());
            }
        }
        return crcs;
    }
    std::vector<ReplayRouteSample> longReplayRoutesOnStaticTerrain(unsigned repeats=16,const char* dumpPath=nullptr,
        bool captured=false) {
        // Coordinates and mover types from the measured replay's long queries.
        // This isolates its real terrain; replay-time traffic is deliberately
        // absent, so these are repeatable kernel workloads, not a full replay.
        struct Input { const char* type;float x,y,toX,toY; };
        const Input inputs[]{
            {"Slth_GLAVehicleQuadCannon",809.417f,1390.406f,380.5f,3550.5f},
            {"Slth_GLAInfantryRebel",660.166f,1200.377f,390.5f,3520.5f},
            {"GLAVehicleTechnicalChassisOne",660.548f,1664.811f,880.5f,3250.5f},
            {"ChinaTankGattling",1804.996f,4024.823f,480.249f,1910.338f},
            {"ChinaTankGattling",1807.941f,3954.264f,480.249f,1910.338f}};
        std::vector<ReplayRouteSample> samples;
        std::ofstream routes;
        if (dumpPath) {
            routes.open(dumpPath);
            if (!routes) throw std::runtime_error("Cannot create terrain route dump");
            routes << std::setprecision(9) << "query,kind,index,x,y,z,layer\n";
        }
        auto* pf=TheAI->pathfinder();
        for (const auto& input:inputs) {
            auto* unit=TheThingFactory->newObject(TheThingFactory->findTemplate(input.type),
                ThePlayerList->getLocalPlayer()->getDefaultTeam());
            Coord3D start{input.x,input.y,TheTerrainLogic->getGroundHeight(input.x,input.y)};
            Coord3D goal{input.toX,input.toY,TheTerrainLogic->getGroundHeight(input.toX,input.toY)};
            unit->setPosition(&start);
            ReplayRouteSample sample;sample.unitType=input.type;
            for (unsigned repeat=0;repeat<repeats;++repeat) {
                pf->m_cumulativeCellsAllocated=0;
                const auto began=std::chrono::steady_clock::now();
                Path* path=nullptr;
                unsigned searchSlices=0,maximumSliceWork=0;
                {
                    struct Restore {
                        Pathfinder* pf;Bool previous;
                        ~Restore() { pf->setGroundQueriesDeferred(previous); }
                    } restore{pf,pf->groundQueriesDeferred()};
                    pf->setGroundQueriesDeferred(captured);
                    for (unsigned poll=0;poll<10000;++poll) {
                        const auto ownerBegan=std::chrono::steady_clock::now();
                        path=pf->findPath(unit,unit->getAIUpdateInterface()->getLocomotorSet(),&start,&goal);
                        sample.maximumOwnerPollMilliseconds=std::max(sample.maximumOwnerPollMilliseconds,
                            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-ownerBegan).count());
                        if (path || !pf->isGroundPathPending(unit->getID())) break;
                        if (pf->m_groundPlanner->state_->completedSlice) {
                            // A completed slice still awaits the explicit commit
                            // boundary. Polling must neither publish nor inspect
                            // the changing live map again.
                            const auto beforePoll=pf->m_cumulativeCellsAllocated;
                            auto* premature=pf->findPath(unit,unit->getAIUpdateInterface()->getLocomotorSet(),&start,&goal);
                            sample.maximumPendingPollWork=std::max(sample.maximumPendingPollWork,
                                unsigned(pf->m_cumulativeCellsAllocated-beforePoll));
                            if (premature) {
                                deleteInstance(premature);
                                throw std::runtime_error("Captured route published before commit");
                            }
                            if (!pf->isGroundPathPending(unit->getID()))
                                throw std::runtime_error("Polling discarded a pending captured route");
                        }
                        // Test-only captured execution remains serial and is
                        // drained without advancing the simulation world.
                        const auto beforeCommit=pf->m_cumulativeCellsAllocated;
                        pf->m_groundPlanner->commitCapturedSlice();
                        const auto sliceWork=unsigned(pf->m_cumulativeCellsAllocated-beforeCommit);
                        if (sliceWork) ++searchSlices;
                        maximumSliceWork=std::max(maximumSliceWork,sliceWork);
                        const auto dispatchBegan=std::chrono::steady_clock::now();
                        pf->m_groundPlanner->advanceCapturedSlice();
                        sample.maximumDispatchMilliseconds=std::max(sample.maximumDispatchMilliseconds,
                            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-dispatchBegan).count());
                        sample.maximumSearchSliceMilliseconds=std::max(sample.maximumSearchSliceMilliseconds,
                            double(pf->m_groundPlanner->stats_.lastSliceNanoseconds)/1e6);
                    }
                    if (pf->isGroundPathPending(unit->getID()))
                        throw std::runtime_error("Captured terrain route did not complete");
                }
                const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
                unsigned nodes=0;
                std::uint64_t digest=14695981039346656037ull;
                if (path) {
                    for (const auto* node=path->getFirstNode();node;node=node->getNext()) {
                        ++nodes;
                        const auto& p=*node->getPosition();
                        for (float coordinate:{p.x,p.y,p.z}) digest=mix(digest,std::bit_cast<unsigned>(coordinate));
                        digest=mix(digest,unsigned(node->getLayer()));
                    }
                    for (const auto* node=path->getFirstNode();node;node=node->getNextOptimized()) {
                        const auto& p=*node->getPosition();
                        for (float coordinate:{p.x,p.y,p.z}) digest=mix(digest,std::bit_cast<unsigned>(coordinate));
                        digest=mix(digest,unsigned(node->getLayer()));
                    }
                }
                const auto work=unsigned(pf->m_cumulativeCellsAllocated);
                if (!repeat) {
                    sample.found=path!=nullptr;sample.nodes=nodes;sample.work=work;
                    sample.digest=digest;sample.firstMilliseconds=elapsed;
                    sample.searchSlices=searchSlices;sample.maximumSliceWork=maximumSliceWork;
                } else {
                    sample.repeatable&=sample.found==(path!=nullptr) && sample.nodes==nodes &&
                        sample.work==work && sample.digest==digest && sample.searchSlices==searchSlices &&
                        sample.maximumSliceWork==maximumSliceWork;
                    sample.maximumWarmMilliseconds=std::max(sample.maximumWarmMilliseconds,elapsed);
                }
                if (path && dumpPath && !repeat) {
                    unsigned index=0;
                    for (const auto* node=path->getFirstNode();node;node=node->getNext(),++index) {
                        const auto& p=*node->getPosition();
                        routes << samples.size() << ",raw," << index << ',' << p.x << ',' << p.y << ',' << p.z << ',' << unsigned(node->getLayer()) << '\n';
                    }
                    index=0;
                    for (const auto* node=path->getFirstNode();node;node=node->getNextOptimized(),++index) {
                        const auto& p=*node->getPosition();
                        routes << samples.size() << ",smooth," << index << ',' << p.x << ',' << p.y << ',' << p.z << ',' << unsigned(node->getLayer()) << '\n';
                    }
                }
                if (path) deleteInstance(path);
            }
            samples.push_back(sample);
            TheGameLogic->destroyObject(unit);TheGameLogic->UPDATE();
        }
        return samples;
    }
    CommandResponse playerUnitRespondsWithBusyNavigation(const char* unitType,bool playerBacklog=false,
        bool sameTick=false,bool activePlayerSearch=false) {
        auto* pf=TheAI->pathfinder();
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        // The preemption case fills the four ordinary workspaces; the other
        // cases additionally exercise a long admission backlog.
        for (unsigned i=0;i<(activePlayerSearch?4u:128u);++i) {
            auto* unit=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleHumvee"),team);
            Coord3D start{2805.f+40*(i%32),205.f+40*(i/32),0};
            start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
            unit->setPosition(&start);
            Coord3D goal{4475,5385,TheTerrainLogic->getGroundHeight(4475,5385)};
            unit->getAIUpdateInterface()->aiMoveToPosition(&goal,playerBacklog?CMD_FROM_PLAYER:CMD_FROM_AI);
        }
        if (!sameTick) TheGameLogic->UPDATE();
        ObjectID interrupted=INVALID_ID;
        if (activePlayerSearch) {
            auto* earlier=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleHumvee"),team);
            Coord3D oldStart{1165,3145,TheTerrainLogic->getGroundHeight(1165,3145)};
            Coord3D oldGoal{45,2195,TheTerrainLogic->getGroundHeight(45,2195)};
            earlier->setPosition(&oldStart);
            auto* earlierAI=earlier->getAIUpdateInterface();
            earlierAI->aiMoveToPosition(&oldGoal,CMD_FROM_PLAYER);
            pf->m_deferGroundQueries=TRUE;pf->m_priorityGroundQuery=TRUE;
            earlierAI->doPathfind(pf);
            interrupted=earlier->getID();
            auto found=pf->m_groundPlanner->state_->pending.find(interrupted);
            if (found==pf->m_groundPlanner->state_->pending.end() || !found->second.slice)
                throw std::runtime_error("Active-command fixture did not capture its older order");
            // Hold a deliberately long foreground continuation at tiny slice
            // boundaries; a new order must not wait for it to finish.
            found->second.slice->dispatchBudget=1;
            earlierAI->doPathfind(pf);
            found=pf->m_groundPlanner->state_->pending.find(interrupted);
            if (found==pf->m_groundPlanner->state_->pending.end() ||
                !found->second.started || !found->second.slice->work)
                throw std::runtime_error("Preemption fixture did not start its older continuation");
            pf->m_groundPlanner->advanceCapturedSlice();
            pf->m_groundPlanner->commitCapturedSlice();
            pf->m_deferGroundQueries=FALSE;pf->m_priorityGroundQuery=FALSE;
            earlierAI->acknowledgePlayerPathCommand();
        }
        auto* dozer=TheThingFactory->newObject(TheThingFactory->findTemplate(unitType),team);
        Coord3D start{3125,385,TheTerrainLogic->getGroundHeight(3125,385)};
        dozer->setPosition(&start);
        auto* ai=dozer->getAIUpdateInterface();
        Coord3D goal{3225,385,TheTerrainLogic->getGroundHeight(3225,385)};
        ai->aiMoveToPosition(&goal,CMD_FROM_PLAYER);
        CommandResponse result;
        while (result.firstRouteTicks<12 && (ai->isWaitingForPath() || !ai->getPath())) {
            TheGameLogic->UPDATE();++result.firstRouteTicks;
        }
        // Re-command immediately after adoption: automatic-repath backoff
        // must not impose a one-second delay on an explicit player order.
        goal={3425,385,TheTerrainLogic->getGroundHeight(3425,385)};
        ai->aiMoveToPosition(&goal,CMD_FROM_PLAYER);
        while (result.replacementRouteTicks<12 && (ai->isWaitingForPath() || !ai->getPath())) {
            TheGameLogic->UPDATE();++result.replacementRouteTicks;
        }
        if (ai->getPath()) {
            const auto& end=*ai->getPath()->getLastNode()->getPosition();
            result.latestGoal=std::hypot(end.x-goal.x,end.y-goal.y)<20;
        }
        std::ostringstream trace;
        for (unsigned i=0;i<12;++i) {
            TheGameLogic->UPDATE();
            trace << " tick=" << i << " pos=" << dozer->getPosition()->x << ',' << dozer->getPosition()->y
                << " facing=" << dozer->getOrientation() << " state=" << ai->getCurrentStateID()
                << " waiting=" << ai->isWaitingForPath() << " path=" << bool(ai->getPath());
        }
        // Preserve authored acceleration: a dozer travels about three world
        // units here, but must actually begin moving during this interval.
        result.moved=std::hypot(dozer->getPosition()->x-start.x,dozer->getPosition()->y-start.y)>0.1f;
        if (interrupted!=INVALID_ID) {
            auto* earlierAI=TheGameLogic->findObjectByID(interrupted)->getAIUpdateInterface();
            if (!earlierAI->getPath() && !earlierAI->isWaitingForPath())
                throw std::runtime_error("Preemption abandoned the earlier player's order");
            for (unsigned tick=0;tick<300 && earlierAI->isWaitingForPath();++tick) TheGameLogic->UPDATE();
            if (earlierAI->isWaitingForPath() || !earlierAI->getPath()) {
                std::ostringstream failure;
                failure << "Preempted order: waiting=" << earlierAI->isWaitingForPath()
                    << " path=" << bool(earlierAI->getPath()) << " pending=" << pf->isGroundPathPending(interrupted)
                    << " queued=" << pf->m_pathRequests->contains(unsigned(interrupted))
                    << " queue_size=" << pf->m_pathRequests->size() << " state=" << earlierAI->getCurrentStateID();
                throw std::runtime_error(failure.str());
            }
        }
        result.detail=trace.str();
        return result;
    }
    std::string compareCrowdedLiveAndCapturedRoute() {
        moveHumveeGroup(10000,0,true);
        auto* pf=TheAI->pathfinder();
        const auto id=ObjectID(pf->m_pathRequests->at(0));
        auto* unit=TheGameLogic->findObjectByID(id);
        auto* ai=unit->getAIUpdateInterface();
        const auto from=*unit->getPosition(),to=*ai->getGoalPosition();
        std::ostringstream report;
        std::vector<std::uint64_t> digests;
        for (bool captured:{false,true}) {
            pf->m_deferGroundQueries=captured;
            pf->m_cumulativeCellsAllocated=0;
            const auto began=std::chrono::steady_clock::now();
            Path* path=nullptr;
            unsigned polls=0;
            do {
                path=pf->findPath(unit,ai->getLocomotorSet(),&from,&to);
                pf->m_groundPlanner->advanceCapturedSlice();
                pf->m_groundPlanner->commitCapturedSlice();
                if (++polls>20000) throw std::runtime_error("Crowd route never completed");
            } while (pf->isGroundPathPending(id));
            std::uint64_t digest=0;unsigned nodes=0;
            if (path) for (auto* node=path->getFirstNode();node;node=node->getNext()) {
                ++nodes;
                digest=digest*1099511628211ull+std::bit_cast<unsigned>(node->getPosition()->x);
                digest=digest*1099511628211ull+std::bit_cast<unsigned>(node->getPosition()->y);
            }
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
            report << " captured=" << captured << " id=" << unsigned(id) << " from=" << from.x << ',' << from.y
                << " to=" << to.x << ',' << to.y << " polls=" << polls << " work=" << pf->m_cumulativeCellsAllocated
                << " nodes=" << nodes << " digest=" << digest << " ms=" << elapsed;
            digests.push_back(digest);
            if (path) deleteInstance(path);
        }
        pf->m_deferGroundQueries=FALSE;
        if (!digests[0] || digests[0]!=digests[1]) throw std::runtime_error(report.str());
        return report.str();
    }
    QueueSnapshotResult queuedOrdersSurviveNavigationSnapshot(bool fullGame=false) {
        auto* pf=TheAI->pathfinder();
        std::vector<ObjectID> ids;
        std::vector<Coord3D> starts;
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        for (unsigned i=0;i<4;++i) {
            auto* unit=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleHumvee"),team);
            Coord3D start{3125.f+40*i,385,0};
            start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
            unit->setPosition(&start);ids.push_back(unit->getID());starts.push_back(start);
        }
        // Preserve command arrival order, deliberately different from object-ID order.
        for (unsigned i:{2u,0u,3u,1u}) {
            Coord3D goal{3425.f+40*i,585,0};
            goal.z=TheTerrainLogic->getGroundHeight(goal.x,goal.y);
            TheGameLogic->findObjectByID(ids[i])->getAIUpdateInterface()->aiMoveToPosition(&goal,CMD_FROM_PLAYER);
        }
        const auto queued=[&] {
            std::vector<unsigned> result;
            for (unsigned i=0;i<pf->m_pathRequests->size();++i) result.push_back(pf->m_pathRequests->at(i));
            return result;
        };
        const auto crc=[&] {
            XferCRC xfer;xfer.open("navigation snapshot");xfer.xferSnapshot(pf);
            const auto value=xfer.getCRC();xfer.close();return value;
        };
        const auto expected=queued();
        const auto commandState=[&] {
            std::vector<std::array<std::uint64_t,4>> result;
            for (const auto id:ids) {
                const auto* ai=TheGameLogic->findObjectByID(id)->getAIUpdateInterface();
                result.push_back({ai->getPathRequestRevision(),std::uint64_t(ai->hasFreshPlayerPathCommand()),
                    ai->getPlayerPathCommandFrame(),ai->getPlayerPathCommandSequence()});
            }
            return result;
        };
        const auto expectedCommands=commandState();
        for (const auto id:ids)
            if (!pf->m_pathRequests->contains(unsigned(id))) throw std::runtime_error("Snapshot fixture did not queue mover");
        const auto expectedCRC=crc();
        const auto file=(dataRoot/"navigation-queue.snapshot").string();
        if (fullGame) {
            const auto saved=TheGameState->saveGame("navigation-queue.sav",UnicodeString(L"Pending navigation"),SAVE_FILE_TYPE_NORMAL);
            if (saved.saveCode!=SC_OK) throw std::runtime_error("Failed to save game with pending navigation");
        } else {
            XferSave save;save.open(file.c_str());save.xferSnapshot(pf);save.close();
        }
        pf->m_pathRequests->clear();pf->queueForPath(INVALID_ID);pf->m_cumulativeCellsAllocated=12345;
        if (fullGame) {
            TheWritableGlobalData->m_loadSaveGame="navigation-queue.sav";
            TheGameState->loadQueuedSaveGame();
            pf=TheAI->pathfinder();
            if (!TheGameLogic->isInGame() || !pf->wasPathQueueLoaded())
                throw std::runtime_error("Game load did not restore navigation block");
        } else {
            XferLoad load;load.setOptions(XO_NO_POST_PROCESSING);load.open(file.c_str());
            load.xferSnapshot(pf);load.close();
        }
        QueueSnapshotResult result{queued()==expected,crc()==expectedCRC,commandState()==expectedCommands};
        // Admission may defer whole queries, but must service at least one
        // queued request per call and must never lose this saved FIFO.
        for (unsigned i=0;i<expected.size() && !pf->m_pathRequests->empty();++i)
            pf->processPathfindQueue();
        for (const auto id:ids) {
            const auto* ai=TheGameLogic->findObjectByID(id)->getAIUpdateInterface();
            if (!ai->isWaitingForPath() && ai->getPath()) ++result.adopted;
        }
        for (unsigned frame=0;frame<300;++frame) TheGameLogic->UPDATE();
        for (unsigned i=0;i<ids.size();++i) {
            const auto* unit=TheGameLogic->findObjectByID(ids[i]);
            if (!unit) continue;
            const auto& end=*unit->getPosition();
            if (std::hypot(end.x-starts[i].x,end.y-starts[i].y)>100) ++result.moved;
        }
        return result;
    }
    bool queuedSpecialRoutesCompleteAcrossSlices(bool flee=false) {
        auto* object=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        Coord3D start{3125,385,TheTerrainLogic->getGroundHeight(3125,385)},goal{3425,385,0};
        object->setPosition(&start);
        auto* ai=object->getAIUpdateInterface();auto* pf=TheAI->pathfinder();
        ai->aiMoveToPosition(&goal,CMD_FROM_PLAYER);
        if (flee) ai->requestSafePath(object->getID());
        else ai->requestApproachPath(&goal);
        pf->queueForPath(object->getID());
        pf->processPathfindQueue();
        for (unsigned slice=0;slice<256 && pf->isGroundPathPending(object->getID());++slice)
            pf->processPathfindQueue();
        // Publication adopts the complete route at a queue boundary.
        bool valid=!ai->isWaitingForPath() && !pf->isGroundPathPending(object->getID()) && ai->getPath();
        if (ai->getPath()) {
            const auto& end=*ai->getPath()->getLastNode()->getPosition();
            if (flee) valid &= std::hypot(end.x-start.x,end.y-start.y)>10;
            else valid &= std::hypot(end.x-goal.x,end.y-goal.y)<300;
        }
        std::ostringstream failure;
        failure << "Special route lifecycle: flee=" << flee << " waiting=" << ai->isWaitingForPath()
            << " pending=" << pf->isGroundPathPending(object->getID()) << " path=" << bool(ai->getPath())
            << " state=" << ai->getCurrentStateID()
            ;
        TheGameLogic->destroyObject(object);TheGameLogic->UPDATE();
        if (!valid) throw std::runtime_error(failure.str());
        return valid;
    }
    bool nativeGroundRequestsCompleteAcrossSlices() {
        auto* pf=TheAI->pathfinder();
        auto* object=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        auto* ai=object->getAIUpdateInterface();
        const auto id=object->getID();
        Coord3D start{3125,385,0},first{3225,385,0},latest{3425,385,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        object->setPosition(&start);
        // A caller can abandon a deferred query without destroying its unit.
        // Such a query has no queued consumer and must not block live orders.
        auto* abandoned=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        abandoned->setPosition(&start);
        {
            struct Restore {
                Pathfinder* pf;Bool previous;
                ~Restore() { pf->setGroundQueriesDeferred(previous); }
            } restore{pf,pf->groundQueriesDeferred()};
            pf->setGroundQueriesDeferred(true);
            auto* unexpected=pf->findPath(abandoned,abandoned->getAIUpdateInterface()->getLocomotorSet(),&start,&first);
            if (unexpected) deleteInstance(unexpected);
            if (!pf->isGroundPathPending(abandoned->getID()))
                throw std::runtime_error("Abandoned query fixture did not suspend");
        }
        ai->aiMoveToPosition(&first,CMD_FROM_PLAYER);
        ai->requestPath(&first,true);
        pf->processPathfindQueue();
        if (pf->isGroundPathPending(abandoned->getID()))
            throw std::runtime_error("Unqueued query retained gameplay admission precedence");
        TheGameLogic->destroyObject(abandoned);
        if (pf->isGroundPathPending(id) && !ai->isWaitingForPath())
            throw std::runtime_error("Queued ground order lost its pending state");
        for (unsigned slice=0;slice<256 && pf->isGroundPathPending(id);++slice)
            pf->processPathfindQueue();
        bool valid=!pf->isGroundPathPending(id) && !ai->isWaitingForPath() && ai->getPath();
        if (!valid) throw std::runtime_error("Queued ground order did not finish its bounded slices");
        pf->m_groundPlanner->invalidate();
        // Preserve the DX9 anti-spin cooldown before issuing another request.
        for (unsigned i=0;i<4;++i) TheGameLogic->UPDATE();
        const auto frame=TheGameLogic->getFrame();
        const auto* previousPath=ai->getPath();
        ai->requestPath(&latest,true);
        pf->processPathfindQueue();
        if (pf->isGroundPathPending(id) && ai->getPath()!=previousPath)
            throw std::runtime_error("Pending replacement discarded the current route");
        for (unsigned slice=0;slice<256 && pf->isGroundPathPending(id);++slice)
            pf->processPathfindQueue();
        valid=valid && TheGameLogic->getFrame()==frame && !pf->isGroundPathPending(id) &&
            !ai->isWaitingForPath() && ai->getPath();
        if (ai->getPath()) {
            const auto& end=*ai->getPath()->getLastNode()->getPosition();
            valid=valid && std::abs(end.x-latest.x)<20 && std::abs(end.y-latest.y)<20;
        }
        // Destruction before the next queue boundary must discard the order.
        ai->aiMoveToPosition(&first,CMD_FROM_PLAYER);
        ai->requestPath(&first,true);
        TheGameLogic->destroyObject(object);
        TheGameLogic->UPDATE();
        return valid && !pf->isGroundPathPending(id);
    }
    bool editedPathsDiscardCachedTargets() {
        auto* object=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        Coord3D start{3125,385,0},goal{3225,385,0},latest{3245,385,0},position{3135,385,0};
        for (auto* point:{&start,&goal,&latest,&position})
            point->z=TheTerrainLogic->getGroundHeight(point->x,point->y);
        object->setPosition(&position);
        const auto& locomotors=object->getAIUpdateInterface()->getLocomotorSet();
        bool valid=true;
        for (int edit:{0,1,2,3}) {
            const bool append=edit==1;
            const bool rewire=edit==3;
            auto* path=newInstance(Path);
            path->appendNode(&start,LAYER_GROUND);
            path->markOptimized();
            Coord3D waypoint=goal;
            if (rewire) {
                waypoint.y+=20;
                waypoint.z=TheTerrainLogic->getGroundHeight(waypoint.x,waypoint.y);
            }
            path->appendNode(&waypoint,LAYER_GROUND);
            if (rewire) path->appendNode(&latest,LAYER_GROUND);
            ClosestPointOnPathInfo old{},actual{},expected{};
            path->computePointOnPath(object,locomotors,position,old);
            if (rewire) path->getFirstNode()->setNextOptimized(path->getLastNode());
            else if (append) path->appendNode(&latest,LAYER_GROUND);
            else if (edit==2) path->getLastNode()->setPosition(&latest);
            else path->updateLastNode(&latest);
            path->computePointOnPath(object,locomotors,position,actual);
            auto* fresh=newInstance(Path);
            fresh->appendNode(&start,LAYER_GROUND);
            fresh->markOptimized();
            if (append) fresh->appendNode(&goal,LAYER_GROUND);
            fresh->appendNode(&latest,LAYER_GROUND);
            fresh->computePointOnPath(object,locomotors,position,expected);
            valid=(rewire ? expected.distAlongPath<old.distAlongPath : expected.distAlongPath>old.distAlongPath) &&
                actual.distAlongPath==expected.distAlongPath && actual.layer==expected.layer &&
                actual.posOnPath.x==expected.posOnPath.x && actual.posOnPath.y==expected.posOnPath.y &&
                actual.posOnPath.z==expected.posOnPath.z && valid;
            deleteInstance(fresh);
            deleteInstance(path);
        }
        TheGameLogic->destroyObject(object);
        TheGameLogic->UPDATE();
        return valid;
    }
    bool dx9FollowerUsesVisibleWaypointAndRefreshesAfterSmallMovement() {
        auto* object=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        const auto& locomotors=object->getAIUpdateInterface()->getLocomotorSet();
        bool matches=true;
        for (float y:{375.f,385.f,395.f}) {
            Coord3D first{3125,385,0},bend{3225,385,0},last{3225,405,0},position{3135,y,0};
            for (auto* p:{&first,&bend,&last,&position}) p->z=TheTerrainLogic->getGroundHeight(p->x,p->y);
            object->setPosition(&position);
            auto* path=newInstance(Path);
            path->appendNode(&first,LAYER_GROUND);
            path->appendNode(&bend,LAYER_GROUND);
            path->appendNode(&last,LAYER_GROUND);
            auto* corner=path->getFirstNode()->getNext();
            path->getFirstNode()->setNextOptimized(corner);
            corner->setNextOptimized(path->getLastNode());
            corner->setCanOptimize(true);
            path->markOptimized();
            ClosestPointOnPathInfo result{};
            path->computePointOnPath(object,locomotors,position,result);
            matches=matches && result.posOnPath.x==bend.x && result.posOnPath.y==bend.y &&
                TheAI->pathfinder()->isLinePassable(object,locomotors.getValidSurfaces(),
                    LAYER_GROUND,position,result.posOnPath,false,true);
            const auto before=result.distAlongPath;
            position.x+=0.25f;
            object->setPosition(&position);
            path->computePointOnPath(object,locomotors,position,result);
            matches=matches && result.distAlongPath==before-0.25f;
            deleteInstance(path);
        }
        TheGameLogic->destroyObject(object);
        TheGameLogic->UPDATE();
        return matches;
    }
    float resumeAfterShortFallback() {
        class ShortFallback final : public PathfindServicesInterface {
        public:
            Path* findPath(Object*, const LocomotorSet&, const Coord3D*, const Coord3D*) override { return nullptr; }
            Path* patchPath(const Object*, const LocomotorSet&, Path*, Bool) override { return nullptr; }
            Path* findClosestPath(Object*, const LocomotorSet&, const Coord3D* from, Coord3D* to, Bool, Real, Bool) override {
                *to=*from;
                to->x+=5;
                auto* path=newInstance(Path);
                path->appendNode(from,LAYER_GROUND);
                path->appendNode(to,LAYER_GROUND);
                path->getFirstNode()->setNextOptimized(path->getLastNode());
                path->markOptimized();
                return path;
            }
            Path* findAttackPath(const Object*, const LocomotorSet&, const Coord3D*, const Object*, const Coord3D*, const Weapon*) override { return nullptr; }
            Path* findSafePath(const Object*, const LocomotorSet&, const Coord3D*, const Coord3D*, const Coord3D*, Real) override { return nullptr; }
        } service;
        auto* object=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        if (!object || !object->getAIUpdateInterface()) throw std::runtime_error("Missing fallback unit");
        Coord3D start{3125,385,0},goal{3225,385,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        goal.z=TheTerrainLogic->getGroundHeight(goal.x,goal.y);
        object->setPosition(&start);
        auto* ai=object->getAIUpdateInterface();
        ai->aiMoveToPosition(&goal,CMD_FROM_PLAYER);
        ai->doPathfind(&service);
        if (!ai->getRetryPath() || !ai->getPath()) throw std::runtime_error("Fallback was not adopted");
        for (unsigned frame=0;frame<300;++frame) TheGameLogic->UPDATE();
        return std::hypot(object->getPosition()->x-start.x,object->getPosition()->y-start.y);
    }
    RequestLifecycle deferredGroundRequestLifecycle() {
        class DeferredService final : public PathfindServicesInterface {
        public:
            bool pending = true;
            unsigned finds = 0, failures = 0;
            Coord3D requested{};
            Bool isGroundPathPending(ObjectID) const override { return pending; }
            Path* findPath(Object*, const LocomotorSet&, const Coord3D* from, const Coord3D* to) override {
                ++finds;
                requested = *to;
                if (pending) return nullptr;
                auto* path = newInstance(Path);
                path->appendNode(from, LAYER_GROUND);
                path->appendNode(to, LAYER_GROUND);
                path->getFirstNode()->setNextOptimized(path->getLastNode());
                path->markOptimized();
                return path;
            }
            Path* patchPath(const Object*, const LocomotorSet&, Path*, Bool) override { return nullptr; }
            Path* findClosestPath(Object*, const LocomotorSet&, const Coord3D*, Coord3D*, Bool, Real, Bool) override {
                ++failures; return nullptr;
            }
            Path* findAttackPath(const Object*, const LocomotorSet&, const Coord3D*, const Object*, const Coord3D*, const Weapon*) override { return nullptr; }
            Path* findSafePath(const Object*, const LocomotorSet&, const Coord3D*, const Coord3D*, const Coord3D*, Real) override { return nullptr; }
        } service;
        auto* object = TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleHumvee"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        if (!object || !object->getAIUpdateInterface()) throw std::runtime_error("Missing lifecycle unit AI");
        const auto id = object->getID();
        auto* ai = object->getAIUpdateInterface();
        Coord3D start{3125,385,0}, first{3225,385,0}, latest{3245,385,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        first.z=TheTerrainLogic->getGroundHeight(first.x,first.y);
        latest.z=TheTerrainLogic->getGroundHeight(latest.x,latest.y);
        object->setPosition(&start);
        ai->aiMoveToPosition(&first, CMD_FROM_PLAYER);
        ai->requestPath(&first,true);
        ai->doPathfind(&service);
        RequestLifecycle result;
        result.waiting = ai->isWaitingForPath() && ai->getPath()==nullptr;
        result.noFailureFallback = service.failures==0;
        const auto pendingRevision=ai->getPathRequestRevision();
        for (unsigned retry=0;retry<3;++retry) {
            ai->doPathfind(&service);
            if (ai->getPathRequestRevision()!=pendingRevision)
                throw std::runtime_error("Resuming a pending request changed its revision");
        }
        ai->requestPath(&latest,true);
        ai->doPathfind(&service);
        result.latestDestination = ai->isWaitingForPath() && service.requested.x==latest.x && service.requested.y==latest.y;
        const auto completionRevision=ai->getPathRequestRevision();
        service.pending=false;
        ai->doPathfind(&service);
        if (ai->getPathRequestRevision()!=completionRevision)
            throw std::runtime_error("Adopting a route changed its request revision");
        result.completed = !ai->isWaitingForPath() && ai->getPath() &&
            ai->getPath()->getLastNode()->getPosition()->x==latest.x &&
            ai->getPath()->getLastNode()->getPosition()->y==latest.y;
        service.pending=true;
        ai->requestPath(&first,true);
        TheGameLogic->destroyObject(object);
        const auto before=service.finds;
        ai->doPathfind(&service);
        result.destructionCancelled = service.finds==before && !ai->isWaitingForPath();
        TheGameLogic->UPDATE();
        TheAI->pathfinder()->queueForPath(id);
        TheAI->pathfinder()->processPathfindQueue();
        result.staleIdDiscarded = TheGameLogic->findObjectByID(id)==nullptr;
        return result;
    }
    bool usesCombinedGroundRequest() {
        class CombinedService final : public PathfindServicesInterface {
        public:
            unsigned calls=0;
            bool fallback=false;
            Path* findPathOrClosest(Object*,const LocomotorSet&,const Coord3D* from,Coord3D* to,Bool,Bool& usedFallback) override {
                ++calls;
                usedFallback=fallback;
                if (fallback) to->x-=20;
                auto* path=newInstance(Path);
                path->appendNode(from,LAYER_GROUND);
                path->appendNode(to,LAYER_GROUND);
                path->getFirstNode()->setNextOptimized(path->getLastNode());
                path->markOptimized();
                return path;
            }
            Path* findPath(Object*,const LocomotorSet&,const Coord3D*,const Coord3D*) override {
                throw std::runtime_error("Combined request repeated an exact search");
            }
            Path* findClosestPath(Object*,const LocomotorSet&,const Coord3D*,Coord3D*,Bool,Real,Bool) override {
                throw std::runtime_error("Combined request repeated a fallback search");
            }
            Path* patchPath(const Object*,const LocomotorSet&,Path*,Bool) override { return nullptr; }
            Path* findAttackPath(const Object*,const LocomotorSet&,const Coord3D*,const Object*,const Coord3D*,const Weapon*) override { return nullptr; }
            Path* findSafePath(const Object*,const LocomotorSet&,const Coord3D*,const Coord3D*,const Coord3D*,Real) override { return nullptr; }
        } service;
        auto* object=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleHumvee"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        if (!object || !object->getAIUpdateInterface()) throw std::runtime_error("Missing combined-request unit");
        Coord3D start{3125,385,0},goal{3225,385,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        goal.z=TheTerrainLogic->getGroundHeight(goal.x,goal.y);
        object->setPosition(&start);
        auto* ai=object->getAIUpdateInterface();
        bool valid=true;
        for (bool fallback : {false,true}) {
            service.fallback=fallback;
            ai->requestPath(&goal,true);
            ai->doPathfind(&service);
            valid=valid && !ai->isWaitingForPath() && ai->getRetryPath()==fallback && ai->getPath() &&
                ai->getPath()->getLastNode()->getPosition()->x==goal.x-(fallback?20:0);
        }
        valid=valid && service.calls==2;
        TheGameLogic->destroyObject(object);
        TheGameLogic->UPDATE();
        return valid;
    }
    bool combinedSearchPreservesExactReservationPolicy() {
        auto* team=ThePlayerList->getLocalPlayer()->getDefaultTeam();
        const auto* type=TheThingFactory->findTemplate("AmericaVehicleHumvee");
        auto* mover=TheThingFactory->newObject(type,team);
        auto* holder=TheThingFactory->newObject(type,team);
        if (!mover || !holder || !mover->getAIUpdateInterface()) throw std::runtime_error("Missing reservation fixtures");
        Coord3D start{3125,385,0},goal{3225,385,0},away{3325,585,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        goal.z=TheTerrainLogic->getGroundHeight(goal.x,goal.y);
        away.z=TheTerrainLogic->getGroundHeight(away.x,away.y);
        mover->setPosition(&start); holder->setPosition(&away);
        auto* pathfinder=TheAI->pathfinder();
        const auto& locomotors=mover->getAIUpdateInterface()->getLocomotorSet();
        auto* exact=pathfinder->findPath(mover,locomotors,&start,&goal);
        pathfinder->updateGoal(holder,&goal,LAYER_GROUND);
        auto requested=goal;
        Bool fallback=TRUE;
        auto* combined=pathfinder->findPathOrClosest(mover,locomotors,&start,&requested,FALSE,fallback);
        // Reservation policy is an observable contract regardless of whether
        // a direct accelerator or hierarchical refinement constructs the path.
        bool valid=exact && combined && !fallback && requested.x==goal.x && requested.y==goal.y;
        auto* a=exact?exact->getFirstNode():nullptr;
        auto* b=combined?combined->getFirstNode():nullptr;
        while (a && b) {
            valid=valid && a->getPosition()->x==b->getPosition()->x &&
                a->getPosition()->y==b->getPosition()->y && a->getLayer()==b->getLayer();
            a=a->getNextOptimized(); b=b->getNextOptimized();
        }
        valid=valid && !a && !b;
        if (exact) deleteInstance(exact);
        if (combined) deleteInstance(combined);
        TheGameLogic->destroyObject(mover); TheGameLogic->destroyObject(holder);
        TheGameLogic->UPDATE();
        return valid;
    }
    bool combinedDeferredFallbackKeepsItsStage() {
        auto* pf=TheAI->pathfinder();
        auto* mover=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleHumvee"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        auto* ally=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleHumvee"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        Coord3D start{3125,385,0},goal{3225,385,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        mover->setPosition(&start);
        const auto& locomotors=mover->getAIUpdateInterface()->getLocomotorSet();
        using OwnedPath=std::unique_ptr<Path,GroundRoutePlanner::WeightedResult::DeletePath>;
        struct Restore {
            Pathfinder* pf;
            Bool previous;
            std::vector<std::pair<PathfindCell*,PathfindCell::CellType>> cells;
            std::vector<std::pair<ICoord2D,ObjectID>> goals;
            ~Restore() {
                pf->setGroundQueriesDeferred(previous);
                for (const auto& [cell,type]:cells) cell->setType(type);
                for (const auto& [position,id]:goals) {
                    auto* cell=pf->getCell(LAYER_GROUND,position.x,position.y);
                    cell->setGoalUnit(INVALID_ID,position);
                    cell->setGoalUnit(id,position);
                }
                pf->invalidateNavigationSnapshots();
            }
        } restore{pf,pf->groundQueriesDeferred()};
        for (int x=320;x<=324;++x) for (int y=36;y<=40;++y) {
            auto* cell=pf->getCell(LAYER_GROUND,x,y);
            restore.cells.emplace_back(cell,cell->getType());
            cell->setType(PathfindCell::CELL_IMPASSABLE);
        }
        pf->invalidateNavigationSnapshots();
        for (bool enclosed : {false,true}) {
            if (enclosed) {
                // Keep the starting footprint legal and surround it with a
                // wall. An impassable starting footprint invokes DX9 escape
                // behavior and therefore is not a negative-route fixture.
                for (int x=307;x<=317;++x) for (int y=33;y<=43;++y) {
                    if (std::abs(x-312)<3 && std::abs(y-38)<3) continue;
                    auto* cell=pf->getCell(LAYER_GROUND,x,y);
                    restore.cells.emplace_back(cell,cell->getType());
                    cell->setType(PathfindCell::CELL_IMPASSABLE);
                }
                // No endpoint in the enclosed pocket is available. A goal
                // reservation leaves traversal legal but rejects termination.
                for (int x=310;x<=314;++x) for (int y=36;y<=40;++y) {
                    auto* cell=pf->getCell(LAYER_GROUND,x,y);
                    restore.goals.push_back({{x,y},cell->getGoalUnit()});
                    cell->setGoalUnit(INVALID_ID,{x,y});
                    cell->setGoalUnit(ally->getID(),{x,y});
                }
                pf->invalidateNavigationSnapshots();
            }
            pf->setGroundQueriesDeferred(false);
            auto expectedGoal=goal;
            Bool expectedFallback=false;
            OwnedPath expected(pf->findPathOrClosest(mover,locomotors,&start,&expectedGoal,FALSE,expectedFallback));
            if (bool(expected)==enclosed || !expectedFallback)
                throw std::runtime_error("Deferred fallback fixture did not produce its expected native outcome");
            pf->setGroundQueriesDeferred(true);
            auto actualGoal=goal;
            Bool actualFallback=false;
            OwnedPath actual;
            for (unsigned slice=0;slice<1000;++slice) {
                actual.reset(pf->findPathOrClosest(mover,locomotors,&start,&actualGoal,FALSE,actualFallback));
                if (!pf->isGroundPathPending(mover->getID())) break;
                pf->m_groundPlanner->advanceCapturedSlice();
                pf->m_groundPlanner->commitCapturedSlice();
            }
            if (pf->isGroundPathPending(mover->getID()) || bool(actual)!=bool(expected) ||
                actualFallback!=expectedFallback || actualGoal.x!=expectedGoal.x || actualGoal.y!=expectedGoal.y)
                throw std::runtime_error("Combined request lost its closest-route stage or restarted a completed failure");
            auto* a=expected?expected->getFirstNode():nullptr;
            auto* b=actual?actual->getFirstNode():nullptr;
            while (a && b) {
                if (a->getPosition()->x!=b->getPosition()->x || a->getPosition()->y!=b->getPosition()->y ||
                    a->getLayer()!=b->getLayer()) throw std::runtime_error("Deferred closest route changed geometry");
                a=a->getNext();b=b->getNext();
            }
            if (a || b) throw std::runtime_error("Deferred closest route changed node count");
            if (!enclosed) {
                auto* ai=mover->getAIUpdateInterface();
                ai->requestPath(&goal,true);
                ai->doPathfind(pf);
                if (!pf->isGroundPathPending(mover->getID()) || !ai->isWaitingForPath())
                    throw std::runtime_error("Invalid exact destination orphaned its deferred closest query");
                for (unsigned tick=0;tick<1000 && ai->isWaitingForPath();++tick)
                    pf->processPathfindQueue();
                if (ai->isWaitingForPath() || pf->isGroundPathPending(mover->getID()) ||
                    !ai->getPath() || !ai->getRetryPath())
                    throw std::runtime_error("Deferred closest query did not publish through its AI owner");
                ai->destroyPath();
            }
        }
        TheGameLogic->destroyObject(mover);
        TheGameLogic->destroyObject(ally);
        return true;
    }
    bool rangerMovesAfterTurningInsideItsWalkingAngle() {
        auto* mover=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaInfantryRanger"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        auto* ai=mover->getAIUpdateInterface();
        ai->chooseLocomotorSet(LOCOMOTORSET_NORMAL);
        Coord3D start{3125,385,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        mover->setPosition(&start);mover->setOrientation(0);
        constexpr float angle=50*3.14159265358979323846f/180;
        Coord3D goal{start.x+100*std::cos(angle),start.y+100*std::sin(angle),start.z};
        goal.z=TheTerrainLogic->getGroundHeight(goal.x,goal.y);
        auto* locomotor=ai->getCurLocomotor();
        if (!locomotor) throw std::runtime_error("Ranger has no active locomotor");
        Bool blocked=FALSE;
        locomotor->locoUpdate_moveTowardsPosition(mover,goal,100,ai->getCurLocomotorSpeed(),&blocked);
        const float remaining=std::abs(angle-mover->getOrientation());
        mover->getPhysics()->update();
        const auto& velocity=*mover->getPhysics()->getVelocity();
        const bool valid=remaining<3.14159265358979323846f/4 && remaining>0 &&
            std::hypot(velocity.x,velocity.y)>0.001f;
        TheGameLogic->destroyObject(mover);TheGameLogic->UPDATE();
        return valid;
    }
    bool fallbackHandlesUnallocatedDestinationCell() {
        auto* mover=TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleHumvee"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        if (!mover || !mover->getAIUpdateInterface()) throw std::runtime_error("Missing fallback fixture mover");
        Coord3D start{3125,385,0},goal{3225,385,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        goal.z=TheTerrainLogic->getGroundHeight(goal.x,goal.y);
        mover->setPosition(&start);
        auto* pathfinder=TheAI->pathfinder();
        auto* cell=pathfinder->getCell(LAYER_GROUND,322,38);
        if (!cell || cell->getFlags()!=PathfindCell::NO_UNITS || cell->getGoalUnit()!=INVALID_ID)
            throw std::runtime_error("Fallback destination must be unoccupied");
        cell->releaseInfo();
        if (cell->hasInfo()) throw std::runtime_error("Destination unexpectedly has search data");
        const auto& locomotors=mover->getAIUpdateInterface()->getLocomotorSet();
        auto* path=pathfinder->findClosestPath(mover,locomotors,&start,&goal,FALSE,0,FALSE);
        bool valid=path && path->getLastNode() &&
            std::hypot(path->getLastNode()->getPosition()->x-3225,path->getLastNode()->getPosition()->y-385)<10;
        if (path) deleteInstance(path);
        TheGameLogic->destroyObject(mover);
        TheGameLogic->UPDATE();
        return valid;
    }
    bool validatesYieldInputs(unsigned missing) {
        auto* object = TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleHumvee"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        auto* other = TheThingFactory->newObject(TheThingFactory->findTemplate("AmericaVehicleHumvee"),
            ThePlayerList->getLocalPlayer()->getDefaultTeam());
        if (!object || !other) throw std::runtime_error("Missing yield fixture units");
        Coord3D start{3125,385,0}, end{3225,385,0};
        start.z=TheTerrainLogic->getGroundHeight(start.x,start.y);
        end.z=TheTerrainLogic->getGroundHeight(end.x,end.y);
        object->setPosition(&start);
        other->setPosition(&end);
        auto* path = newInstance(Path);
        auto pathStart=start;
        if (missing==3) pathStart.x-=100;
        path->appendNode(&pathStart,LAYER_GROUND);
        path->appendNode(&end,LAYER_GROUND);
        path->getFirstNode()->setNextOptimized(path->getLastNode());
        path->markOptimized();
        Path* crossing=nullptr;
        if (missing==3) {
            crossing=newInstance(Path);
            auto from=start,to=start;
            from.y-=100; to.y+=100;
            crossing->appendNode(&from,LAYER_GROUND);
            crossing->appendNode(&to,LAYER_GROUND);
            crossing->getFirstNode()->setNextOptimized(crossing->getLastNode());
            crossing->markOptimized();
        }
        auto* result = TheAI->pathfinder()->getMoveAwayFromPath(
            missing==0 ? nullptr : object, other,
            missing==1 ? nullptr : path, crossing?other:nullptr, crossing);
        bool valid = missing<2 ? result==nullptr :
            result && result->getLastNode() &&
            std::hypot(result->getLastNode()->getPosition()->x-start.x,
                result->getLastNode()->getPosition()->y-start.y)>0;
        if (valid && crossing) {
            const auto* goal=result->getLastNode()->getPosition();
            valid=std::abs(goal->x-start.x)>17.5f && std::abs(goal->y-start.y)>17.5f;
            auto* repeated=TheAI->pathfinder()->getMoveAwayFromPath(object,other,path,other,crossing);
            auto* a=result->getFirstNode();
            auto* b=repeated?repeated->getFirstNode():nullptr;
            while (a && b) {
                valid=valid && a->getPosition()->x==b->getPosition()->x &&
                    a->getPosition()->y==b->getPosition()->y && a->getLayer()==b->getLayer();
                a=a->getNextOptimized(); b=b->getNextOptimized();
            }
            valid=valid && !a && !b;
            if (repeated) deleteInstance(repeated);
        }
        if (result) deleteInstance(result);
        if (crossing) deleteInstance(crossing);
        deleteInstance(path);
        TheGameLogic->destroyObject(object);
        TheGameLogic->destroyObject(other);
        TheGameLogic->UPDATE();
        return valid;
    }
    GroupMovement moveNorthwestCrowd(unsigned frames) {
        return moveHumveeGroup(1024,frames,true,Region2D{{605,45},{1205,1400}});
    }
    void auditIncrementalOccupancy(bool regionalTerrain=false) {
        auto& planner=*TheAI->pathfinder()->m_groundPlanner;
        auto* edited=TheAI->pathfinder()->getCell(LAYER_GROUND,100,100);
        const auto previousPinched=edited->getPinched();
        if (regionalTerrain) {
            edited->setPinched(!previousPinched);
            planner.invalidate(IRegion2D{{100,100},{100,100}});
        }
        Pathfinder::GroundRouteQuery query;query.acceptableSurfaces=LOCOMOTORSURFACE_GROUND;
        planner.prepareCapturedSnapshot(query,false);
        planner.captureDynamicSnapshotIncremental();
        const auto observed=planner.state_->capturedOccupancyCells;
        const auto observedIds=planner.state_->capturedOccupantIds;
        // Force the independent full native-grid scan even if the journal is
        // empty. This catches missing mutation notifications as well as bad
        // delta application. Both paths leave the same current snapshot.
        planner.state_->capturedFixedFrame=~0u;
        planner.captureDynamicSnapshotFull();
        const auto& expected=planner.state_->capturedOccupancyCells;
        if (observed.size()!=expected.size() || observedIds!=planner.state_->capturedOccupantIds)
            throw std::runtime_error("Incremental native occupancy count/IDs differ from full capture");
        for (std::size_t i=0;i<expected.size();++i) {
            const auto& a=observed[i];const auto& b=expected[i];
            if (a.layer!=b.layer || a.x!=b.x || a.y!=b.y ||
                a.occupancy.unit!=b.occupancy.unit || a.occupancy.valid!=b.occupancy.valid ||
                a.occupancy.empty!=b.occupancy.empty || a.occupancy.goal!=b.occupancy.goal ||
                a.occupancy.moving!=b.occupancy.moving || a.occupancy.fixed!=b.occupancy.fixed ||
                a.goal!=b.goal || a.aircraftGoal!=b.aircraftGoal || a.aircraftReserved!=b.aircraftReserved ||
                a.terrain!=b.terrain || a.obstacle!=b.obstacle || a.valid!=b.valid ||
                a.pinched!=b.pinched || a.fence!=b.fence || a.connection!=b.connection)
                throw std::runtime_error("Incremental native occupancy differs at " +
                    std::to_string(a.x) + "," + std::to_string(a.y));
        }
        if (regionalTerrain) {
            // Restore before the next simulation update. Leave the reverse
            // edit journaled alongside subsequent real unit movement writes.
            edited->setPinched(previousPinched);
            planner.invalidate(IRegion2D{{100,100},{100,100}});
        }
    }
    GroupMovement moveHumveeGroup(unsigned count, unsigned frames, bool mapWideInfantry = false,
        std::optional<Region2D> spawnRegion = {},bool captureCRCs=false,bool auditOccupancy=false) {
        if (count == 0 || count > 10000)
            throw std::invalid_argument("Invalid movement workload");
        struct Unit {
            ObjectID id;Coord3D start;Coord3D goal{};
            float bestGoalDistance=std::numeric_limits<float>::infinity();
            unsigned firstIdleFrame=0;Coord3D firstIdlePosition{};
        };
        std::vector<Unit> units;
        units.reserve(count);
        AIGroupPtr group = TheAI->createGroup();
        const auto* unitTemplate = TheThingFactory->findTemplate(
            mapWideInfantry ? "AmericaInfantryRanger" : "AmericaVehicleHumvee");
        auto* team = ThePlayerList->getLocalPlayer()->getDefaultTeam();
        auto* pathfinder = TheAI->pathfinder();
        // Deterministic, non-overlapping starting positions on the northern
        // plateau. Reject cliff/water/object footprints using the real grid.
        Region3D extent;
        TheTerrainLogic->getExtent(&extent);
        const int spacing = mapWideInfantry ? 20 : 40;
        const int diameter = mapWideInfantry ? 2 : 4;
        const int firstX = spawnRegion ? int(spawnRegion->lo.x) : mapWideInfantry ? int(extent.lo.x) + 25 : 2805;
        const int firstY = spawnRegion ? int(spawnRegion->lo.y) : mapWideInfantry ? int(extent.lo.y) + 25 : 205;
        const int endX = spawnRegion ? int(spawnRegion->hi.x) : mapWideInfantry ? int(extent.hi.x) - 25 : 4200;
        const int endY = spawnRegion ? int(spawnRegion->hi.y) : mapWideInfantry ? int(extent.hi.y) - 25 : 1200;
        for (int y = firstY; y < endY && units.size() < count; y += spacing) {
            for (int x = firstX; x < endX && units.size() < count; x += spacing) {
                if (pathfinder->clearCellForDiameter(false, x / 10, y / 10, LAYER_GROUND, diameter) < diameter ||
                    pathfinder->getCell(LAYER_GROUND, x / 10, y / 10)->getPinched()) continue;
                Coord3D start{float(x), float(y), 0};
                start.z = TheTerrainLogic->getGroundHeight(start.x, start.y);
                auto* object = TheThingFactory->newObject(unitTemplate, team);
                if (!object || !object->getAIUpdateInterface() || !object->getPhysics())
                    throw std::runtime_error("Group member has no AI or physics");
                object->setPosition(&start);
                group->add(object);
                units.push_back({object->getID(), start});
            }
        }
        if (units.size() != count) throw std::runtime_error("Not enough legal group spawn positions");
        Coord3D goal{4475, 1385, 0};
        goal.z = TheTerrainLogic->getGroundHeight(goal.x, goal.y);
        GroupMovement result;
        // The gameplay map is static for this measured batch. Warm its
        // immutable terrain/HPA capture before issuing the first commands so
        // setup invalidations cannot be charged to a movement frame.
        pathfinder->m_groundPlanner->warmStaticSnapshot();
        result.created = unsigned(units.size());
        using Clock = std::chrono::steady_clock;
        auto began = Clock::now();
        group->groupMoveToPosition(&goal, false, CMD_FROM_PLAYER);
        result.commandMilliseconds = std::chrono::duration<double, std::milli>(Clock::now() - began).count();
        for (auto& unit:units) {
            auto* object=TheGameLogic->findObjectByID(unit.id);
            auto* ai=object->getAIUpdateInterface();
            const auto* destination=ai->getGoalPosition();
            unit.goal=*destination;
            if (!pathfinder->validMovementPosition(object->getCrusherLevel()>0,
                TheTerrainLogic->getLayerForDestination(destination),ai->getLocomotorSet(),destination)) {
                ++result.invalidInitialGoals;
                if (result.stalled.size()<16) {
                    std::ostringstream detail;
                    detail << "invalid_initial_goal id=" << unsigned(unit.id)
                        << " start=" << unit.start.x << ',' << unit.start.y
                        << " goal=" << destination->x << ',' << destination->y << ',' << destination->z;
                    ICoord2D coordinate;
                    pathfinder->worldToCell(destination,&coordinate);
                    const auto layer=TheTerrainLogic->getLayerForDestination(destination);
                    auto* cell=pathfinder->getCell(layer,coordinate.x,coordinate.y);
                    detail << " layer=" << int(layer) << " cell_type=" << (cell?int(cell->getType()):-1)
                        << " ignored=" << unsigned(ai->getIgnoredObstacleID());
                    result.stalled.push_back(detail.str());
                }
            }
        }
        const auto firstFrame = TheGameLogic->getFrame();
        if (auditOccupancy) auditIncrementalOccupancy();
        for (unsigned i = 0; i < frames; ++i) {
            began = Clock::now();
            TheGameLogic->UPDATE();
            const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - began).count();
            if (auditOccupancy) { auditIncrementalOccupancy(i%5==0);++result.captureAudits; }
            if (i==0) result.pendingAfterFirstUpdate=static_cast<unsigned>(pathfinder->m_pathRequests->size());
            for (auto& unit:units) {
                auto* object=TheGameLogic->findObjectByID(unit.id);
                if (!object) continue;
                const auto& position=*object->getPosition();
                unit.bestGoalDistance=std::min(unit.bestGoalDistance,std::hypot(position.x-unit.goal.x,position.y-unit.goal.y));
                if (!unit.firstIdleFrame && object->getAIUpdateInterface()->getCurrentStateID()==AI_IDLE) {
                    unit.firstIdleFrame=i+1;unit.firstIdlePosition=position;
                }
            }
            if (captureCRCs) result.frameCRCs.push_back(TheGameLogic->getCRC(CRC_RECALC));
            result.updateMilliseconds += elapsed;
            result.worstUpdateMilliseconds = (std::max)(result.worstUpdateMilliseconds, elapsed);
            const auto queueStats=pathfinder->getNavigationStats();
            result.pathfindQueueMilliseconds += queueStats.lastQueueNanoseconds/1000000.0;
            result.worstPathfindQueueMilliseconds=(std::max)(result.worstPathfindQueueMilliseconds,
                queueStats.lastQueueNanoseconds/1000000.0);
            result.requestMilliseconds += queueStats.lastRequestNanoseconds/1000000.0;
            result.worstRequestMilliseconds=(std::max)(result.worstRequestMilliseconds,
                queueStats.lastRequestNanoseconds/1000000.0);
            result.dispatchMilliseconds += queueStats.lastDispatchNanoseconds/1000000.0;
            result.worstDispatchMilliseconds=(std::max)(result.worstDispatchMilliseconds,
                queueStats.lastDispatchNanoseconds/1000000.0);
            result.sliceMilliseconds += queueStats.lastSliceNanoseconds/1000000.0;
            result.commitMilliseconds += queueStats.lastCommitNanoseconds/1000000.0;
            result.maximumSliceMilliseconds=(std::max)(result.maximumSliceMilliseconds,
                queueStats.maximumSliceNanoseconds/1000000.0);
            if (queueStats.maximumSliceNanoseconds >=
                result.maximumSliceMilliseconds*1000000.0) {
                result.maximumSliceObject=queueStats.maximumSliceObject;
                result.maximumSliceWork=queueStats.maximumSliceWork;
                result.maximumSliceStartX=queueStats.maximumSliceStartX;
                result.maximumSliceStartY=queueStats.maximumSliceStartY;
                result.maximumSliceGoalX=queueStats.maximumSliceGoalX;
                result.maximumSliceGoalY=queueStats.maximumSliceGoalY;
            }
            const auto work = static_cast<unsigned>(pathfinder->m_cumulativeCellsAllocated);
            result.navigationWork += work;
            result.maximumNavigationWork = (std::max)(result.maximumNavigationWork, work);
            if (work >= CellsPerFrame) ++result.budgetExhaustedUpdates;
        }
        if (TheGameLogic->getFrame() != firstFrame + frames)
            throw std::runtime_error("Group simulation did not advance all requested frames");
        for (const auto& unit : units) {
            auto* object = TheGameLogic->findObjectByID(unit.id);
            if (!object) continue;
            ++result.alive;
            const auto& end = *object->getPosition();
            auto* ai = object->getAIUpdateInterface();
            if (ai->isWaitingForPath()) ++result.waiting;
            const auto& destination = *ai->getGoalPosition();
            // Once an order has completed, DX9 idle units may yield to later
            // traffic. Check the first idle transition against the original
            // assigned goal, not the position after subsequent yield orders.
            const bool abandoned = unit.firstIdleFrame &&
                std::hypot(unit.firstIdlePosition.x-unit.goal.x,
                    unit.firstIdlePosition.y-unit.goal.y)>100;
            if (abandoned) ++result.abandoned;
            if (std::hypot(end.x - unit.start.x, end.y - unit.start.y) > 100) ++result.advanced;
            const bool closer=std::hypot(end.x - goal.x, end.y - goal.y) + 100 <
                std::hypot(unit.start.x - goal.x, unit.start.y - goal.y);
            if (closer) ++result.closer;
            if ((abandoned && result.abandoned <= 16) ||
                (!closer && result.stalled.size()<16) ||
                (std::hypot(end.x - unit.start.x, end.y - unit.start.y) <= 100 && result.stalled.size() < 16)) {
                std::ostringstream detail;
                detail << "abandoned=" << abandoned << " id=" << unsigned(unit.id) << " start=" << unit.start.x << ',' << unit.start.y
                    << " end=" << end.x << ',' << end.y << " goal=" << destination.x << ',' << destination.y
                    << " state=" << ai->getCurrentStateID() << " blocked=" << ai->getNumFramesBlocked()
                    << " path=" << (ai->getPath() != nullptr) << " waiting=" << ai->isWaitingForPath()
                    << " moving=" << ai->isMoving() << " retry=" << ai->getRetryPath()
                    << " queued=" << pathfinder->m_pathRequests->contains(unsigned(unit.id))
                    << " pending=" << pathfinder->isGroundPathPending(unit.id)
                    << " cooldown=" << ai->m_queueForPathFrame << " frame=" << TheGameLogic->getFrame();
                detail << " best_goal_distance=" << unit.bestGoalDistance << " first_idle_frame=" << unit.firstIdleFrame
                    << " first_idle_position=" << unit.firstIdlePosition.x << ',' << unit.firstIdlePosition.y;
                if (auto* path = ai->getPath()) {
                    ClosestPointOnPathInfo point{};
                    path->computePointOnPath(object,ai->getLocomotorSet(),end,point);
                    const auto& velocity=*object->getPhysics()->getVelocity();
                    detail << " velocity=" << velocity.x << ',' << velocity.y
                        << " pathTarget=" << point.posOnPath.x << ',' << point.posOnPath.y;
                    const float ground=TheTerrainLogic->getLayerHeight(end.x,end.y,object->getLayer());
                    const float turn=std::remainder(std::atan2(point.posOnPath.y-end.y,point.posOnPath.x-end.x)-
                        object->getOrientation(),2*3.14159265358979323846f);
                    detail << " turnRadians=" << turn << " aboveSurface=" << end.z-ground
                        << " airborneThreshold=" << -9*TheGlobalData->m_gravity
                        << " terrainValid=" << pathfinder->validMovementTerrain(object->getLayer(),ai->getCurLocomotor(),&end)
                        << " airborneDrive=" << ai->getCurLocomotor()->getAllowMotiveForceWhileAirborne();
                    detail << " nodes=";
                    unsigned shown = 0;
                    for (auto* node = path->getFirstNode(); node && shown < 16; node = node->getNextOptimized(), ++shown)
                        detail << '(' << node->getPosition()->x << ',' << node->getPosition()->y << ')';
                }
                detail << " neighbors=";
                for (const auto& neighbor : units) {
                    if (neighbor.id == unit.id) continue;
                    const auto* other = TheGameLogic->findObjectByID(neighbor.id);
                    if (!other) continue;
                    const auto& p = *other->getPosition();
                    if (std::hypot(p.x - end.x, p.y - end.y) < 60)
                        detail << '(' << unsigned(neighbor.id) << ':' << p.x << ',' << p.y << ')';
                }
                result.stalled.push_back(detail.str());
            }
            for (auto value : {std::uint32_t(unit.id), std::bit_cast<std::uint32_t>(end.x),
                    std::bit_cast<std::uint32_t>(end.y), std::bit_cast<std::uint32_t>(end.z)}) {
                result.digest ^= value;
                result.digest *= 1099511628211ull;
            }
        }
        if (result.advanced<count) {
            const auto& planner=*pathfinder->m_groundPlanner;
            for (const auto& [id,task]:planner.state_->pending) {
                std::ostringstream detail;
                const auto* obj=TheGameLogic->findObjectByID(id);
                const auto* ai=obj?obj->getAIUpdateInterface():nullptr;
                detail << "pending id=" << unsigned(id) << " started=" << task.started
                    << " canstart=" << task.canStart() << " done=" << task.done()
                    << " obsolete=" << task.obsolete() << " work=" << bool(task.slice && task.slice->work)
                    << " queued=" << pathfinder->m_pathRequests->contains(unsigned(id))
                    << " wait=" << bool(ai && ai->isWaitingForPath())
                    << " captured=" << planner.state_->leasedCapturedScratch;
                result.stalled.push_back(detail.str());
            }
        }
        return result;
    }
    std::string userDataPath() const { return TheGlobalData->getPath_UserData().str(); }
    std::string expectedDataPath() const { return dataRoot.string() + "\\"; }
};
}
