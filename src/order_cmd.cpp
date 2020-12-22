/* $Id$ */

/** @file order_cmd.cpp Handling of orders. */

#include "stdafx.h"
#include "openttd.h"
#include "order.h"
#include "airport.h"
#include "depot.h"
#include "waypoint.h"
#include "command_func.h"
#include "station.h"
#include "player_func.h"
#include "news.h"
#include "saveload.h"
#include "vehicle_gui.h"
#include "cargotype.h"
#include "aircraft.h"
#include "strings_func.h"
#include "core/alloc_func.hpp"
#include "functions.h"
#include "window_func.h"
#include "settings_type.h"
#include "string_func.h"
#include "timetable.h"
#include "vehicle_func.h"

#include "table/strings.h"

#include "safeguards.h"

TileIndex _backup_orders_tile;
BackuppedOrders _backup_orders_data;

DEFINE_OLD_POOL_GENERIC(Order, Order)

/**
 *
 * Unpacks a order from savegames made with TTD(Patch)
 *
 */
Order UnpackOldOrder(uint16 packed)
{
	Order order;
	order.type    = (OrderType)GB(packed, 0, 4);
	order.flags   = GB(packed, 4, 4);
	order.dest    = GB(packed, 8, 8);
	order.next    = NULL;

	order.refit_cargo   = CT_NO_REFIT;
	order.refit_subtype = 0;
	order.wait_time     = 0;
	order.travel_time   = 0;
	order.index = 0; // avoid compiler warning

	// Sanity check
	// TTD stores invalid orders as OT_NOTHING with non-zero flags/station
	if (!order.IsValid() && (order.flags != 0 || order.dest != 0)) {
		order.type = OT_DUMMY;
		order.flags = 0;
	}

	return order;
}

/**
 *
 * Unpacks a order from savegames with version 4 and lower
 *
 */
static Order UnpackVersion4Order(uint16 packed)
{
	Order order;
	order.type  = (OrderType)GB(packed, 0, 4);
	order.flags = GB(packed, 4, 4);
	order.dest  = GB(packed, 8, 8);
	order.next  = NULL;
	order.index = 0; // avoid compiler warning
	order.refit_cargo   = CT_NO_REFIT;
	order.refit_subtype = 0;
	order.wait_time     = 0;
	order.travel_time   = 0;
	return order;
}

/**
 *
 * Updates the widgets of a vehicle which contains the order-data
 *
 */
void InvalidateVehicleOrder(const Vehicle *v)
{
	InvalidateWindow(WC_VEHICLE_VIEW,      v->index);
	InvalidateWindow(WC_VEHICLE_ORDERS,    v->index);
	InvalidateWindow(WC_VEHICLE_TIMETABLE, v->index);
}

/**
 *
 * Swap two orders
 *
 */
static void SwapOrders(Order *order1, Order *order2)
{
	Order temp_order;

	temp_order = *order1;
	AssignOrder(order1, *order2);
	order1->next = order2->next;
	AssignOrder(order2, temp_order);
	order2->next = temp_order.next;
}

/**
 *
 * Assign data to an order (from an other order)
 *   This function makes sure that the index is maintained correctly
 *
 */
void AssignOrder(Order *order, Order data)
{
	order->type  = data.type;
	order->flags = data.flags;
	order->dest  = data.dest;

	order->refit_cargo   = data.refit_cargo;
	order->refit_subtype = data.refit_subtype;

	order->wait_time   = data.wait_time;
	order->travel_time = data.travel_time;
}


/**
 * Delete all news items regarding defective orders about a vehicle
 * This could kill still valid warnings (for example about void order when just
 * another order gets added), but assume the player will notice the problems,
 * when (s)he's changing the orders.
 */
static void DeleteOrderWarnings(const Vehicle* v)
{
	DeleteVehicleNews(v->index, STR_TRAIN_HAS_TOO_FEW_ORDERS  + v->type * 4);
	DeleteVehicleNews(v->index, STR_TRAIN_HAS_VOID_ORDER      + v->type * 4);
	DeleteVehicleNews(v->index, STR_TRAIN_HAS_DUPLICATE_ENTRY + v->type * 4);
	DeleteVehicleNews(v->index, STR_TRAIN_HAS_INVALID_ENTRY   + v->type * 4);
}


static TileIndex GetOrderLocation(const Order& o)
{
	switch (o.type) {
		default: NOT_REACHED();
		case OT_GOTO_STATION: return GetStation(o.dest)->xy;
		case OT_GOTO_DEPOT:   return GetDepot(o.dest)->xy;
	}
}


/** Add an order to the orderlist of a vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 - 15) - ID of the vehicle
 * - p1 = (bit 16 - 31) - the selected order (if any). If the last order is given,
 *                        the order will be inserted before that one
 *                        only the first 8 bits used currently (bit 16 - 23) (max 255)
 * @param p2 packed order to insert
 */
