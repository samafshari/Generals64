// GameSpyStubs.cpp - Stub definitions for removed GameSpy code.
// GameSpy has been removed from the build. These stubs provide nullptr
// definitions and no-op function implementations so that remaining code
// can still compile and link without GameSpy.

#include "PreRTS.h"

#include "GameNetwork/GameSpy/GSConfig.h"
#include "GameNetwork/GameSpy/PingThread.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/GameSpy/StagingRoomGameInfo.h"
#include "GameNetwork/GameSpy/LadderDefs.h"
#include "GameNetwork/GameSpy/GameResultsThread.h"
#include "GameNetwork/GameSpyThread.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/MainMenuUtils.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"
#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/ThreadUtils.h"
#include "GameNetwork/RankPointValue.h"
#include "GameNetwork/NAT.h"
#include "Common/INI.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/GameWindow.h"

// ---------------------------------------------------------------------------
// Extern pointer definitions (all null)
// ---------------------------------------------------------------------------
GameSpyConfigInterface *TheGameSpyConfig = nullptr;
PingerInterface *ThePinger = nullptr;
GameSpyInfoInterface *TheGameSpyInfo = nullptr;
GameSpyPeerMessageQueueInterface *TheGameSpyPeerMessageQueue = nullptr;
GameSpyBuddyMessageQueueInterface *TheGameSpyBuddyMessageQueue = nullptr;
GameSpyPSMessageQueueInterface *TheGameSpyPSMessageQueue = nullptr;
GameSpyStagingRoom *TheGameSpyGame = nullptr;
GameSpyThreadClass *TheGameSpyThread = nullptr;
MutexClass TheGameSpyMutex;
NAT *TheNAT = nullptr;
LadderList *TheLadderList = nullptr;

// GameSpyColor array (from Chat.cpp)
Color GameSpyColor[GSCOLOR_MAX] = {};

// ---------------------------------------------------------------------------
// GameSpyOverlay.cpp stubs
// ---------------------------------------------------------------------------
void ClearGSMessageBoxes() {}
void GSMessageBoxOk(UnicodeString, UnicodeString, GameWinMsgBoxFunc) {}
void GSMessageBoxOkCancel(UnicodeString, UnicodeString, GameWinMsgBoxFunc, GameWinMsgBoxFunc) {}
void GSMessageBoxYesNo(UnicodeString, UnicodeString, GameWinMsgBoxFunc, GameWinMsgBoxFunc) {}
void RaiseGSMessageBox() {}
void GameSpyOpenOverlay(GSOverlayType) {}
void GameSpyCloseOverlay(GSOverlayType) {}
void GameSpyCloseAllOverlays() {}
Bool GameSpyIsOverlayOpen(GSOverlayType) { return FALSE; }
void GameSpyToggleOverlay(GSOverlayType) {}
void GameSpyUpdateOverlays() {}
void ReOpenPlayerInfo() {}
void CheckReOpenPlayerInfo() {}

// ---------------------------------------------------------------------------
// MainMenuUtils.cpp stubs
// ---------------------------------------------------------------------------
void HTTPThinkWrapper() {}
void StopAsyncDNSCheck() {}
void StartPatchCheck() {}
void CancelPatchCheckCallback() {}
void StartDownloadingPatches() {}
// HandleCanceledDownload is defined in MainMenu.cpp
void CheckOverallStats() {}
void HandleOverallStats(const char*, unsigned) {}
void CheckNumPlayersOnline() {}
void HandleNumPlayersOnline(Int) {}

