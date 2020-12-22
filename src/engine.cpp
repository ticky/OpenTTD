/* $Id$ */

/** @file engine.cpp Base for all engine handling. */

#include "stdafx.h"
#include "openttd.h"
#include "debug.h"
#include "engine.h"
#include "player_base.h"
#include "player_func.h"
#include "command_func.h"
#include "news.h"
#include "saveload.h"
#include "variables.h"
#include "train.h"
#include "aircraft.h"
#include "newgrf_cargo.h"
#include "group.h"
#include "strings_func.h"
#include "gfx_func.h"
#include "functions.h"
#include "window_func.h"
#include "date_func.h"
#include "autoreplace_base.h"
#include "autoreplace_gui.h"
#include "string_func.h"
#include "settings_type.h"

#include "table/strings.h"
#include "table/engines.h"

#include "safeguards.h"

Engine _engines[TOTAL_NUM_ENGINES];
EngineInfo _engine_info[TOTAL_NUM_ENGINES];
RailVehicleInfo _rail_vehicle_info[NUM_TRAIN_ENGINES];
ShipVehicleInfo _ship_vehicle_info[NUM_SHIP_ENGINES];
AircraftVehicleInfo _aircraft_vehicle_info[NUM_AIRCRAFT_ENGINES];
RoadVehicleInfo _road_vehicle_info[NUM_ROAD_ENGINES];

enum {
	YEAR_ENGINE_AGING_STOPS = 2050,
};


void SetupEngines()
{
	/* Copy original static engine data */
	memcpy(&_engine_info, &_orig_engine_info, sizeof(_orig_engine_info));
	memcpy(&_rail_vehicle_info, &_orig_rail_vehicle_info, sizeof(_orig_rail_vehicle_info));
	memcpy(&_ship_vehicle_info, &_orig_ship_vehicle_info, sizeof(_orig_ship_vehicle_info));
	memcpy(&_aircraft_vehicle_info, &_orig_aircraft_vehicle_info, sizeof(_orig_aircraft_vehicle_info));
	memcpy(&_road_vehicle_info, &_orig_road_vehicle_info, sizeof(_orig_road_vehicle_info));

	/* Add type to engines */
	Engine* e = _engines;
	do e->type = VEH_TRAIN;    while (++e < &_engines[ROAD_ENGINES_INDEX]);
	do e->type = VEH_ROAD;     while (++e < &_engines[SHIP_ENGINES_INDEX]);
	do e->type = VEH_SHIP;     while (++e < &_engines[AIRCRAFT_ENGINES_INDEX]);
	do e->type = VEH_AIRCRAFT; while (++e < &_engines[TOTAL_NUM_ENGINES]);

	/* Set up default engine names */
	for (EngineID engine = 0; engine < TOTAL_NUM_ENGINES; engine++) {
		EngineInfo *ei = &_engine_info[engine];
		ei->string_id = STR_8000_KIRBY_PAUL_TANK_STEAM + engine;
	}
}


void ShowEnginePreviewWindow(EngineID engine);

void DeleteCustomEngineNames()
{
	Engine *e;
	FOR_ALL_ENGINES(e) {
		free(e->name);
		e->name = NULL;
	}

	_vehicle_design_names &= ~1;
}

void LoadCustomEngineNames()
{
	/* XXX: not done */
	DEBUG(misc, 1, "LoadCustomEngineNames: not done");
}