CommandCost CmdInsertOrder(TileIndex tile, uint32 flags, uint32 p1, uint32 p2)
{
	Vehicle *v;
	VehicleID veh   = GB(p1,  0, 16);
	VehicleOrderID sel_ord = GB(p1, 16, 16);
	Order new_order = UnpackOrder(p2);

	if (!IsValidVehicleID(veh)) return CMD_ERROR;

	v = GetVehicle(veh);

	if (!CheckOwnership(v->owner)) return CMD_ERROR;

	/* Check if the inserted order is to the correct destination (owner, type),
	 * and has the correct flags if any */
	switch (new_order.type) {
		case OT_GOTO_STATION: {
			const Station *st;

			if (!IsValidStationID(new_order.dest)) return CMD_ERROR;
			st = GetStation(new_order.dest);

			if (st->owner != OWNER_NONE && !CheckOwnership(st->owner)) {
				return CMD_ERROR;
			}

			switch (v->type) {
				case VEH_TRAIN:
					if (!(st->facilities & FACIL_TRAIN)) return CMD_ERROR;
					break;

				case VEH_ROAD:
					if (IsCargoInClass(v->cargo_type, CC_PASSENGERS)) {
						if (!(st->facilities & FACIL_BUS_STOP)) return CMD_ERROR;
					} else {
						if (!(st->facilities & FACIL_TRUCK_STOP)) return CMD_ERROR;
					}
					break;

				case VEH_SHIP:
					if (!(st->facilities & FACIL_DOCK)) return CMD_ERROR;
					break;

				case VEH_AIRCRAFT:
					if (!(st->facilities & FACIL_AIRPORT) || !CanAircraftUseStation(v->engine_type, st)) {
						return CMD_ERROR;
					}
					break;

				default: return CMD_ERROR;
			}

			/* Order flags can be any of the following for stations:
			 * [full-load | unload] [+ transfer] [+ non-stop]
			 * non-stop orders (if any) are only valid for trains */
			switch (new_order.flags) {
				case 0:
				case OFB_FULL_LOAD:
				case OFB_FULL_LOAD | OFB_TRANSFER:
				case OFB_UNLOAD:
				case OFB_UNLOAD | OFB_TRANSFER:
				case OFB_TRANSFER:
					break;

				case OFB_NON_STOP:
				case OFB_NON_STOP | OFB_FULL_LOAD:
				case OFB_NON_STOP | OFB_FULL_LOAD | OFB_TRANSFER:
				case OFB_NON_STOP | OFB_UNLOAD:
				case OFB_NON_STOP | OFB_UNLOAD | OFB_TRANSFER:
				case OFB_NON_STOP | OFB_TRANSFER:
					if (v->type != VEH_TRAIN) return CMD_ERROR;
					break;

				default: return CMD_ERROR;
			}
			break;
		}

		case OT_GOTO_DEPOT: {
			if (v->type == VEH_AIRCRAFT) {
				const Station* st;

				if (!IsValidStationID(new_order.dest)) return CMD_ERROR;
				st = GetStation(new_order.dest);

				if (!CheckOwnership(st->owner) ||
						!(st->facilities & FACIL_AIRPORT) ||
						st->Airport()->nof_depots == 0 ||
						!CanAircraftUseStation(v->engine_type, st)) {
					return CMD_ERROR;
				}
			} else {
				const Depot* dp;

				if (!IsValidDepotID(new_order.dest)) return CMD_ERROR;
				dp = GetDepot(new_order.dest);

				if (!CheckOwnership(GetTileOwner(dp->xy))) return CMD_ERROR;

				switch (v->type) {
					case VEH_TRAIN:
						if (!IsTileDepotType(dp->xy, TRANSPORT_RAIL)) return CMD_ERROR;
						break;

					case VEH_ROAD:
						if (!IsTileDepotType(dp->xy, TRANSPORT_ROAD)) return CMD_ERROR;
						break;

					case VEH_SHIP:
						if (!IsTileDepotType(dp->xy, TRANSPORT_WATER)) return CMD_ERROR;
						break;

					default: return CMD_ERROR;
				}
			}

			/* Order flags can be any of the following for depots:
			 * order [+ halt] [+ non-stop]
			 * non-stop orders (if any) are only valid for trains */
			switch (new_order.flags) {
				case OFB_PART_OF_ORDERS:
				case OFB_PART_OF_ORDERS | OFB_HALT_IN_DEPOT:
					break;

				case OFB_NON_STOP | OFB_PART_OF_ORDERS:
				case OFB_NON_STOP | OFB_PART_OF_ORDERS | OFB_HALT_IN_DEPOT:
					if (v->type != VEH_TRAIN) return CMD_ERROR;
					break;

				default: return CMD_ERROR;
			}
			break;
		}

		case OT_GOTO_WAYPOINT: {
			const Waypoint* wp;

			if (v->type != VEH_TRAIN) return CMD_ERROR;

			if (!IsValidWaypointID(new_order.dest)) return CMD_ERROR;
			wp = GetWaypoint(new_order.dest);

			if (!CheckOwnership(GetTileOwner(wp->xy))) return CMD_ERROR;

			/* Order flags can be any of the following for waypoints:
			 * [non-stop]
			 * non-stop orders (if any) are only valid for trains */
			switch (new_order.flags) {
				case 0: break;

				case OFB_NON_STOP:
					if (v->type != VEH_TRAIN) return CMD_ERROR;
					break;

				default: return CMD_ERROR;
			}
			break;
		}

		default: return CMD_ERROR;
	}

	if (sel_ord > v->num_orders) return CMD_ERROR;

	if (!HasOrderPoolFree(1)) return_cmd_error(STR_8831_NO_MORE_SPACE_FOR_ORDERS);

	if (v->type == VEH_SHIP && IsHumanPlayer(v->owner) && _patches.pathfinder_for_ships != VPF_NPF) {
		/* Make sure the new destination is not too far away from the previous */
		const Order *prev = NULL;
		uint n = 0;

		/* Find the last goto station or depot order before the insert location.
		 * If the order is to be inserted at the beginning of the order list this
		 * finds the last order in the list. */
		for (const Order *o = v->orders; o != NULL; o = o->next) {
			if (o->type == OT_GOTO_STATION || o->type == OT_GOTO_DEPOT) prev = o;
			if (++n == sel_ord && prev != NULL) break;
		}
		if (prev != NULL) {
			uint dist = DistanceManhattan(
				GetOrderLocation(*prev),
				GetOrderLocation(new_order)
			);
			if (dist >= 130) {
				return_cmd_error(STR_0210_TOO_FAR_FROM_PREVIOUS_DESTINATIO);
			}
		}
	}

	if (flags & DC_EXEC) {
		Vehicle *u;
		Order *new_o = new Order();
		AssignOrder(new_o, new_order);

		/* Create new order and link in list */
		if (v->orders == NULL) {
			v->orders = new_o;
		} else {
			/* Try to get the previous item (we are inserting above the
			    selected) */
			Order *order = GetVehicleOrder(v, sel_ord - 1);

			if (order == NULL && GetVehicleOrder(v, sel_ord) != NULL) {
				/* There is no previous item, so we are altering v->orders itself
				    But because the orders can be shared, we copy the info over
				    the v->orders, so we don't have to change the pointers of
				    all vehicles */
				SwapOrders(v->orders, new_o);
				/* Now update the next pointers */
				v->orders->next = new_o;
			} else if (order == NULL) {
				/* 'sel' is a non-existing order, add him to the end */
				order = GetLastVehicleOrder(v);
				order->next = new_o;
			} else {
				/* Put the new order in between */
				new_o->next = order->next;
				order->next = new_o;
			}
		}

		u = GetFirstVehicleFromSharedList(v);
		DeleteOrderWarnings(u);
		for (; u != NULL; u = u->next_shared) {
			/* Increase amount of orders */
			u->num_orders++;

			/* If the orderlist was empty, assign it */
			if (u->orders == NULL) u->orders = v->orders;

			assert(v->orders == u->orders);

			/* If there is added an order before the current one, we need
			to update the selected order */
			if (sel_ord <= u->cur_order_index) {
				uint cur = u->cur_order_index + 1;
				/* Check if we don't go out of bound */
				if (cur < u->num_orders)
					u->cur_order_index = cur;
			}
			/* Update any possible open window of the vehicle */
			InvalidateVehicleOrder(u);
		}

		/* Make sure to rebuild the whole list */
		RebuildVehicleLists();
	}

	return CommandCost();
}