// ---------------------------------------------------------------------------
// LobbyUtils.cpp stubs
// ---------------------------------------------------------------------------
GameWindow *GetGameListBox() { return nullptr; }
GameWindow *GetGameInfoListBox() { return nullptr; }
NameKeyType GetGameListBoxID() { return NAMEKEY_INVALID; }
NameKeyType GetGameInfoListBoxID() { return NAMEKEY_INVALID; }
void GrabWindowInfo() {}
void ReleaseWindowInfo() {}
void RefreshGameInfoListBox(GameWindow*, GameWindow*) {}
void RefreshGameListBoxes() {}
void ToggleGameListType() {}
void playerTemplateComboBoxTooltip(GameWindow*, WinInstanceData*, UnsignedInt) {}
void playerTemplateListBoxTooltip(GameWindow*, WinInstanceData*, UnsignedInt) {}
Bool HandleSortButton(NameKeyType) { return FALSE; }
void PopulateLobbyPlayerListbox() {}

// ---------------------------------------------------------------------------
// BuddyDefs.cpp / PersistentStorageDefs.cpp stubs
// ---------------------------------------------------------------------------
void HandleBuddyResponses() {}
void PopulateOldBuddyMessages() {}
void HandlePersistentStorageResponses() {}
void UpdateLocalPlayerStats() {}
void SetLookAtPlayer(Int, AsciiString) {}
void PopulatePlayerInfoWindows(AsciiString) {}

// ---------------------------------------------------------------------------
// ThreadUtils.cpp stubs
// ---------------------------------------------------------------------------
std::wstring MultiByteToWideCharSingleLine(const char *orig)
{
	if (!orig) return std::wstring();
	int len = MultiByteToWideChar(CP_ACP, 0, orig, -1, nullptr, 0);
	std::wstring result(len, 0);
	MultiByteToWideChar(CP_ACP, 0, orig, -1, &result[0], len);
	// Strip newlines
	for (auto &c : result) { if (c == L'\n' || c == L'\r') c = L' '; }
	return result;
}

std::string WideCharStringToMultiByte(const WideChar *orig)
{
	if (!orig) return std::string();
	int len = WideCharToMultiByte(CP_ACP, 0, orig, -1, nullptr, 0, nullptr, nullptr);
	std::string result(len, 0);
	WideCharToMultiByte(CP_ACP, 0, orig, -1, &result[0], len, nullptr, nullptr);
	return result;
}

// ---------------------------------------------------------------------------
// PSPlayerStats (from PersistentStorageThread.cpp)
// ---------------------------------------------------------------------------
PSPlayerStats::PSPlayerStats() { reset(); }
PSPlayerStats::PSPlayerStats(const PSPlayerStats& other) { *this = other; }
void PSPlayerStats::reset()
{
	id = 0;
	locale = 0;
	gamesAsRandom = 0;
	lastFPS = 0;
	lastGeneral = 0;
	gamesInRowWithLastGeneral = 0;
	challengeMedals = 0;
	battleHonors = 0;
	QMwinsInARow = 0;
	maxQMwinsInARow = 0;
	winsInARow = 0;
	maxWinsInARow = 0;
	lossesInARow = 0;
	maxLossesInARow = 0;
	disconsInARow = 0;
	maxDisconsInARow = 0;
	desyncsInARow = 0;
	maxDesyncsInARow = 0;
	builtParticleCannon = 0;
	builtNuke = 0;
	builtSCUD = 0;
	lastLadderPort = 0;
}

void PSPlayerStats::incorporate(const PSPlayerStats&) {}

// ---------------------------------------------------------------------------
// StagingRoomGameInfo.cpp stubs
// ---------------------------------------------------------------------------
GameSpyGameSlot::GameSpyGameSlot() : m_profileID(0), m_wins(0), m_losses(0), m_rankPoints(0), m_favoriteSide(0), m_pingInt(0) {}
void GameSpyGameSlot::setPingString(AsciiString pingStr) { m_pingStr = pingStr; }

