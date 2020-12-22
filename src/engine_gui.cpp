/* $Id$ */

/** @file engine_gui.cpp GUI to show engine related information. */

#include "stdafx.h"
#include "openttd.h"
#include "gui.h"
#include "window_gui.h"
#include "gfx_func.h"
#include "engine.h"
#include "command_func.h"
#include "economy_func.h"
#include "news.h"
#include "variables.h"
#include "newgrf_engine.h"
#include "strings_func.h"
#include "articulated_vehicles.h"

#include "table/strings.h"
#include "table/sprites.h"

#include "safeguards.h"

static StringID GetEngineCategoryName(EngineID engine)
{
	switch (GetEngine(engine)->type) {
		default: NOT_REACHED();
		case VEH_ROAD:              return STR_8103_ROAD_VEHICLE;
		case VEH_AIRCRAFT:          return STR_8104_AIRCRAFT;
		case VEH_SHIP:              return STR_8105_SHIP;
		case VEH_TRAIN:
			switch (RailVehInfo(engine)->railtype) {
				default: NOT_REACHED();
				case RAILTYPE_RAIL:     return STR_8102_RAILROAD_LOCOMOTIVE;
				case RAILTYPE_ELECTRIC: return STR_8102_RAILROAD_LOCOMOTIVE;
				case RAILTYPE_MONO:     return STR_8106_MONORAIL_LOCOMOTIVE;
				case RAILTYPE_MAGLEV:   return STR_8107_MAGLEV_LOCOMOTIVE;
			}
	}
}

static const Widget _engine_preview_widgets[] = {
{   WWT_CLOSEBOX,   RESIZE_NONE,     5,     0,    10,     0,    13, STR_00C5,                                  STR_018B_CLOSE_WINDOW},
{    WWT_CAPTION,   RESIZE_NONE,     5,    11,   299,     0,    13, STR_8100_MESSAGE_FROM_VEHICLE_MANUFACTURE, STR_018C_WINDOW_TITLE_DRAG_THIS},
{      WWT_PANEL,   RESIZE_NONE,     5,     0,   299,    14,   191, 0x0,                                       STR_NULL},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,     5,    85,   144,   172,   183, STR_00C9_NO,                               STR_NULL},
{ WWT_PUSHTXTBTN,   RESIZE_NONE,     5,   155,   214,   172,   183, STR_00C8_YES,                              STR_NULL},
{   WIDGETS_END},
};

typedef void DrawEngineProc(int x, int y, EngineID engine, PaletteID pal);
typedef void DrawEngineInfoProc(EngineID, int x, int y, int maxw);

struct DrawEngineInfo {
	DrawEngineProc *engine_proc;
	DrawEngineInfoProc *info_proc;
};

static void DrawTrainEngineInfo(EngineID engine, int x, int y, int maxw);
static void DrawRoadVehEngineInfo(EngineID engine, int x, int y, int maxw);
static void DrawShipEngineInfo(EngineID engine, int x, int y, int maxw);
static void DrawAircraftEngineInfo(EngineID engine, int x, int y, int maxw);

static const DrawEngineInfo _draw_engine_list[4] = {
	{ DrawTrainEngine,    DrawTrainEngineInfo    },
	{ DrawRoadVehEngine,  DrawRoadVehEngineInfo  },
	{ DrawShipEngine,     DrawShipEngineInfo     },
	{ DrawAircraftEngine, DrawAircraftEngineInfo },
};

static void EnginePreviewWndProc(Window *w, WindowEvent *e)
{
	switch (e->event) {
	case WE_PAINT: {
		EngineID engine = w->window_number;
		const DrawEngineInfo* dei;
		int width;

		DrawWindowWidgets(w);

		SetDParam(0, GetEngineCategoryName(engine));
		DrawStringMultiCenter(150, 44, STR_8101_WE_HAVE_JUST_DESIGNED_A, 296);

		SetDParam(0, engine);
		DrawStringCentered(w->width >> 1, 80, STR_ENGINE_NAME, TC_BLACK);

		dei = &_draw_engine_list[GetEngine(engine)->type];

		width = w->width;
		dei->engine_proc(width >> 1, 100, engine, 0);
		dei->info_proc(engine, width >> 1, 130, width - 52);
		break;
	}

	case WE_CLICK:
		switch (e->we.click.widget) {
			case 4:
				DoCommandP(0, w->window_number, 0, NULL, CMD_WANT_ENGINE_PREVIEW);
				/* Fallthrough */
			case 3:
				DeleteWindow(w);
				break;
		}
		break;
	}
}

static const WindowDesc _engine_preview_desc = {
	WDP_CENTER, WDP_CENTER, 300, 192, 300, 192,
	WC_ENGINE_PREVIEW, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET,
	_engine_preview_widgets,
	EnginePreviewWndProc
};


void ShowEnginePreviewWindow(EngineID engine)
{
	AllocateWindowDescFront(&_engine_preview_desc, engine);
}