/** Declone an order-list
 * @param *dst delete the orders of this vehicle
 * @param flags execution flags
 */
static CommandCost DecloneOrder(Vehicle *dst, uint32 flags)
{
	if (flags & DC_EXEC) {
		DeleteVehicleOrders(dst);
		InvalidateVehicleOrder(dst);
		RebuildVehicleLists();
	}
	return CommandCost();
}

/**
 * Remove the VehicleList that shows all the vehicles with the same shared
 *  orders.
 */
static void RemoveSharedOrderVehicleList(Vehicle *v)
{
	assert(v->orders != NULL);
	WindowClass window_class = WC_NONE;

	switch (v->type) {
		default: NOT_REACHED();
		case VEH_TRAIN:    window_class = WC_TRAINS_LIST;   break;
		case VEH_ROAD:     window_class = WC_ROADVEH_LIST;  break;
		case VEH_SHIP:     window_class = WC_SHIPS_LIST;    break;
		case VEH_AIRCRAFT: window_class = WC_AIRCRAFT_LIST; break;
	}
	DeleteWindowById(window_class, (v->orders->index << 16) | (v->type << 11) | VLW_SHARED_ORDERS | v->owner);
}

/** Delete an order from the orderlist of a vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the ID of the vehicle
 * @param p2 the order to delete (max 255)
 */
CommandCost CmdDeleteOrder(TileIndex tile, uint32 flags, uint32 p1, uint32 p2)
{
	Vehicle *v, *u;
	VehicleID veh_id = p1;
	VehicleOrderID sel_ord = p2;
	Order *order;

	if (!IsValidVehicleID(veh_id)) return CMD_ERROR;

	v = GetVehicle(veh_id);

	if (!CheckOwnership(v->owner)) return CMD_ERROR;

	/* If we did not select an order, we maybe want to de-clone the orders */
	if (sel_ord >= v->num_orders)
		return DecloneOrder(v, flags);

	order = GetVehicleOrder(v, sel_ord);
	if (order == NULL) return CMD_ERROR;

	if (flags & DC_EXEC) {
		if (GetVehicleOrder(v, sel_ord - 1) == NULL) {
			if (GetVehicleOrder(v, sel_ord + 1) != NULL) {
				/* First item, but not the last, so we need to alter v->orders
				    Because we can have shared order, we copy the data
				    from the next item over the deleted */
				order = GetVehicleOrder(v, sel_ord + 1);
				SwapOrders(v->orders, order);
			} else {
				/* XXX -- The system currently can't handle a shared-order vehicle list
				 *  open when there aren't any orders in the list, so close the window
				 *  in this case. Of course it needs a better fix later */
				RemoveSharedOrderVehicleList(v);
				/* Last item, so clean the list */
				v->orders = NULL;
			}
		} else {
			GetVehicleOrder(v, sel_ord - 1)->next = order->next;
		}

		/* Give the item free */
		delete order;

		u = GetFirstVehicleFromSharedList(v);
		DeleteOrderWarnings(u);
		for (; u != NULL; u = u->next_shared) {
			u->num_orders--;

			if (sel_ord < u->cur_order_index)
				u->cur_order_index--;

			/* If we removed the last order, make sure the shared vehicles
			 * also set their orders to NULL */
			if (v->orders == NULL) u->orders = NULL;

			assert(v->orders == u->orders);

			/* NON-stop flag is misused to see if a train is in a station that is
			 * on his order list or not */
			if (sel_ord == u->cur_order_index && u->current_order.type == OT_LOADING &&
					HasBit(u->current_order.flags, OF_NON_STOP)) {
				u->current_order.flags = 0;
			}

			/* Update any possible open window of the vehicle */
			InvalidateVehicleOrder(u);
		}

		RebuildVehicleLists();
	}

	return CommandCost();
}

/** Goto order of order-list.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 The ID of the vehicle which order is skipped
 * @param p2 the selected order to which we want to skip
 */
CommandCost CmdSkipToOrder(TileIndex tile, uint32 flags, uint32 p1, uint32 p2)
{
	Vehicle *v;
	VehicleID veh_id = p1;
	VehicleOrderID sel_ord = p2;

	if (!IsValidVehicleID(veh_id)) return CMD_ERROR;

	v = GetVehicle(veh_id);

	if (!CheckOwnership(v->owner) || sel_ord == v->cur_order_index ||
			sel_ord >= v->num_orders || v->num_orders < 2) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->cur_order_index = sel_ord;

		if (v->type == VEH_ROAD) ClearSlot(v);

		if (v->current_order.type == OT_LOADING) {
			v->LeaveStation();
			/* NON-stop flag is misused to see if a train is in a station that is
			 * on his order list or not */
			if (HasBit(v->current_order.flags, OF_NON_STOP)) v->current_order.flags = 0;
		}

		InvalidateVehicleOrder(v);
	}

	/* We have an aircraft/ship, they have a mini-schedule, so update them all */
	if (v->type == VEH_AIRCRAFT) InvalidateWindowClasses(WC_AIRCRAFT_LIST);
	if (v->type == VEH_SHIP) InvalidateWindowClasses(WC_SHIPS_LIST);

	return CommandCost();
}

/**
 * Move an order inside the orderlist
 * @param tile unused
 * @param p1 the ID of the vehicle
 * @param p2 order to move and target
 *           bit 0-15  : the order to move
 *           bit 16-31 : the target order
 * @note The target order will move one place down in the orderlist
 *  if you move the order upwards else it'll move it one place down
 */