GameSpyStagingRoom::GameSpyStagingRoom() : m_id(0), m_transport(nullptr), m_requiresPassword(FALSE), m_allowObservers(FALSE), m_version(0), m_exeCRC(0), m_iniCRC(0), m_isQM(FALSE), m_pingInt(0), m_ladderPort(0), m_reportedNumPlayers(0), m_reportedMaxPlayers(0), m_reportedNumObservers(0) {}
void GameSpyStagingRoom::reset() { GameInfo::reset(); }
void GameSpyStagingRoom::cleanUpSlotPointers() {}
Bool GameSpyStagingRoom::amIHost() const { return FALSE; }
GameSpyGameSlot *GameSpyStagingRoom::getGameSpySlot(Int index) { return (index >= 0 && index < MAX_SLOTS) ? &m_GameSpySlot[index] : nullptr; }
AsciiString GameSpyStagingRoom::generateGameSpyGameResultsPacket() { return AsciiString::TheEmptyString; }
AsciiString GameSpyStagingRoom::generateLadderGameResultsPacket() { return AsciiString::TheEmptyString; }
void GameSpyStagingRoom::init() { GameInfo::init(); }
void GameSpyStagingRoom::resetAccepted() {}
void GameSpyStagingRoom::startGame(Int) {}
void GameSpyStagingRoom::launchGame() {}
Int GameSpyStagingRoom::getLocalSlotNum() const { return -1; }
void GameSpyStagingRoom::setPingString(AsciiString) {}

PSRequest::PSRequest() : addDiscon(FALSE), addDesync(FALSE), lastHouse(0) { requestType = PSREQUEST_READPLAYERSTATS; }

// ---------------------------------------------------------------------------
// LadderDefs.cpp stubs
// ---------------------------------------------------------------------------
LadderInfo::LadderInfo() : playersPerTeam(0), minWins(0), maxWins(0), randomMaps(FALSE), randomFactions(FALSE), validQM(FALSE), validCustom(FALSE), submitReplay(FALSE), port(0), index(0) {}
LadderList::LadderList() {}
LadderList::~LadderList() { /* leak prevention not needed for stubs */ }
const LadderInfo* LadderList::findLadder(const AsciiString&, UnsignedShort) { return nullptr; }
const LadderInfo* LadderList::findLadderByIndex(Int) { return nullptr; }
const LadderInfoList* LadderList::getLocalLadders() { return &m_localLadders; }
const LadderInfoList* LadderList::getSpecialLadders() { return &m_specialLadders; }
const LadderInfoList* LadderList::getStandardLadders() { return &m_standardLadders; }
void LadderList::loadLocalLadders() {}
void LadderList::checkLadder(AsciiString, Int) {}

// ---------------------------------------------------------------------------
// RankPointValue stubs (were in PopupPlayerInfo.cpp)
// ---------------------------------------------------------------------------
Int CalculateRank(const PSPlayerStats&) { return 0; }
Int GetFavoriteSide(const PSPlayerStats&) { return 0; }
const Image* LookupSmallRankImage(Int, Int) { return nullptr; }

// ---------------------------------------------------------------------------
// GameResultsThread.cpp stubs
// ---------------------------------------------------------------------------
class GameResultsInterfaceStub : public GameResultsInterface
{
public:
	void init() override {}
	void reset() override {}
	void update() override {}
	void startThreads() override {}
	void endThreads() override {}
	Bool areThreadsRunning() override { return FALSE; }
	void addRequest(const GameResultsRequest&) override {}
	Bool getRequest(GameResultsRequest&) override { return FALSE; }
	void addResponse(const GameResultsResponse&) override {}
	Bool getResponse(GameResultsResponse&) override { return FALSE; }
	Bool areGameResultsBeingSent() override { return FALSE; }
};

GameResultsInterface *TheGameResultsQueue = nullptr;
GameResultsInterface* GameResultsInterface::createNewGameResultsInterface()
{
	return MSGNEW("GameEngineSubsystem") GameResultsInterfaceStub;
}

// ---------------------------------------------------------------------------
// PersistentStorageThread.cpp stubs
// ---------------------------------------------------------------------------
std::string GameSpyPSMessageQueueInterface::formatPlayerKVPairs(PSPlayerStats) { return std::string(); }