static void CalcEngineReliability(Engine *e)
{
	uint age = e->age;

	/* Check for early retirement */
	if (e->player_avail != 0 && !_patches.never_expire_vehicles) {
		int retire_early = EngInfo(e - _engines)->retire_early;
		uint retire_early_max_age = max(0, e->duration_phase_1 + e->duration_phase_2 - retire_early * 12);
		if (retire_early != 0 && age >= retire_early_max_age) {
			/* Early retirement is enabled and we're past the date... */
			e->player_avail = 0;
			AddRemoveEngineFromAutoreplaceAndBuildWindows(e->type);
		}
	}

	if (age < e->duration_phase_1) {
		uint start = e->reliability_start;
		e->reliability = age * (e->reliability_max - start) / e->duration_phase_1 + start;
	} else if ((age -= e->duration_phase_1) < e->duration_phase_2 || _patches.never_expire_vehicles) {
		/* We are at the peak of this engines life. It will have max reliability.
		 * This is also true if the engines never expire. They will not go bad over time */
		e->reliability = e->reliability_max;
	} else if ((age -= e->duration_phase_2) < e->duration_phase_3) {
		uint max = e->reliability_max;
		e->reliability = (int)age * (int)(e->reliability_final - max) / e->duration_phase_3 + max;
	} else {
		/* time's up for this engine.
		 * We will now completely retire this design */
		e->player_avail = 0;
		e->reliability = e->reliability_final;
		/* Kick this engine out of the lists */
		AddRemoveEngineFromAutoreplaceAndBuildWindows(e->type);
	}
	InvalidateWindowClasses(WC_BUILD_VEHICLE); // Update to show the new reliability
	InvalidateWindowClasses(WC_REPLACE_VEHICLE);
}

void StartupEngines()
{
	Engine *e;
	const EngineInfo *ei;
	/* Aging of vehicles stops, so account for that when starting late */
	const Date aging_date = min(_date, ConvertYMDToDate(YEAR_ENGINE_AGING_STOPS, 0, 1));

	for (e = _engines, ei = _engine_info; e != endof(_engines); e++, ei++) {
		uint32 r;

		e->age = 0;
		e->flags = 0;
		e->player_avail = 0;

		/* The magic value of 729 days below comes from the NewGRF spec. If the
		 * base intro date is before 1922 then the random number of days is not
		 * added. */
		r = Random();
		e->intro_date = ei->base_intro <= ConvertYMDToDate(1922, 0, 1) ? ei->base_intro : (Date)GB(r, 0, 9) + ei->base_intro;
		if (e->intro_date <= _date) {
			e->age = (aging_date - e->intro_date) >> 5;
			e->player_avail = (byte)-1;
			e->flags |= ENGINE_AVAILABLE;
		}

		e->reliability_start = GB(r, 16, 14) + 0x7AE0;
		r = Random();
		e->reliability_max   = GB(r,  0, 14) + 0xBFFF;
		e->reliability_final = GB(r, 16, 14) + 0x3FFF;

		r = Random();
		e->duration_phase_1 = GB(r, 0, 5) + 7;
		e->duration_phase_2 = GB(r, 5, 4) + ei->base_life * 12 - 96;
		e->duration_phase_3 = GB(r, 9, 7) + 120;

		e->reliability_spd_dec = (ei->unk2&0x7F) << 2;

		/* my invented flag for something that is a wagon */
		if (ei->unk2 & 0x80) {
			e->age = 0xFFFF;
		} else {
			CalcEngineReliability(e);
		}

		e->lifelength = ei->lifelength + _patches.extend_vehicle_life;

		/* prevent certain engines from ever appearing. */
		if (!HasBit(ei->climates, _opt.landscape)) {
			e->flags |= ENGINE_AVAILABLE;
			e->player_avail = 0;
		}
	}

	/* Update the bitmasks for the vehicle lists */
	Player *p;
	FOR_ALL_PLAYERS(p) {
		p->avail_railtypes = GetPlayerRailtypes(p->index);
		p->avail_roadtypes = GetPlayerRoadtypes(p->index);
	}
}

static void AcceptEnginePreview(EngineID eid, PlayerID player)
{
	Engine *e = GetEngine(eid);
	Player *p = GetPlayer(player);

	SetBit(e->player_avail, player);
	if (e->type == VEH_TRAIN) {
		const RailVehicleInfo *rvi = RailVehInfo(eid);

		assert(rvi->railtype < RAILTYPE_END);
		SetBit(p->avail_railtypes, rvi->railtype);
	} else if (e->type == VEH_ROAD) {
		SetBit(p->avail_roadtypes, HasBit(EngInfo(eid)->misc_flags, EF_ROAD_TRAM) ? ROADTYPE_TRAM : ROADTYPE_ROAD);
	}

	e->preview_player_rank = 0xFF;
	if (player == _local_player) {
		AddRemoveEngineFromAutoreplaceAndBuildWindows(e->type);
	}
}