CommandCost CmdMoveOrder(TileIndex tile, uint32 flags, uint32 p1, uint32 p2)
{
	VehicleID veh = p1;
	VehicleOrderID moving_order = GB(p2,  0, 16);
	VehicleOrderID target_order = GB(p2, 16, 16);

	if (!IsValidVehicleID(veh)) return CMD_ERROR;

	Vehicle *v = GetVehicle(veh);
	if (!CheckOwnership(v->owner)) return CMD_ERROR;

	/* Don't make senseless movements */
	if (moving_order >= v->num_orders || target_order >= v->num_orders ||
			moving_order == target_order || v->num_orders <= 1)
		return CMD_ERROR;

	Order *moving_one = GetVehicleOrder(v, moving_order);
	/* Don't move an empty order */
	if (moving_one == NULL) return CMD_ERROR;

	if (flags & DC_EXEC) {
		/* Take the moving order out of the pointer-chain */
		Order *one_before = GetVehicleOrder(v, moving_order - 1);
		Order *one_past = GetVehicleOrder(v, moving_order + 1);

		if (one_before == NULL) {
			/* First order edit */
			v->orders = moving_one->next;
		} else {
			one_before->next = moving_one->next;
		}

		/* Insert the moving_order again in the pointer-chain */
		one_before = GetVehicleOrder(v, target_order - 1);
		one_past = GetVehicleOrder(v, target_order);

		moving_one->next = one_past;

		if (one_before == NULL) {
			/* first order edit */
			SwapOrders(v->orders, moving_one);
			v->orders->next = moving_one;
		} else {
			one_before->next = moving_one;
		}

		/* Update shared list */
		Vehicle *u = GetFirstVehicleFromSharedList(v);

		DeleteOrderWarnings(u);

		for (; u != NULL; u = u->next_shared) {
			/* Update the first order */
			if (u->orders != v->orders) u->orders = v->orders;

			/* Update the current order */
			if (u->cur_order_index == moving_order) {
				u->cur_order_index = target_order;
			} else if(u->cur_order_index > moving_order && u->cur_order_index <= target_order) {
				u->cur_order_index--;
			} else if(u->cur_order_index < moving_order && u->cur_order_index >= target_order) {
				u->cur_order_index++;
			}

			assert(v->orders == u->orders);
			/* Update any possible open window of the vehicle */
			InvalidateVehicleOrder(u);
		}

		/* Make sure to rebuild the whole list */
		RebuildVehicleLists();
	}

	return CommandCost();
}

/** Modify an order in the orderlist of a vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 - 15) - ID of the vehicle
 * - p1 = (bit 16 - 31) - the selected order (if any). If the last order is given,
 *                        the order will be inserted before that one
 *                        only the first 8 bits used currently (bit 16 - 23) (max 255)
 * @param p2 mode to change the order to (always set)
 */
CommandCost CmdModifyOrder(TileIndex tile, uint32 flags, uint32 p1, uint32 p2)
{
	Vehicle *v;
	Order *order;
	VehicleOrderID sel_ord = GB(p1, 16, 16); // XXX - automatically truncated to 8 bits.
	VehicleID veh   = GB(p1,  0, 16);

	if (!IsValidVehicleID(veh)) return CMD_ERROR;
	if (p2 != OF_FULL_LOAD && p2 != OF_UNLOAD && p2 != OF_NON_STOP && p2 != OF_TRANSFER) return CMD_ERROR;

	v = GetVehicle(veh);

	if (!CheckOwnership(v->owner)) return CMD_ERROR;

	/* Is it a valid order? */
	if (sel_ord >= v->num_orders) return CMD_ERROR;

	order = GetVehicleOrder(v, sel_ord);
	if ((order->type != OT_GOTO_STATION  || GetStation(order->dest)->IsBuoy()) &&
			(order->type != OT_GOTO_DEPOT    || p2 == OF_UNLOAD) &&
			(order->type != OT_GOTO_WAYPOINT || p2 != OF_NON_STOP)) {
		return CMD_ERROR;
	}

	if (flags & DC_EXEC) {
		switch (p2) {
		case OF_FULL_LOAD:
			ToggleBit(order->flags, OF_FULL_LOAD);
			if (order->type != OT_GOTO_DEPOT) ClrBit(order->flags, OF_UNLOAD);
			break;
		case OF_UNLOAD:
			ToggleBit(order->flags, OF_UNLOAD);
			ClrBit(order->flags, OF_FULL_LOAD);
			break;
		case OF_NON_STOP:
			ToggleBit(order->flags, OF_NON_STOP);
			break;
		case OF_TRANSFER:
			ToggleBit(order->flags, OF_TRANSFER);
			break;
		default: NOT_REACHED();
		}

		/* Update the windows and full load flags, also for vehicles that share the same order list */
		{
			Vehicle* u;

			u = GetFirstVehicleFromSharedList(v);
			DeleteOrderWarnings(u);
			for (; u != NULL; u = u->next_shared) {
				/* Toggle u->current_order "Full load" flag if it changed.
				 * However, as the same flag is used for depot orders, check
				 * whether we are not going to a depot as there are three
				 * cases where the full load flag can be active and only
				 * one case where the flag is used for depot orders. In the
				 * other cases for the OrderTypeByte the flags are not used,
				 * so do not care and those orders should not be active
				 * when this function is called.
				 */
				if (sel_ord == u->cur_order_index &&
						u->current_order.type != OT_GOTO_DEPOT &&
						HasBit(u->current_order.flags, OF_FULL_LOAD) != HasBit(order->flags, OF_FULL_LOAD)) {
					ToggleBit(u->current_order.flags, OF_FULL_LOAD);
				}
				InvalidateVehicleOrder(u);
			}
		}
	}

	return CommandCost();
}

/** Clone/share/copy an order-list of an other vehicle.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0-15) - destination vehicle to clone orders to (p1 & 0xFFFF)
 * - p1 = (bit 16-31) - source vehicle to clone orders from, if any (none for CO_UNSHARE)
 * @param p2 mode of cloning: CO_SHARE, CO_COPY, or CO_UNSHARE
 */