// ---------------------------------------------------------------------------
// Chat.cpp stubs
// ---------------------------------------------------------------------------
void INI::parseOnlineChatColorDefinition(INI* ini)
{
	// Consume the OnlineChatColors block — use empty parse table to skip to End
	static const FieldParse emptyTable[] = { { nullptr, nullptr, nullptr, 0 } };
	ini->initFromINI(GameSpyColor, emptyTable);
}

// ---------------------------------------------------------------------------
// PeerDefs.cpp stubs
// ---------------------------------------------------------------------------
void TearDownGameSpy() {}

// ---------------------------------------------------------------------------
// PeerThread.cpp stubs
// ---------------------------------------------------------------------------
int isThreadHosting = 0;
extern "C" int getQR2HostingStatus() { return 0; }

// ---------------------------------------------------------------------------
// WOLLobbyMenu.cpp stubs
// ---------------------------------------------------------------------------
Bool DontShowMainMenu = FALSE;

// ---------------------------------------------------------------------------
// PopupPlayerInfo.cpp stubs
// ---------------------------------------------------------------------------
void GameSpyPlayerInfoOverlayInit(WindowLayout*, void*) {}
void GameSpyPlayerInfoOverlayUpdate(WindowLayout*, void*) {}
void GameSpyPlayerInfoOverlayShutdown(WindowLayout*, void*) {}
WindowMsgHandledType GameSpyPlayerInfoOverlaySystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType GameSpyPlayerInfoOverlayInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
void ResetBattleHonorInsertion() {}
void InsertBattleHonor(GameWindow*, const Image*, Bool, Int, Int&, Int&, UnicodeString, Int) {}
void BattleHonorTooltip(GameWindow*, WinInstanceData*, UnsignedInt) {}
Int GetAdditionalDisconnectsFromUserFile(Int) { return 0; }

// ---------------------------------------------------------------------------
// WOLBuddyOverlay.cpp stubs
// ---------------------------------------------------------------------------
void WOLBuddyOverlayInit(WindowLayout*, void*) {}
void WOLBuddyOverlayUpdate(WindowLayout*, void*) {}
void WOLBuddyOverlayShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLBuddyOverlaySystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLBuddyOverlayInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
void WOLBuddyOverlayRCMenuInit(WindowLayout*, void*) {}
WindowMsgHandledType WOLBuddyOverlayRCMenuSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType PopupBuddyNotificationSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
void InitBuddyControls(Int) {}
WindowMsgHandledType BuddyControlSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
void updateBuddyInfo() {}

// ---------------------------------------------------------------------------
// WOLCustomScoreScreen.cpp stubs
// ---------------------------------------------------------------------------
void WOLCustomScoreScreenInit(WindowLayout*, void*) {}
void WOLCustomScoreScreenUpdate(WindowLayout*, void*) {}
void WOLCustomScoreScreenShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLCustomScoreScreenSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLCustomScoreScreenInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// WOLGameSetupMenu.cpp stubs
// ---------------------------------------------------------------------------
void WOLGameSetupMenuInit(WindowLayout*, void*) {}
void WOLGameSetupMenuUpdate(WindowLayout*, void*) {}
void WOLGameSetupMenuShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLGameSetupMenuSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLGameSetupMenuInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
void gameAcceptTooltip(GameWindow*, WinInstanceData*, UnsignedInt) {}