static PlayerID GetBestPlayer(uint8 pp)
{
	const Player *p;
	int32 best_hist;
	PlayerID best_player;
	uint mask = 0;

	do {
		best_hist = -1;
		best_player = PLAYER_SPECTATOR;
		FOR_ALL_PLAYERS(p) {
			if (p->is_active && p->block_preview == 0 && !HasBit(mask, p->index) &&
					p->old_economy[0].performance_history > best_hist) {
				best_hist = p->old_economy[0].performance_history;
				best_player = p->index;
			}
		}

		if (best_player == PLAYER_SPECTATOR) return PLAYER_SPECTATOR;

		SetBit(mask, best_player);
	} while (--pp != 0);

	return best_player;
}

void EnginesDailyLoop()
{
	EngineID i;

	if (_cur_year >= YEAR_ENGINE_AGING_STOPS) return;

	for (i = 0; i != lengthof(_engines); i++) {
		Engine *e = &_engines[i];

		if (e->flags & ENGINE_EXCLUSIVE_PREVIEW) {
			if (e->flags & ENGINE_OFFER_WINDOW_OPEN) {
				if (e->preview_player_rank != 0xFF && !--e->preview_wait) {
					e->flags &= ~ENGINE_OFFER_WINDOW_OPEN;
					DeleteWindowById(WC_ENGINE_PREVIEW, i);
					e->preview_player_rank++;
				}
			} else if (e->preview_player_rank != 0xFF) {
				PlayerID best_player = GetBestPlayer(e->preview_player_rank);

				if (best_player == PLAYER_SPECTATOR) {
					e->preview_player_rank = 0xFF;
					continue;
				}

				if (!IsHumanPlayer(best_player)) {
					/* XXX - TTDBUG: TTD has a bug here ???? */
					AcceptEnginePreview(i, best_player);
				} else {
					e->flags |= ENGINE_OFFER_WINDOW_OPEN;
					e->preview_wait = 20;
					if (IsInteractivePlayer(best_player)) ShowEnginePreviewWindow(i);
				}
			}
		}
	}
}

/** Accept an engine prototype. XXX - it is possible that the top-player
 * changes while you are waiting to accept the offer? Then it becomes invalid
 * @param tile unused
 * @param flags operation to perfom
 * @param p1 engine-prototype offered
 * @param p2 unused
 */
CommandCost CmdWantEnginePreview(TileIndex tile, uint32 flags, uint32 p1, uint32 p2)
{
	Engine *e;

	if (!IsEngineIndex(p1)) return CMD_ERROR;
	e = GetEngine(p1);
	if (GetBestPlayer(e->preview_player_rank) != _current_player) return CMD_ERROR;

	if (flags & DC_EXEC) AcceptEnginePreview(p1, _current_player);

	return CommandCost();
}

/* Determine if an engine type is a wagon (and not a loco) */
static bool IsWagon(EngineID index)
{
	return index < NUM_TRAIN_ENGINES && RailVehInfo(index)->railveh_type == RAILVEH_WAGON;
}

static void NewVehicleAvailable(Engine *e)
{
	Vehicle *v;
	Player *p;
	EngineID index = e - _engines;

	/* In case the player didn't build the vehicle during the intro period,
	 * prevent that player from getting future intro periods for a while. */
	if (e->flags & ENGINE_EXCLUSIVE_PREVIEW) {
		FOR_ALL_PLAYERS(p) {
			uint block_preview = p->block_preview;

			if (!HasBit(e->player_avail, p->index)) continue;

			/* We assume the user did NOT build it.. prove me wrong ;) */
			p->block_preview = 20;

			FOR_ALL_VEHICLES(v) {
				if (v->type == VEH_TRAIN || v->type == VEH_ROAD || v->type == VEH_SHIP ||
						(v->type == VEH_AIRCRAFT && IsNormalAircraft(v))) {
					if (v->owner == p->index && v->engine_type == index) {
						/* The user did prove me wrong, so restore old value */
						p->block_preview = block_preview;
						break;
					}
				}
			}
		}
	}

	e->flags = (e->flags & ~ENGINE_EXCLUSIVE_PREVIEW) | ENGINE_AVAILABLE;
	AddRemoveEngineFromAutoreplaceAndBuildWindows(e->type);

	/* Now available for all players */
	e->player_avail = (byte)-1;

	/* Do not introduce new rail wagons */
	if (IsWagon(index)) return;

	if (e->type == VEH_TRAIN) {
		/* maybe make another rail type available */
		RailType railtype = RailVehInfo(index)->railtype;
		assert(railtype < RAILTYPE_END);
		FOR_ALL_PLAYERS(p) {
			if (p->is_active) SetBit(p->avail_railtypes, railtype);
		}
	} else if (e->type == VEH_ROAD) {
		/* maybe make another road type available */
		FOR_ALL_PLAYERS(p) {
			if (p->is_active) SetBit(p->avail_roadtypes, HasBit(EngInfo(index)->misc_flags, EF_ROAD_TRAM) ? ROADTYPE_TRAM : ROADTYPE_ROAD);
		}
	}
	AddNewsItem(index, NEWS_FLAGS(NM_CALLBACK, 0, NT_NEW_VEHICLES, DNC_VEHICLEAVAIL), 0, 0);
}

