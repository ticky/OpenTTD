/* $Id$ */

/** @file window_type.h Types related to windows */

#ifndef WINDOW_TYPE_H
#define WINDOW_TYPE_H

#include "core/enum_type.hpp"

enum WindowClass {
	WC_NONE,
	WC_MAIN_WINDOW = WC_NONE,
	WC_MAIN_TOOLBAR,
	WC_STATUS_BAR,
	WC_BUILD_TOOLBAR,
	WC_NEWS_WINDOW,
	WC_TOWN_DIRECTORY,
	WC_STATION_LIST,
	WC_TOWN_VIEW,
	WC_SMALLMAP,
	WC_TRAINS_LIST,
	WC_ROADVEH_LIST,
	WC_SHIPS_LIST,
	WC_AIRCRAFT_LIST,
	WC_VEHICLE_VIEW,
	WC_VEHICLE_DETAILS,
	WC_VEHICLE_REFIT,
	WC_VEHICLE_ORDERS,
	WC_STATION_VIEW,
	WC_VEHICLE_DEPOT,
	WC_BUILD_VEHICLE,
	WC_BUILD_BRIDGE,
	WC_ERRMSG,
	WC_BUILD_STATION,
	WC_BUS_STATION,
	WC_TRUCK_STATION,
	WC_BUILD_DEPOT,
	WC_COMPANY,
	WC_FINANCES,
	WC_PLAYER_COLOR,
	WC_QUERY_STRING,
	WC_SAVELOAD,
	WC_SELECT_GAME,
	WC_TOOLBAR_MENU,
	WC_INCOME_GRAPH,
	WC_OPERATING_PROFIT,
	WC_TOOLTIPS,
	WC_INDUSTRY_VIEW,
	WC_PLAYER_FACE,
	WC_LAND_INFO,
	WC_TOWN_AUTHORITY,
	WC_SUBSIDIES_LIST,
	WC_GRAPH_LEGEND,
	WC_DELIVERED_CARGO,
	WC_PERFORMANCE_HISTORY,
	WC_COMPANY_VALUE,
	WC_COMPANY_LEAGUE,
	WC_BUY_COMPANY,
	WC_PAYMENT_RATES,
	WC_ENGINE_PREVIEW,
	WC_MUSIC_WINDOW,
	WC_MUSIC_TRACK_SELECTION,
	WC_SCEN_LAND_GEN,
	WC_SCEN_TOWN_GEN,
	WC_SCEN_INDUSTRY,
	WC_SCEN_BUILD_ROAD,
	WC_BUILD_TREES,
	WC_SEND_NETWORK_MSG,
	WC_DROPDOWN_MENU,
	WC_BUILD_INDUSTRY,
	WC_GAME_OPTIONS,
	WC_NETWORK_WINDOW,
	WC_INDUSTRY_DIRECTORY,
	WC_MESSAGE_HISTORY,
	WC_CHEATS,
	WC_PERFORMANCE_DETAIL,
	WC_CONSOLE,
	WC_EXTRA_VIEW_PORT,
	WC_CLIENT_LIST,
	WC_NETWORK_STATUS_WINDOW,
	WC_CUSTOM_CURRENCY,
	WC_REPLACE_VEHICLE,
	WC_HIGHSCORE,
	WC_ENDSCREEN,
	WC_SIGN_LIST,
	WC_GENERATE_LANDSCAPE,
	WC_GENERATE_PROGRESS_WINDOW,
	WC_CONFIRM_POPUP_QUERY,
	WC_TRANSPARENCY_TOOLBAR,
	WC_VEHICLE_TIMETABLE,
	WC_BUILD_SIGNAL,
	WC_COMPANY_PASSWORD_WINDOW,
};

struct Window;
struct WindowEvent;
typedef int32 WindowNumber;

/**
 * You cannot 100% reliably calculate the biggest custom struct as
 * the number of pointers in it and alignment will have a huge impact.
 * 96 is the largest window-size for 64-bit machines currently.
 */
#define WINDOW_CUSTOM_SIZE 96

#endif /* WINDOW_TYPE_H */