// ---------------------------------------------------------------------------
// WOLLadderScreen.cpp stubs
// ---------------------------------------------------------------------------
void WOLLadderScreenInit(WindowLayout*, void*) {}
void WOLLadderScreenUpdate(WindowLayout*, void*) {}
void WOLLadderScreenShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLLadderScreenSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLLadderScreenInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// WOLLobbyMenu.cpp stubs
// ---------------------------------------------------------------------------
void WOLLobbyMenuInit(WindowLayout*, void*) {}
void WOLLobbyMenuUpdate(WindowLayout*, void*) {}
void WOLLobbyMenuShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLLobbyMenuSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLLobbyMenuInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// WOLLocaleSelectPopup.cpp stubs
// ---------------------------------------------------------------------------
void WOLLocaleSelectInit(WindowLayout*, void*) {}
void WOLLocaleSelectUpdate(WindowLayout*, void*) {}
void WOLLocaleSelectShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLLocaleSelectSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLLocaleSelectInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// WOLLoginMenu.cpp stubs
// ---------------------------------------------------------------------------
void WOLLoginMenuInit(WindowLayout*, void*) {}
void WOLLoginMenuUpdate(WindowLayout*, void*) {}
void WOLLoginMenuShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLLoginMenuSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLLoginMenuInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// WOLMapSelectMenu.cpp stubs
// ---------------------------------------------------------------------------
void WOLMapSelectMenuInit(WindowLayout*, void*) {}
void WOLMapSelectMenuUpdate(WindowLayout*, void*) {}
void WOLMapSelectMenuShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLMapSelectMenuSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLMapSelectMenuInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// WOLMessageWindow.cpp stubs
// ---------------------------------------------------------------------------
void WOLMessageWindowInit(WindowLayout*, void*) {}
void WOLMessageWindowUpdate(WindowLayout*, void*) {}
void WOLMessageWindowShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLMessageWindowSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLMessageWindowInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// WOLQMScoreScreen.cpp stubs
// ---------------------------------------------------------------------------
void WOLQMScoreScreenInit(WindowLayout*, void*) {}
void WOLQMScoreScreenUpdate(WindowLayout*, void*) {}
void WOLQMScoreScreenShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLQMScoreScreenSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLQMScoreScreenInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// WOLQuickMatchMenu.cpp stubs
// ---------------------------------------------------------------------------
void WOLQuickMatchMenuInit(WindowLayout*, void*) {}
void WOLQuickMatchMenuUpdate(WindowLayout*, void*) {}
void WOLQuickMatchMenuShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLQuickMatchMenuSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLQuickMatchMenuInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// WOLStatusMenu.cpp stubs
// ---------------------------------------------------------------------------
void WOLStatusMenuInit(WindowLayout*, void*) {}
void WOLStatusMenuUpdate(WindowLayout*, void*) {}
void WOLStatusMenuShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLStatusMenuSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLStatusMenuInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// WOLWelcomeMenu.cpp stubs
// ---------------------------------------------------------------------------
void WOLWelcomeMenuInit(WindowLayout*, void*) {}
void WOLWelcomeMenuUpdate(WindowLayout*, void*) {}
void WOLWelcomeMenuShutdown(WindowLayout*, void*) {}
WindowMsgHandledType WOLWelcomeMenuSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType WOLWelcomeMenuInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// PopupHostGame.cpp stubs
// ---------------------------------------------------------------------------
void PopupHostGameInit(WindowLayout*, void*) {}
void PopupHostGameUpdate(WindowLayout*, void*) {}
WindowMsgHandledType PopupHostGameSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType PopupHostGameInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// PopupJoinGame.cpp stubs
// ---------------------------------------------------------------------------
void PopupJoinGameInit(WindowLayout*, void*) {}
WindowMsgHandledType PopupJoinGameSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType PopupJoinGameInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }

// ---------------------------------------------------------------------------
// PopupLadderSelect.cpp stubs
// ---------------------------------------------------------------------------
void PopupLadderSelectInit(WindowLayout*, void*) {}
WindowMsgHandledType PopupLadderSelectSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
WindowMsgHandledType PopupLadderSelectInput(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
void RCGameDetailsMenuInit(WindowLayout*, void*) {}
WindowMsgHandledType RCGameDetailsMenuSystem(GameWindow*, UnsignedInt, WindowMsgData, WindowMsgData) { return MSG_IGNORED; }