CommandCost CmdCloneOrder(TileIndex tile, uint32 flags, uint32 p1, uint32 p2)
{
	Vehicle *dst;
	VehicleID veh_src = GB(p1, 16, 16);
	VehicleID veh_dst = GB(p1,  0, 16);

	if (!IsValidVehicleID(veh_dst)) return CMD_ERROR;

	dst = GetVehicle(veh_dst);

	if (!CheckOwnership(dst->owner)) return CMD_ERROR;

	switch (p2) {
		case CO_SHARE: {
			Vehicle *src;

			if (!IsValidVehicleID(veh_src)) return CMD_ERROR;

			src = GetVehicle(veh_src);

			/* Sanity checks */
			if (!CheckOwnership(src->owner) || dst->type != src->type || dst == src)
				return CMD_ERROR;

			/* Trucks can't share orders with busses (and visa versa) */
			if (src->type == VEH_ROAD) {
				if (src->cargo_type != dst->cargo_type && (IsCargoInClass(src->cargo_type, CC_PASSENGERS) || IsCargoInClass(dst->cargo_type, CC_PASSENGERS)))
					return CMD_ERROR;
			}

			/* Is the vehicle already in the shared list? */
			{
				const Vehicle* u;

				for (u = GetFirstVehicleFromSharedList(src); u != NULL; u = u->next_shared) {
					if (u == dst) return CMD_ERROR;
				}
			}

			if (flags & DC_EXEC) {
				/* If the destination vehicle had a OrderList, destroy it */
				DeleteVehicleOrders(dst);

				dst->orders = src->orders;
				dst->num_orders = src->num_orders;

				/* Link this vehicle in the shared-list */
				dst->next_shared = src->next_shared;
				dst->prev_shared = src;
				if (src->next_shared != NULL) src->next_shared->prev_shared = dst;
				src->next_shared = dst;

				InvalidateVehicleOrder(dst);
				InvalidateVehicleOrder(src);

				RebuildVehicleLists();
			}
			break;
		}

		case CO_COPY: {
			Vehicle *src;
			int delta;

			if (!IsValidVehicleID(veh_src)) return CMD_ERROR;

			src = GetVehicle(veh_src);

			/* Sanity checks */
			if (!CheckOwnership(src->owner) || dst->type != src->type || dst == src)
				return CMD_ERROR;

			/* Trucks can't copy all the orders from busses (and visa versa) */
			if (src->type == VEH_ROAD) {
				const Order *order;
				TileIndex required_dst = INVALID_TILE;

				FOR_VEHICLE_ORDERS(src, order) {
					if (order->type == OT_GOTO_STATION) {
						const Station *st = GetStation(order->dest);
						if (IsCargoInClass(dst->cargo_type, CC_PASSENGERS)) {
							if (st->bus_stops != NULL) required_dst = st->bus_stops->xy;
						} else {
							if (st->truck_stops != NULL) required_dst = st->truck_stops->xy;
						}
						/* This station has not the correct road-bay, so we can't copy! */
						if (required_dst == INVALID_TILE)
							return CMD_ERROR;
					}
				}
			}

			/* make sure there are orders available */
			delta = dst->IsOrderListShared() ? src->num_orders + 1 : src->num_orders - dst->num_orders;
			if (!HasOrderPoolFree(delta))
				return_cmd_error(STR_8831_NO_MORE_SPACE_FOR_ORDERS);

			if (flags & DC_EXEC) {
				const Order *order;
				Order **order_dst;

				/* If the destination vehicle had a OrderList, destroy it */
				DeleteVehicleOrders(dst);

				order_dst = &dst->orders;
				FOR_VEHICLE_ORDERS(src, order) {
					*order_dst = new Order();
					AssignOrder(*order_dst, *order);
					order_dst = &(*order_dst)->next;
				}

				dst->num_orders = src->num_orders;

				InvalidateVehicleOrder(dst);

				RebuildVehicleLists();
			}
			break;
		}

		case CO_UNSHARE: return DecloneOrder(dst, flags);
		default: return CMD_ERROR;
	}

	return CommandCost();
}

/** Add/remove refit orders from an order
 * @param tile Not used
 * @param flags operation to perform
 * @param p1 VehicleIndex of the vehicle having the order
 * @param p2 bitmask
 *   - bit 0-7 CargoID
 *   - bit 8-15 Cargo subtype
 *   - bit 16-23 number of order to modify
 */
CommandCost CmdOrderRefit(TileIndex tile, uint32 flags, uint32 p1, uint32 p2)
{
	const Vehicle *v;
	Order *order;
	VehicleID veh = GB(p1, 0, 16);
	VehicleOrderID order_number  = GB(p2, 16, 8);
	CargoID cargo = GB(p2, 0, 8);
	byte subtype  = GB(p2, 8, 8);

	if (!IsValidVehicleID(veh)) return CMD_ERROR;

	v = GetVehicle(veh);

	if (!CheckOwnership(v->owner)) return CMD_ERROR;

	order = GetVehicleOrder(v, order_number);
	if (order == NULL) return CMD_ERROR;

	if (flags & DC_EXEC) {
		Vehicle *u;

		order->refit_cargo = cargo;
		order->refit_subtype = subtype;

		u = GetFirstVehicleFromSharedList(v);
		for (; u != NULL; u = u->next_shared) {
			/* Update any possible open window of the vehicle */
			InvalidateVehicleOrder(u);

			/* If the vehicle already got the current depot set as current order, then update current order as well */
			if (u->cur_order_index == order_number && HasBit(u->current_order.flags, OF_PART_OF_ORDERS)) {
				u->current_order.refit_cargo = cargo;
				u->current_order.refit_subtype = subtype;
			}
		}
	}

	return CommandCost();
}

/**
 *
 * Backup a vehicle order-list, so you can replace a vehicle
 *  without loosing the order-list
 *
 */