void EnginesMonthlyLoop()
{
	if (_cur_year < YEAR_ENGINE_AGING_STOPS) {
		Engine *e;
		FOR_ALL_ENGINES(e) {
			/* Age the vehicle */
			if (e->flags & ENGINE_AVAILABLE && e->age != 0xFFFF) {
				e->age++;
				CalcEngineReliability(e);
			}

			if (!(e->flags & ENGINE_AVAILABLE) && _date >= (e->intro_date + 365)) {
				/* Introduce it to all players */
				NewVehicleAvailable(e);
			} else if (!(e->flags & (ENGINE_AVAILABLE|ENGINE_EXCLUSIVE_PREVIEW)) && _date >= e->intro_date) {
				/* Introduction date has passed.. show introducing dialog to one player. */
				e->flags |= ENGINE_EXCLUSIVE_PREVIEW;

				/* Do not introduce new rail wagons */
				if (!IsWagon(e - _engines))
					e->preview_player_rank = 1; // Give to the player with the highest rating.
			}
		}
	}
}

static bool IsUniqueEngineName(const char *name)
{
	char buf[512];

	for (EngineID i = 0; i < TOTAL_NUM_ENGINES; i++) {
		SetDParam(0, i);
		GetString(buf, STR_ENGINE_NAME, lastof(buf));
		if (strcmp(buf, name) == 0) return false;
	}

	return true;
}

/** Rename an engine.
 * @param tile unused
 * @param flags operation to perfom
 * @param p1 engine ID to rename
 * @param p2 unused
 */
CommandCost CmdRenameEngine(TileIndex tile, uint32 flags, uint32 p1, uint32 p2)
{
	if (!IsEngineIndex(p1) || StrEmpty(_cmd_text)) return CMD_ERROR;

	if (!IsUniqueEngineName(_cmd_text)) return_cmd_error(STR_NAME_MUST_BE_UNIQUE);

	if (flags & DC_EXEC) {
		Engine *e = GetEngine(p1);
		free(e->name);
		e->name = strdup(_cmd_text);
		_vehicle_design_names |= 3;
		MarkWholeScreenDirty();
	}

	return CommandCost();
}


/** Check if an engine is buildable.
 * @param engine index of the engine to check.
 * @param type   the type the engine should be.
 * @param player index of the player.
 * @return True if an engine is valid, of the specified type, and buildable by
 *              the given player.
 */
bool IsEngineBuildable(EngineID engine, VehicleType type, PlayerID player)
{
	/* check if it's an engine that is in the engine array */
	if (!IsEngineIndex(engine)) return false;

	const Engine *e = GetEngine(engine);

	/* check if it's an engine of specified type */
	if (e->type != type) return false;

	/* check if it's available */
	if (!HasBit(e->player_avail, player)) return false;

	if (type == VEH_TRAIN) {
		/* Check if the rail type is available to this player */
		const Player *p = GetPlayer(player);
		if (!HasBit(p->avail_railtypes, RailVehInfo(engine)->railtype)) return false;
	}

	return true;
}

