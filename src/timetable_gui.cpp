/* $Id$ */

/** @file timetable_gui.cpp GUI for time tabling. */

#include "stdafx.h"
#include "openttd.h"
#include "variables.h"
#include "command_func.h"
#include "engine.h"
#include "gui.h"
#include "window_gui.h"
#include "textbuf_gui.h"
#include "cargotype.h"
#include "depot.h"
#include "strings_func.h"
#include "vehicle_base.h"
#include "string_func.h"
#include "gfx_func.h"
#include "player_func.h"
#include "settings_type.h"

#include "table/strings.h"

#include "safeguards.h"

static int GetOrderFromTimetableWndPt(Window *w, int y, const Vehicle *v)
{
	/*
	 * Calculation description:
	 * 15 = 14 (w->widget[ORDER_WIDGET_ORDER_LIST].top) + 1 (frame-line)
	 * 10 = order text hight
	 */
	int sel = (y - 15) / 10;

	if ((uint)sel >= w->vscroll.cap) return INVALID_ORDER;

	sel += w->vscroll.pos;

	return (sel <= v->num_orders * 2 && sel >= 0) ? sel : INVALID_ORDER;
}

static inline void SetTimetableParams(int param1, int param2, uint32 time)
{
	if (_patches.timetable_in_ticks) {
		SetDParam(param1, STR_TIMETABLE_TICKS);
		SetDParam(param2, time);
	} else {
		SetDParam(param1, STR_TIMETABLE_DAYS);
		SetDParam(param2, time / DAY_TICKS);
	}
}

static void DrawTimetableWindow(Window *w)
{
	const Vehicle *v = GetVehicle(w->window_number);
	if (WP(w, order_d).sel >= v->num_orders * 2) WP(w, order_d).sel = -1;
	int selected = WP(w, order_d).sel;

	SetVScrollCount(w, v->num_orders * 2);

	if (v->owner == _local_player) {
		if (selected == -1) {
			w->DisableWidget(6);
			w->DisableWidget(7);
		} else if (selected % 2 == 1) {
			w->EnableWidget(6);
			w->EnableWidget(7);
		} else {
			const Order *order = GetVehicleOrder(v, (selected + 1) / 2);
			bool disable = order == NULL || order->type != OT_GOTO_STATION || (_patches.new_nonstop && (order->flags & OFB_NON_STOP));

			w->SetWidgetDisabledState(6, disable);
			w->SetWidgetDisabledState(7, disable);
		}

		w->EnableWidget(8);
		w->EnableWidget(9);
	} else {
		w->DisableWidget(6);
		w->DisableWidget(7);
		w->DisableWidget(8);
		w->DisableWidget(9);
	}

	w->SetWidgetLoweredState(9, HasBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE));

	SetDParam(0, v->index);
	DrawWindowWidgets(w);

	int y = 15;
	int i = w->vscroll.pos;
	VehicleOrderID order_id = (i + 1) / 2;
	bool final_order = false;

	const Order *order = GetVehicleOrder(v, order_id);

	while (order != NULL) {
		/* Don't draw anything if it extends past the end of the window. */
		if (i - w->vscroll.pos >= w->vscroll.cap) break;

		if (i % 2 == 0) {
			SetDParam(2, STR_EMPTY);

			switch (order->type) {
				case OT_DUMMY:
					SetDParam(0, STR_INVALID_ORDER);
					break;

				case OT_GOTO_STATION:
					SetDParam(0, (order->flags & OFB_NON_STOP) ? STR_880A_GO_NON_STOP_TO : STR_8806_GO_TO);
					SetDParam(1, order->dest);

					if (order->wait_time > 0) {
						SetDParam(2, STR_TIMETABLE_STAY_FOR);
						SetTimetableParams(3, 4, order->wait_time);
					}

					break;

				case OT_GOTO_DEPOT: {
					StringID string = STR_EMPTY;

					if (v->type == VEH_AIRCRAFT) {
						string = STR_GO_TO_AIRPORT_HANGAR;
						SetDParam(1, order->dest);
					} else {
						SetDParam(1, GetDepot(order->dest)->town_index);

						switch (v->type) {
							case VEH_TRAIN: string = (order->flags & OFB_NON_STOP) ? STR_880F_GO_NON_STOP_TO_TRAIN_DEPOT : STR_GO_TO_TRAIN_DEPOT; break;
							case VEH_ROAD:  string = STR_GO_TO_ROADVEH_DEPOT; break;
							case VEH_SHIP:  string = STR_GO_TO_SHIP_DEPOT; break;
							default: break;
						}
					}

					if (order->flags & OFB_FULL_LOAD) string++; // Service at orders

					SetDParam(0, string);
					break;
				}

				case OT_GOTO_WAYPOINT:
					SetDParam(0, (order->flags & OFB_NON_STOP) ? STR_GO_NON_STOP_TO_WAYPOINT : STR_GO_TO_WAYPOINT);
					SetDParam(1, order->dest);
					break;

				default: break;
			}

			DrawString(2, y, STR_TIMETABLE_GO_TO, (i == selected) ? TC_WHITE : TC_BLACK);

			order_id++;

			if (order_id >= v->num_orders) {
				order = GetVehicleOrder(v, 0);
				final_order = true;
			} else {
				order = order->next;
			}
		} else {
			StringID string;

			if (order->travel_time == 0) {
				string = STR_TIMETABLE_TRAVEL_NOT_TIMETABLED;
			} else {
				SetTimetableParams(0, 1, order->travel_time);
				string = STR_TIMETABLE_TRAVEL_FOR;
			}

			DrawString(12, y, string, (i == selected) ? TC_WHITE : TC_BLACK);

			if (final_order) break;
		}

		i++;
		y += 10;
	}

	y = w->widget[5].top + 1;

	{
		uint total_time = 0;
		bool complete = true;

		for (const Order *order = GetVehicleOrder(v, 0); order != NULL; order = order->next) {
			total_time += order->travel_time + order->wait_time;
			if (order->travel_time == 0) complete = false;
			if (order->wait_time == 0 && order->type == OT_GOTO_STATION && !(_patches.new_nonstop && (order->flags & OFB_NON_STOP))) complete = false;
		}

		if (total_time != 0) {
			SetTimetableParams(0, 1, total_time);
			DrawString(2, y, complete ? STR_TIMETABLE_TOTAL_TIME : STR_TIMETABLE_TOTAL_TIME_INCOMPLETE, TC_BLACK);
		}
	}
	y += 10;

	if (v->lateness_counter == 0 || (!_patches.timetable_in_ticks && v->lateness_counter / DAY_TICKS == 0)) {
		DrawString(2, y, STR_TIMETABLE_STATUS_ON_TIME, TC_BLACK);
	} else {
		SetTimetableParams(0, 1, abs(v->lateness_counter));
		DrawString(2, y, v->lateness_counter < 0 ? STR_TIMETABLE_STATUS_EARLY : STR_TIMETABLE_STATUS_LATE, TC_BLACK);
	}
}