void BackupVehicleOrders(const Vehicle *v, BackuppedOrders *bak)
{
	/* Make sure we always have freed the stuff */
	free(bak->order);
	bak->order = NULL;
	free(bak->name);
	bak->name = NULL;

	/* Save general info */
	bak->orderindex       = v->cur_order_index;
	bak->group            = v->group_id;
	bak->service_interval = v->service_interval;
	if (v->name != NULL) bak->name = strdup(v->name);

	/* If we have shared orders, store it on a special way */
	if (v->IsOrderListShared()) {
		const Vehicle *u = (v->next_shared) ? v->next_shared : v->prev_shared;

		bak->clone = u->index;
	} else {
		/* Else copy the orders */

		/* We do not have shared orders */
		bak->clone = INVALID_VEHICLE;


		/* Count the number of orders */
		uint cnt = 0;
		const Order *order;
		FOR_VEHICLE_ORDERS(v, order) cnt++;

		/* Allocate memory for the orders plus an end-of-orders marker */
		bak->order = MallocT<Order>(cnt + 1);

		Order *dest = bak->order;

		/* Copy the orders */
		FOR_VEHICLE_ORDERS(v, order) {
			*dest = *order;
			dest++;
		}
		/* End the list with an empty order */
		dest->Free();
	}
}

/**
 *
 * Restore vehicle orders that are backupped via BackupVehicleOrders
 *
 */
void RestoreVehicleOrders(const Vehicle *v, const BackuppedOrders *bak)
{
	/* If we have a custom name, process that */
	if (bak->name != NULL) {
		_cmd_text = bak->name;
		DoCommandP(0, v->index, 0, NULL, CMD_NAME_VEHICLE);
	}

	/* If we had shared orders, recover that */
	if (bak->clone != INVALID_VEHICLE) {
		DoCommandP(0, v->index | (bak->clone << 16), CO_SHARE, NULL, CMD_CLONE_ORDER);
	} else {

		/* CMD_NO_TEST_IF_IN_NETWORK is used here, because CMD_INSERT_ORDER checks if the
		 *  order number is one more than the current amount of orders, and because
		 *  in network the commands are queued before send, the second insert always
		 *  fails in test mode. By bypassing the test-mode, that no longer is a problem. */
		for (uint i = 0; bak->order[i].IsValid(); i++) {
			if (!DoCommandP(0, v->index + (i << 16), PackOrder(&bak->order[i]), NULL,
					CMD_INSERT_ORDER | CMD_NO_TEST_IF_IN_NETWORK)) {
				break;
			}

			/* Copy timetable if enabled */
			if (_patches.timetabling && !DoCommandP(0, v->index | (i << 16) | (1 << 25),
					bak->order[i].wait_time << 16 | bak->order[i].travel_time, NULL,
					CMD_CHANGE_TIMETABLE | CMD_NO_TEST_IF_IN_NETWORK)) {
				break;
			}
		}
	}

	/* Restore vehicle order-index and service interval */
	DoCommandP(0, v->index, bak->orderindex | (bak->service_interval << 16) , NULL, CMD_RESTORE_ORDER_INDEX);

	/* Restore vehicle group */
	DoCommandP(0, bak->group, v->index, NULL, CMD_ADD_VEHICLE_GROUP);
}

/** Restore the current order-index of a vehicle and sets service-interval.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 the ID of the vehicle
 * @param p2 various bistuffed elements
 * - p2 = (bit  0-15) - current order-index (p2 & 0xFFFF)
 * - p2 = (bit 16-31) - service interval (p2 >> 16)
 * @todo Unfortunately you cannot safely restore the unitnumber or the old vehicle
 * as far as I can see. We can store it in BackuppedOrders, and restore it, but
 * but we have no way of seeing it has been tampered with or not, as we have no
 * legit way of knowing what that ID was.@n
 * If we do want to backup/restore it, just add UnitID uid to BackuppedOrders, and
 * restore it as parameter 'y' (ugly hack I know) for example. "v->unitnumber = y;"
 */
CommandCost CmdRestoreOrderIndex(TileIndex tile, uint32 flags, uint32 p1, uint32 p2)
{
	Vehicle *v;
	VehicleOrderID cur_ord = GB(p2,  0, 16);
	uint16 serv_int = GB(p2, 16, 16);

	if (!IsValidVehicleID(p1)) return CMD_ERROR;

	v = GetVehicle(p1);

	/* Check the vehicle type and ownership, and if the service interval and order are in range */
	if (!CheckOwnership(v->owner)) return CMD_ERROR;
	if (serv_int != GetServiceIntervalClamped(serv_int) || cur_ord >= v->num_orders) return CMD_ERROR;

	if (flags & DC_EXEC) {
		v->cur_order_index = cur_ord;
		v->service_interval = serv_int;
	}

	return CommandCost();
}


static TileIndex GetStationTileForVehicle(const Vehicle* v, const Station* st)
{
	switch (v->type) {
		default: NOT_REACHED();
		case VEH_TRAIN:     return st->train_tile;
		case VEH_AIRCRAFT:  return CanAircraftUseStation(v->engine_type, st) ? st->airport_tile : 0;
		case VEH_SHIP:      return st->dock_tile;
		case VEH_ROAD:
			if (IsCargoInClass(v->cargo_type, CC_PASSENGERS)) {
				return (st->bus_stops != NULL) ? st->bus_stops->xy : 0;
			} else {
				return (st->truck_stops != NULL) ? st->truck_stops->xy : 0;
			}
	}
}


/**
 *
 * Check the orders of a vehicle, to see if there are invalid orders and stuff
 *
 */
void CheckOrders(const Vehicle* v)
{
	/* Does the user wants us to check things? */
	if (_patches.order_review_system == 0) return;

	/* Do nothing for crashed vehicles */
	if (v->vehstatus & VS_CRASHED) return;

	/* Do nothing for stopped vehicles if setting is '1' */
	if (_patches.order_review_system == 1 && v->vehstatus & VS_STOPPED)
		return;

	/* do nothing we we're not the first vehicle in a share-chain */
	if (v->next_shared != NULL) return;

	/* Only check every 20 days, so that we don't flood the message log */
	if (v->owner == _local_player && v->day_counter % 20 == 0) {
		int n_st, problem_type = -1;
		const Order *order;
		int message = 0;

		/* Check the order list */
		n_st = 0;

		FOR_VEHICLE_ORDERS(v, order) {
			/* Dummy order? */
			if (order->type == OT_DUMMY) {
				problem_type = 1;
				break;
			}
			/* Does station have a load-bay for this vehicle? */
			if (order->type == OT_GOTO_STATION) {
				const Station* st = GetStation(order->dest);
				TileIndex required_tile = GetStationTileForVehicle(v, st);

				n_st++;
				if (required_tile == 0) problem_type = 3;
			}
		}

		/* Check if the last and the first order are the same */
		if (v->num_orders > 1) {
			const Order* last = GetLastVehicleOrder(v);

			if (v->orders->type  == last->type &&
					v->orders->flags == last->flags &&
					v->orders->dest  == last->dest) {
				problem_type = 2;
			}
		}

		/* Do we only have 1 station in our order list? */
		if (n_st < 2 && problem_type == -1) problem_type = 0;

		/* We don't have a problem */
		if (problem_type < 0) return;

		message = STR_TRAIN_HAS_TOO_FEW_ORDERS + (v->type << 2) + problem_type;
		//DEBUG(misc, 3, "Triggered News Item for vehicle %d", v->index);

		SetDParam(0, v->unitnumber);
		AddNewsItem(
			message,
			NEWS_FLAGS(NM_SMALL, NF_VIEWPORT | NF_VEHICLE, NT_ADVICE, 0),
			v->index,
			0
		);
	}
}

