/* $Id$ */

/** @file openttd.h Some generic types. */

#ifndef OPENTTD_H
#define OPENTTD_H

#ifndef VARDEF
#define VARDEF extern
#endif

// Forward declarations of structs.
struct Depot;
struct Waypoint;
struct Station;
struct ViewPort;
struct NewsItem;
struct DrawPixelInfo;
struct Group;
typedef byte VehicleOrderID;  ///< The index of an order within its current vehicle (not pool related)
typedef byte LandscapeID;
typedef uint16 EngineID;
typedef uint16 UnitID;

typedef EngineID *EngineList; ///< engine list type placeholder acceptable for C code (see helpers.cpp)

/* IDs used in Pools */
typedef uint16 StationID;
static const StationID INVALID_STATION = 0xFFFF;
typedef uint16 RoadStopID;
typedef uint16 DepotID;
typedef uint16 WaypointID;
typedef uint16 OrderID;
typedef uint16 SignID;
typedef uint16 GroupID;
typedef uint16 EngineRenewID;
typedef uint16 DestinationID;

/* DestinationID must be at least as large as every these below, because it can
 * be any of them
 */
assert_compile(sizeof(DestinationID) >= sizeof(DepotID));
assert_compile(sizeof(DestinationID) >= sizeof(WaypointID));
assert_compile(sizeof(DestinationID) >= sizeof(StationID));

enum GameModes {
	GM_MENU,
	GM_NORMAL,
	GM_EDITOR
};

/** Mode which defines what mode we're switching to. */
enum SwitchModes {
	SM_NONE,
	SM_NEWGAME,         ///< New Game --> 'Random game'.
	SM_EDITOR,          ///< Switch to scenario editor.
	SM_LOAD_GAME,       ///< Load game, Play Scenario.
	SM_MENU,            ///< Switch to game intro menu.
	SM_SAVE_GAME,       ///< Save game.
	SM_SAVE_HEIGHTMAP,  ///< Save heightmap.
	SM_GENRANDLAND,     ///< Generate random land within scenario editor.
	SM_LOAD_SCENARIO,   ///< Load scenario from scenario editor.
	SM_START_SCENARIO,
	SM_START_HEIGHTMAP, ///< Load a heightmap and start a new game from it.
	SM_LOAD_HEIGHTMAP,  ///< Load heightmap from scenario editor.
};


/* Modes for GenerateWorld */
enum GenerateWorldModes {
	GW_NEWGAME   = 0,    /* Generate a map for a new game */
	GW_EMPTY     = 1,    /* Generate an empty map (sea-level) */
	GW_RANDOM    = 2,    /* Generate a random map for SE */
	GW_HEIGHTMAP = 3,    /* Generate a newgame from a heightmap */
};

/* Modes for InitializeGame, those are _bits_! */
enum InitializeGameModes {
	IG_NONE       = 0,  /* Don't do anything special */
	IG_DATE_RESET = 1,  /* Reset the date when initializing a game */
};

enum TransportType {
	/* These constants are for now linked to the representation of bridges
	 * and tunnels, so they can be used by GetTileTrackStatus_TunnelBridge.
	 * In an ideal world, these constants would be used everywhere when
	 * accessing tunnels and bridges. For now, you should just not change
	 * the values for road and rail.
	 */
	TRANSPORT_BEGIN = 0,
	TRANSPORT_RAIL = 0,
	TRANSPORT_ROAD = 1,
	TRANSPORT_WATER, // = 2
	TRANSPORT_END,
	INVALID_TRANSPORT = 0xff,
};

/* Display Options */
enum {
	DO_SHOW_TOWN_NAMES    = 0,
	DO_SHOW_STATION_NAMES = 1,
	DO_SHOW_SIGNS         = 2,
	DO_FULL_ANIMATION     = 3,
	DO_FULL_DETAIL        = 5,
	DO_WAYPOINTS          = 6,
};

/* Landscape types */
enum {
	LT_TEMPERATE  = 0,
	LT_ARCTIC     = 1,
	LT_TROPIC     = 2,
	LT_TOYLAND    = 3,

	NUM_LANDSCAPE = 4,
};

struct ViewportSign {
	int32 left;
	int32 top;
	uint16 width_1, width_2;
};

enum {
	SORT_ASCENDING  = 0,
	SORT_DESCENDING = 1,
	SORT_BY_DATE    = 0,
	SORT_BY_NAME    = 2
};

extern byte _savegame_sort_order;

enum {
	MAX_SCREEN_WIDTH  = 2048,
	MAX_SCREEN_HEIGHT = 1200,
};

/* In certain windows you navigate with the arrow keys. Do not scroll the
 * gameview when here. Bitencoded variable that only allows scrolling if all
 * elements are zero */
enum {
	SCROLL_CON  = 0,
	SCROLL_EDIT = 1,
	SCROLL_SAVE = 2,
	SCROLL_CHAT = 4,
};
extern byte _no_scroll;

/** To have a concurrently running thread interface with the main program, use
 * the OTTD_SendThreadMessage() function. Actions to perform upon the message are handled
 * in the ProcessSentMessage() function */
enum ThreadMsg {
	MSG_OTTD_NO_MESSAGE,
	MSG_OTTD_SAVETHREAD_DONE,
	MSG_OTTD_SAVETHREAD_ERROR,
};

void OTTD_SendThreadMessage(ThreadMsg msg);

extern byte _game_mode;
extern bool _exit_game;
extern byte _fast_forward;
extern int8 _pause_game;

#endif /* OPENTTD_H */