static uint GetTotalCapacityOfArticulatedParts(EngineID engine, VehicleType type)
{
	uint total = 0;

	uint16 *cap = GetCapacityOfArticulatedParts(engine, type);
	for (uint c = 0; c < NUM_CARGO; c++) {
		total += cap[c];
	}

	return total;
}

static void DrawTrainEngineInfo(EngineID engine, int x, int y, int maxw)
{
	const RailVehicleInfo *rvi = RailVehInfo(engine);
	int multihead = (rvi->railveh_type == RAILVEH_MULTIHEAD) ? 1 : 0;

	SetDParam(0, (_price.build_railvehicle >> 3) * GetEngineProperty(engine, 0x17, rvi->base_cost) >> 5);
	SetDParam(2, GetEngineProperty(engine, 0x09, rvi->max_speed) * 10 / 16);
	SetDParam(3, GetEngineProperty(engine, 0x0B, rvi->power));
	SetDParam(1, GetEngineProperty(engine, 0x16, rvi->weight) << multihead);

	SetDParam(4, GetEngineProperty(engine, 0x0D, rvi->running_cost) * GetPriceByIndex(rvi->running_cost_class) >> 8);

	uint capacity = GetTotalCapacityOfArticulatedParts(engine, VEH_TRAIN);
	if (capacity != 0) {
		SetDParam(5, rvi->cargo_type);
		SetDParam(6, capacity);
	} else {
		SetDParam(5, CT_INVALID);
	}
	DrawStringMultiCenter(x, y, STR_VEHICLE_INFO_COST_WEIGHT_SPEED_POWER, maxw);
}

static void DrawAircraftEngineInfo(EngineID engine, int x, int y, int maxw)
{
	const AircraftVehicleInfo *avi = AircraftVehInfo(engine);
	SetDParam(0, (_price.aircraft_base >> 3) * GetEngineProperty(engine, 0x0B, avi->base_cost) >> 5);
	SetDParam(1, avi->max_speed * 10 / 16);
	SetDParam(2, avi->passenger_capacity);
	SetDParam(3, avi->mail_capacity);
	SetDParam(4, GetEngineProperty(engine, 0x0E, avi->running_cost) * _price.aircraft_running >> 8);

	DrawStringMultiCenter(x, y, STR_A02E_COST_MAX_SPEED_CAPACITY, maxw);
}

static void DrawRoadVehEngineInfo(EngineID engine, int x, int y, int maxw)
{
	const RoadVehicleInfo *rvi = RoadVehInfo(engine);

	SetDParam(0, (_price.roadveh_base >> 3) * GetEngineProperty(engine, 0x11, rvi->base_cost) >> 5);
	SetDParam(1, rvi->max_speed * 10 / 32);
	SetDParam(2, rvi->running_cost * GetPriceByIndex(rvi->running_cost_class) >> 8);
	SetDParam(3, rvi->cargo_type);
	SetDParam(4, GetTotalCapacityOfArticulatedParts(engine, VEH_ROAD));

	DrawStringMultiCenter(x, y, STR_902A_COST_SPEED_RUNNING_COST, maxw);
}

static void DrawShipEngineInfo(EngineID engine, int x, int y, int maxw)
{
	const ShipVehicleInfo *svi = ShipVehInfo(engine);
	SetDParam(0, GetEngineProperty(engine, 0x0A, svi->base_cost) * (_price.ship_base >> 3) >> 5);
	SetDParam(1, GetEngineProperty(engine, 0x0B, svi->max_speed) * 10 / 32);
	SetDParam(2, svi->cargo_type);
	SetDParam(3, GetEngineProperty(engine, 0x0D, svi->capacity));
	SetDParam(4, GetEngineProperty(engine, 0x0F, svi->running_cost) * _price.ship_running >> 8);
	DrawStringMultiCenter(x, y, STR_982E_COST_MAX_SPEED_CAPACITY, maxw);
}


StringID GetNewsStringNewVehicleAvail(const NewsItem *ni)
{
	EngineID engine = ni->string_id;
	SetDParam(0, GetEngineCategoryName(engine));
	SetDParam(1, engine);
	return STR_NEW_VEHICLE_NOW_AVAILABLE_WITH_TYPE;
}

void DrawNewsNewVehicleAvail(Window *w)
{
	DrawNewsBorder(w);

	EngineID engine = WP(w, news_d).ni->string_id;
	const DrawEngineInfo *dei = &_draw_engine_list[GetEngine(engine)->type];

	SetDParam(0, GetEngineCategoryName(engine));
	DrawStringMultiCenter(w->width >> 1, 20, STR_NEW_VEHICLE_NOW_AVAILABLE, w->width - 2);

	GfxFillRect(25, 56, w->width - 25, w->height - 2, 10);

	SetDParam(0, engine);
	DrawStringMultiCenter(w->width >> 1, 57, STR_NEW_VEHICLE_TYPE, w->width - 2);

	dei->engine_proc(w->width >> 1, 88, engine, 0);
	GfxFillRect(25, 56, w->width - 56, 112, PALETTE_NEWSPAPER, FILLRECT_RECOLOR);
	dei->info_proc(engine, w->width >> 1, 129, w->width - 52);
}