/**
 * Removes an order from all vehicles. Triggers when, say, a station is removed.
 * @param type The type of the order (OT_GOTO_[STATION|DEPOT|WAYPOINT]).
 * @param destination The destination. Can be a StationID, DepotID or WaypointID.
 */
void RemoveOrderFromAllVehicles(OrderType type, DestinationID destination)
{
	Vehicle *v;

	/* Aircraft have StationIDs for depot orders and never use DepotIDs
	 * This fact is handled specially below
	 */

	/* Go through all vehicles */
	FOR_ALL_VEHICLES(v) {
		Order *order;
		bool invalidate;

		/* Forget about this station if this station is removed */
		if (v->last_station_visited == destination && type == OT_GOTO_STATION) {
			v->last_station_visited = INVALID_STATION;
		}

		order = &v->current_order;
		if ((v->type == VEH_AIRCRAFT && order->type == OT_GOTO_DEPOT ? OT_GOTO_STATION : order->type) == type &&
				v->current_order.dest == destination) {
			order->type = OT_DUMMY;
			order->flags = 0;
			InvalidateWindow(WC_VEHICLE_VIEW, v->index);
		}

		/* Clear the order from the order-list */
		invalidate = false;
		FOR_VEHICLE_ORDERS(v, order) {
			if ((v->type == VEH_AIRCRAFT && order->type == OT_GOTO_DEPOT ? OT_GOTO_STATION : order->type) == type &&
					order->dest == destination) {
				order->type = OT_DUMMY;
				order->flags = 0;
				invalidate = true;
			}
		}

		/* Only invalidate once, and if needed */
		if (invalidate) {
			for (const Vehicle *w = GetFirstVehicleFromSharedList(v); w != NULL; w = w->next_shared) {
				InvalidateVehicleOrder(w);
			}
		}
	}
}

/**
 *
 * Checks if a vehicle has a GOTO_DEPOT in his order list
 *
 * @return True if this is true (lol ;))
 *
 */
bool VehicleHasDepotOrders(const Vehicle *v)
{
	const Order *order;

	FOR_VEHICLE_ORDERS(v, order) {
		if (order->type == OT_GOTO_DEPOT)
			return true;
	}

	return false;
}

/**
 *
 * Delete all orders from a vehicle
 *
 */
void DeleteVehicleOrders(Vehicle *v)
{
	DeleteOrderWarnings(v);

	/* If we have a shared order-list, don't delete the list, but just
	    remove our pointer */
	if (v->IsOrderListShared()) {
		Vehicle *u = v;

		v->orders = NULL;
		v->num_orders = 0;

		/* Unlink ourself */
		if (v->prev_shared != NULL) {
			v->prev_shared->next_shared = v->next_shared;
			u = v->prev_shared;
		}
		if (v->next_shared != NULL) {
			v->next_shared->prev_shared = v->prev_shared;
			u = v->next_shared;
		}
		v->prev_shared = NULL;
		v->next_shared = NULL;

		/* If we are the only one left in the Shared Order Vehicle List,
		 *  remove it, as we are no longer a Shared Order Vehicle */
		if (u->prev_shared == NULL && u->next_shared == NULL && u->orders != NULL) RemoveSharedOrderVehicleList(u);

		/* We only need to update this-one, because if there is a third
		 *  vehicle which shares the same order-list, nothing will change. If
		 *  this is the last vehicle, the last line of the order-window
		 *  will change from Shared order list, to Order list, so it needs
		 *  an update */
		InvalidateVehicleOrder(u);
		return;
	}

	/* Remove the orders */
	Order *cur = v->orders;
	/* Delete the vehicle list of shared orders, if any */
	if (cur != NULL) RemoveSharedOrderVehicleList(v);
	v->orders = NULL;
	v->num_orders = 0;

	if (cur != NULL) {
		cur->FreeChain(); // Free the orders.
	}
}

Date GetServiceIntervalClamped(uint index)
{
	return (_patches.servint_ispercent) ? Clamp(index, MIN_SERVINT_PERCENT, MAX_SERVINT_PERCENT) : Clamp(index, MIN_SERVINT_DAYS, MAX_SERVINT_DAYS);
}

/**
 *
 * Check if a vehicle has any valid orders
 *
 * @return false if there are no valid orders
 *
 */
static bool CheckForValidOrders(const Vehicle *v)
{
	const Order *order;

	FOR_VEHICLE_ORDERS(v, order) if (order->type != OT_DUMMY) return true;

	return false;
}

/**
 * Handle the orders of a vehicle and determine the next place
 * to go to if needed.
 * @param v the vehicle to do this for.
 * @return true *if* the vehicle is eligible for reversing
 *              (basically only when leaving a station).
 */