static inline uint32 PackTimetableArgs(const Vehicle *v, uint selected)
{
	uint order_number = (selected + 1) / 2;
	uint is_journey   = (selected % 2 == 1) ? 1 : 0;

	if (order_number >= v->num_orders) order_number = 0;

	return v->index | (order_number << 16) | (is_journey << 24);
}

static void TimetableWndProc(Window *w, WindowEvent *we)
{
	switch (we->event) {
		case WE_PAINT:
			DrawTimetableWindow(w);
			break;

		case WE_CLICK: {
			const Vehicle *v = GetVehicle(w->window_number);

			switch (we->we.click.widget) {
				case 3: { /* Main panel. */
					int selected = GetOrderFromTimetableWndPt(w, we->we.click.pt.y, v);

					if (selected == INVALID_ORDER || selected == WP(w, order_d).sel) {
						/* Deselect clicked order */
						WP(w, order_d).sel = -1;
					} else {
						/* Select clicked order */
						WP(w, order_d).sel = selected;
					}
					break;
				}

				case 6: { /* "Wait For" button. */
					int selected = WP(w, order_d).sel;
					VehicleOrderID real = (selected + 1) / 2;

					if (real >= v->num_orders) real = 0;

					const Order *order = GetVehicleOrder(v, real);
					StringID current = STR_EMPTY;

					if (order != NULL) {
						uint time = (selected % 2 == 1) ? order->travel_time : order->wait_time;
						if (!_patches.timetable_in_ticks) time /= DAY_TICKS;

						if (time != 0) {
							SetDParam(0, time);
							current = STR_CONFIG_PATCHES_INT32;
						}
					}

					ShowQueryString(current, STR_TIMETABLE_CHANGE_TIME, 31, 150, w, CS_NUMERAL);
					break;
				}

				case 7: { /* Clear waiting time button. */
					uint32 p1 = PackTimetableArgs(v, WP(w, order_d).sel);
					DoCommandP(0, p1, 0, NULL, CMD_CHANGE_TIMETABLE | CMD_MSG(STR_CAN_T_TIMETABLE_VEHICLE));
					break;
				}

				case 8: /* Reset the vehicle's late counter. */
					DoCommandP(0, v->index, 0, NULL, CMD_SET_VEHICLE_ON_TIME | CMD_MSG(STR_CAN_T_TIMETABLE_VEHICLE));
					break;

				case 9: /* Autofill the timetable. */
					DoCommandP(0, v->index, HasBit(v->vehicle_flags, VF_AUTOFILL_TIMETABLE) ? 0 : 1, NULL, CMD_AUTOFILL_TIMETABLE | CMD_MSG(STR_CAN_T_TIMETABLE_VEHICLE));
					break;
			}

			SetWindowDirty(w);
			break;
		}

		case WE_ON_EDIT_TEXT: {
			const Vehicle *v = GetVehicle(w->window_number);

			uint32 p1 = PackTimetableArgs(v, WP(w, order_d).sel);

			uint64 time = StrEmpty(we->we.edittext.str) ? 0 : strtoul(we->we.edittext.str, NULL, 10);
			if (!_patches.timetable_in_ticks) time *= DAY_TICKS;

			uint32 p2 = minu(time, MAX_UVALUE(uint16));

			DoCommandP(0, p1, p2, NULL, CMD_CHANGE_TIMETABLE | CMD_MSG(STR_CAN_T_TIMETABLE_VEHICLE));
			break;
		}

		case WE_RESIZE:
			/* Update the scroll + matrix */
			w->vscroll.cap = (w->widget[3].bottom - w->widget[3].top) / 10;
			break;

	}
}