/** Get the default cargo type for a certain engine type
 * @param engine The ID to get the cargo for
 * @return The cargo type. CT_INVALID means no cargo capacity
 */
CargoID GetEngineCargoType(EngineID engine)
{
	assert(IsEngineIndex(engine));

	switch (GetEngine(engine)->type) {
		case VEH_TRAIN:
			if (RailVehInfo(engine)->capacity == 0) return CT_INVALID;
			return RailVehInfo(engine)->cargo_type;

		case VEH_ROAD:
			if (RoadVehInfo(engine)->capacity == 0) return CT_INVALID;
			return RoadVehInfo(engine)->cargo_type;

		case VEH_SHIP:
			if (ShipVehInfo(engine)->capacity == 0) return CT_INVALID;
			return ShipVehInfo(engine)->cargo_type;

		case VEH_AIRCRAFT:
			/* all aircraft starts as passenger planes with cargo capacity */
			return CT_PASSENGERS;

		default: NOT_REACHED(); return CT_INVALID;
	}
}

/************************************************************************
 * Engine Replacement stuff
 ************************************************************************/

DEFINE_OLD_POOL_GENERIC(EngineRenew, EngineRenew)

/**
 * Retrieves the EngineRenew that specifies the replacement of the given
 * engine type from the given renewlist */
static EngineRenew *GetEngineReplacement(EngineRenewList erl, EngineID engine, GroupID group)
{
	EngineRenew *er = (EngineRenew *)erl;

	while (er) {
		if (er->from == engine && er->group_id == group) return er;
		er = er->next;
	}
	return NULL;
}

void RemoveAllEngineReplacement(EngineRenewList *erl)
{
	EngineRenew *er = (EngineRenew *)(*erl);
	EngineRenew *next;

	while (er != NULL) {
		next = er->next;
		delete er;
		er = next;
	}
	*erl = NULL; // Empty list
}

EngineID EngineReplacement(EngineRenewList erl, EngineID engine, GroupID group)
{
	const EngineRenew *er = GetEngineReplacement(erl, engine, group);
	if (er == NULL && (group == DEFAULT_GROUP || (IsValidGroupID(group) && !GetGroup(group)->replace_protection))) {
		/* We didn't find anything useful in the vehicle's own group so we will try ALL_GROUP */
		er = GetEngineReplacement(erl, engine, ALL_GROUP);
	}
	return er == NULL ? INVALID_ENGINE : er->to;
}

CommandCost AddEngineReplacement(EngineRenewList *erl, EngineID old_engine, EngineID new_engine, GroupID group, uint32 flags)
{
	EngineRenew *er;

	/* Check if the old vehicle is already in the list */
	er = GetEngineReplacement(*erl, old_engine, group);
	if (er != NULL) {
		if (flags & DC_EXEC) er->to = new_engine;
		return CommandCost();
	}

	if (!EngineRenew::CanAllocateItem()) return CMD_ERROR;

	if (flags & DC_EXEC) {
		er = new EngineRenew(old_engine, new_engine);
		er->group_id = group;

		/* Insert before the first element */
		er->next = (EngineRenew *)(*erl);
		*erl = (EngineRenewList)er;
	}

	return CommandCost();
}

CommandCost RemoveEngineReplacement(EngineRenewList *erl, EngineID engine, GroupID group, uint32 flags)
{
	EngineRenew *er = (EngineRenew *)(*erl);
	EngineRenew *prev = NULL;

	while (er)
	{
		if (er->from == engine && er->group_id == group) {
			if (flags & DC_EXEC) {
				if (prev == NULL) { // First element
					/* The second becomes the new first element */
					*erl = (EngineRenewList)er->next;
				} else {
					/* Cut this element out */
					prev->next = er->next;
				}
				delete er;
			}
			return CommandCost();
		}
		prev = er;
		er = er->next;
	}

	return CMD_ERROR;
}

static const SaveLoad _engine_renew_desc[] = {
	    SLE_VAR(EngineRenew, from,     SLE_UINT16),
	    SLE_VAR(EngineRenew, to,       SLE_UINT16),

	    SLE_REF(EngineRenew, next,     REF_ENGINE_RENEWS),
	SLE_CONDVAR(EngineRenew, group_id, SLE_UINT16, 60, SL_MAX_VERSION),
	SLE_END()
};