bool ProcessOrders(Vehicle *v)
{
	switch (v->current_order.type) {
		case OT_GOTO_DEPOT:
			/* Let a depot order in the orderlist interrupt. */
			if (!(v->current_order.flags & OFB_PART_OF_ORDERS)) return false;

			if ((v->current_order.flags & OFB_SERVICE_IF_NEEDED) && !v->NeedsServicing()) {
				UpdateVehicleTimetable(v, true);
				v->cur_order_index++;
			}
			break;

		case OT_LOADING:
			return false;

		case OT_LEAVESTATION:
			if (v->type != VEH_AIRCRAFT) return false;
			break;

		default: break;
	}

	/**
	 * Reversing because of order change is allowed only just after leaving a
	 * station (and the difficulty setting to allowed, of course)
	 * this can be detected because only after OT_LEAVESTATION, current_order
	 * will be reset to nothing. (That also happens if no order, but in that case
	 * it won't hit the point in code where may_reverse is checked)
	 */
	bool may_reverse = v->current_order.type == OT_NOTHING;

	/* Check if we've reached the waypoint? */
	if (v->current_order.type == OT_GOTO_WAYPOINT && v->tile == v->dest_tile) {
		UpdateVehicleTimetable(v, true);
		v->cur_order_index++;
	}

	/* Check if we've reached a non-stop station while TTDPatch nonstop is enabled.. */
	if (_patches.new_nonstop &&
			v->current_order.flags & OFB_NON_STOP &&
			IsTileType(v->tile, MP_STATION) &&
			v->current_order.dest == GetStationIndex(v->tile)) {
		UpdateVehicleTimetable(v, true);
		v->cur_order_index++;
	}

	/* Get the current order */
	if (v->cur_order_index >= v->num_orders) v->cur_order_index = 0;

	const Order *order = GetVehicleOrder(v, v->cur_order_index);

	/* If no order, do nothing. */
	if (order == NULL || (v->type == VEH_AIRCRAFT && order->type == OT_DUMMY && !CheckForValidOrders(v))) {
		if (v->type == VEH_AIRCRAFT) {
			/* Aircraft do something vastly different here, so handle separately */
			extern void HandleMissingAircraftOrders(Vehicle *v);
			HandleMissingAircraftOrders(v);
			return false;
		}

		v->current_order.Free();
		v->dest_tile = 0;
		if (v->type == VEH_ROAD) ClearSlot(v);
		return false;
	}

	/* If it is unchanged, keep it. */
	if (order->type  == v->current_order.type &&
			order->flags == v->current_order.flags &&
			order->dest  == v->current_order.dest &&
			(v->type != VEH_SHIP || order->type != OT_GOTO_STATION || GetStation(order->dest)->dock_tile != 0)) {
		return false;
	}

	/* Otherwise set it, and determine the destination tile. */
	v->current_order = *order;

	InvalidateVehicleOrder(v);
	switch (v->type) {
		default:
			NOT_REACHED();

		case VEH_ROAD:
		case VEH_TRAIN:
			break;

		case VEH_AIRCRAFT:
		case VEH_SHIP:
			InvalidateWindowClasses(v->GetVehicleListWindowClass());
			break;
	}

	switch (order->type) {
		case OT_GOTO_STATION:
			v->dest_tile = v->GetOrderStationLocation(order->dest);
			break;

		case OT_GOTO_DEPOT:
			if (v->type != VEH_AIRCRAFT) v->dest_tile = GetDepot(order->dest)->xy;
			break;

		case OT_GOTO_WAYPOINT:
			v->dest_tile = GetWaypoint(order->dest)->xy;
			break;

		default:
			v->dest_tile = 0;
			return false;
	}

	return may_reverse;
}

void InitializeOrders()
{
	_Order_pool.CleanPool();
	_Order_pool.AddBlockToPool();

	_backup_orders_tile = 0;
}

static const SaveLoad _order_desc[] = {
	SLE_VAR(Order, type,  SLE_UINT8),
	SLE_VAR(Order, flags, SLE_UINT8),
	SLE_VAR(Order, dest,  SLE_UINT16),
	SLE_REF(Order, next,  REF_ORDER),
	SLE_CONDVAR(Order, refit_cargo,    SLE_UINT8,  36, SL_MAX_VERSION),
	SLE_CONDVAR(Order, refit_subtype,  SLE_UINT8,  36, SL_MAX_VERSION),
	SLE_CONDVAR(Order, wait_time,      SLE_UINT16, 67, SL_MAX_VERSION),
	SLE_CONDVAR(Order, travel_time,    SLE_UINT16, 67, SL_MAX_VERSION),

	/* Leftover from the minor savegame version stuff
	 * We will never use those free bytes, but we have to keep this line to allow loading of old savegames */
	SLE_CONDNULL(10, 5, 35),
	SLE_END()
};

static void Save_ORDR()
{
	Order *order;

	FOR_ALL_ORDERS(order) {
		SlSetArrayIndex(order->index);
		SlObject(order, _order_desc);
	}
}

static void Load_ORDR()
{
	if (CheckSavegameVersionOldStyle(5, 2)) {
		/* Version older than 5.2 did not have a ->next pointer. Convert them
		    (in the old days, the orderlist was 5000 items big) */
		uint len = SlGetFieldLength();
		uint i;

		if (CheckSavegameVersion(5)) {
			/* Pre-version 5 had an other layout for orders
			    (uint16 instead of uint32) */
			len /= sizeof(uint16);
			uint16 *orders = MallocT<uint16>(len + 1);

			SlArray(orders, len, SLE_UINT16);

			for (i = 0; i < len; ++i) {
				Order *order = new (i) Order();
				AssignOrder(order, UnpackVersion4Order(orders[i]));
			}

			free(orders);
		} else if (CheckSavegameVersionOldStyle(5, 2)) {
			len /= sizeof(uint16);
			uint16 *orders = MallocT<uint16>(len + 1);

			SlArray(orders, len, SLE_UINT32);

			for (i = 0; i < len; ++i) {
				Order *order = new (i) Order();
				AssignOrder(order, UnpackOrder(orders[i]));
			}

			free(orders);
		}

		/* Update all the next pointer */
		for (i = 1; i < len; ++i) {
			/* The orders were built like this:
			 *   While the order is valid, set the previous will get it's next pointer set
			 *   We start with index 1 because no order will have the first in it's next pointer */
			if (GetOrder(i)->IsValid())
				GetOrder(i - 1)->next = GetOrder(i);
		}
	} else {
		int index;

		while ((index = SlIterateArray()) != -1) {
			Order *order = new (index) Order();
			SlObject(order, _order_desc);
		}
	}
}

extern const ChunkHandler _order_chunk_handlers[] = {
	{ 'ORDR', Save_ORDR, Load_ORDR, CH_ARRAY | CH_LAST},
};