static const Widget _timetable_widgets[] = {
	{   WWT_CLOSEBOX,   RESIZE_NONE,    14,     0,    10,     0,    13, STR_00C5,                   STR_018B_CLOSE_WINDOW},
	{    WWT_CAPTION,   RESIZE_RIGHT,   14,    11,   387,     0,    13, STR_TIMETABLE_TITLE,        STR_018C_WINDOW_TITLE_DRAG_THIS},
	{  WWT_STICKYBOX,   RESIZE_LR,      14,   388,   399,     0,    13, STR_NULL,                   STR_STICKY_BUTTON},

	{      WWT_PANEL,   RESIZE_RB,      14,     0,   387,    14,    95, STR_NULL,                   STR_TIMETABLE_TOOLTIP},
	{  WWT_SCROLLBAR,   RESIZE_LRB,     14,   388,   399,    14,    95, STR_NULL,                   STR_0190_SCROLL_BAR_SCROLLS_LIST},

	{      WWT_PANEL,   RESIZE_RTB,     14,     0,   399,    96,   117, STR_NULL,                   STR_NULL},

	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,     0,   109,   118,   129, STR_TIMETABLE_CHANGE_TIME,  STR_TIMETABLE_WAIT_TIME_TOOLTIP},
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   110,   219,   118,   129, STR_CLEAR_TIME,             STR_TIMETABLE_CLEAR_TIME_TOOLTIP},
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   220,   337,   118,   129, STR_RESET_LATENESS,         STR_TIMETABLE_RESET_LATENESS_TOOLTIP},
	{ WWT_PUSHTXTBTN,   RESIZE_TB,      14,   338,   387,   118,   129, STR_TIMETABLE_AUTOFILL,     STR_TIMETABLE_AUTOFILL_TOOLTIP},

	{      WWT_PANEL,   RESIZE_RTB,     14,   388,   387,   118,   129, STR_NULL,                   STR_NULL},
	{  WWT_RESIZEBOX,   RESIZE_LRTB,    14,   388,   399,   118,   129, STR_NULL,                   STR_RESIZE_BUTTON},

	{    WIDGETS_END }
};

static const WindowDesc _timetable_desc = {
	WDP_AUTO, WDP_AUTO, 400, 130, 400, 130,
	WC_VEHICLE_TIMETABLE, WC_VEHICLE_VIEW,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_UNCLICK_BUTTONS | WDF_STICKY_BUTTON | WDF_RESIZABLE,
	_timetable_widgets,
	TimetableWndProc
};

void ShowTimetableWindow(const Vehicle *v)
{
	Window *w = AllocateWindowDescFront(&_timetable_desc, v->index);

	if (w != NULL) {
		w->caption_color = v->owner;
		w->vscroll.cap = 8;
		w->resize.step_height = 10;
		WP(w, order_d).sel = -1;
	}
}