static void Save_ERNW()
{
	EngineRenew *er;

	FOR_ALL_ENGINE_RENEWS(er) {
		SlSetArrayIndex(er->index);
		SlObject(er, _engine_renew_desc);
	}
}

static void Load_ERNW()
{
	int index;

	while ((index = SlIterateArray()) != -1) {
		EngineRenew *er = new (index) EngineRenew();
		SlObject(er, _engine_renew_desc);

		/* Advanced vehicle lists, ungrouped vehicles got added */
		if (CheckSavegameVersion(60)) {
			er->group_id = ALL_GROUP;
		} else if (CheckSavegameVersion(71)) {
			if (er->group_id == DEFAULT_GROUP) er->group_id = ALL_GROUP;
		}
	}
}

static const SaveLoad _engine_desc[] = {
	SLE_CONDVAR(Engine, intro_date,          SLE_FILE_U16 | SLE_VAR_I32,  0,  30),
	SLE_CONDVAR(Engine, intro_date,          SLE_INT32,                  31, SL_MAX_VERSION),
	SLE_CONDVAR(Engine, age,                 SLE_FILE_U16 | SLE_VAR_I32,  0,  30),
	SLE_CONDVAR(Engine, age,                 SLE_INT32,                  31, SL_MAX_VERSION),
	    SLE_VAR(Engine, reliability,         SLE_UINT16),
	    SLE_VAR(Engine, reliability_spd_dec, SLE_UINT16),
	    SLE_VAR(Engine, reliability_start,   SLE_UINT16),
	    SLE_VAR(Engine, reliability_max,     SLE_UINT16),
	    SLE_VAR(Engine, reliability_final,   SLE_UINT16),
	    SLE_VAR(Engine, duration_phase_1,    SLE_UINT16),
	    SLE_VAR(Engine, duration_phase_2,    SLE_UINT16),
	    SLE_VAR(Engine, duration_phase_3,    SLE_UINT16),

	    SLE_VAR(Engine, lifelength,          SLE_UINT8),
	    SLE_VAR(Engine, flags,               SLE_UINT8),
	    SLE_VAR(Engine, preview_player_rank, SLE_UINT8),
	    SLE_VAR(Engine, preview_wait,        SLE_UINT8),
	SLE_CONDNULL(1, 0, 44),
	    SLE_VAR(Engine, player_avail,        SLE_UINT8),
	SLE_CONDSTR(Engine, name,                SLE_STR, 0,                 84, SL_MAX_VERSION),

	/* reserve extra space in savegame here. (currently 16 bytes) */
	SLE_CONDNULL(16, 2, SL_MAX_VERSION),

	SLE_END()
};

static void Save_ENGN()
{
	uint i;

	for (i = 0; i != lengthof(_engines); i++) {
		SlSetArrayIndex(i);
		SlObject(&_engines[i], _engine_desc);
	}
}

static void Load_ENGN()
{
	int index;
	while ((index = SlIterateArray()) != -1) {
		SlObject(GetEngine(index), _engine_desc);
	}
}

static void Load_ENGS()
{
	StringID names[TOTAL_NUM_ENGINES];

	SlArray(names, lengthof(names), SLE_STRINGID);

	for (EngineID engine = 0; engine < lengthof(names); engine++) {
		Engine *e = GetEngine(engine);
		e->name = CopyFromOldName(names[engine]);
	}
}

extern const ChunkHandler _engine_chunk_handlers[] = {
	{ 'ENGN', Save_ENGN,     Load_ENGN,     CH_ARRAY          },
	{ 'ENGS', NULL,          Load_ENGS,     CH_RIFF           },
	{ 'ERNW', Save_ERNW,     Load_ERNW,     CH_ARRAY | CH_LAST},
};

void InitializeEngines()
{
	/* Clean the engine renew pool and create 1 block in it */
	_EngineRenew_pool.CleanPool();
	_EngineRenew_pool.AddBlockToPool();

	Engine *e;
	FOR_ALL_ENGINES(e) {
		free(e->name);
		e->name = NULL;
	}
}
