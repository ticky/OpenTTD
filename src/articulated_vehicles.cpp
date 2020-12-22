/* $Id$ */

/** @file articulated_vehicles.cpp Implementation of articulated vehicles. */

#include "stdafx.h"
#include "openttd.h"
#include "articulated_vehicles.h"
#include "engine.h"
#include "train.h"
#include "roadveh.h"
#include "newgrf_callbacks.h"
#include "newgrf_engine.h"
#include "vehicle_func.h"

#include "safeguards.h"

uint CountArticulatedParts(EngineID engine_type, bool purchase_window)
{
	if (!HasBit(EngInfo(engine_type)->callbackmask, CBM_VEHICLE_ARTIC_ENGINE)) return 0;

	Vehicle *v = NULL;;
	if (!purchase_window) {
		v = new InvalidVehicle();
		v->engine_type = engine_type;
	}

	uint i;
	for (i = 1; i < MAX_UVALUE(EngineID); i++) {
		uint16 callback = GetVehicleCallback(CBID_VEHICLE_ARTIC_ENGINE, i, 0, engine_type, v);
		if (callback == CALLBACK_FAILED || GB(callback, 0, 8) == 0xFF) break;
	}

	delete v;

	return i - 1;
}


uint16 *GetCapacityOfArticulatedParts(EngineID engine, VehicleType type)
{
	static uint16 capacity[NUM_CARGO];
	memset(capacity, 0, sizeof(capacity));

	if (type == VEH_TRAIN) {
		const RailVehicleInfo *rvi = RailVehInfo(engine);
		capacity[rvi->cargo_type] = GetEngineProperty(engine, 0x14, rvi->capacity);
		if (rvi->railveh_type == RAILVEH_MULTIHEAD) capacity[rvi->cargo_type] += rvi->capacity;
	} else if (type == VEH_ROAD) {
		const RoadVehicleInfo *rvi = RoadVehInfo(engine);
		capacity[rvi->cargo_type] = GetEngineProperty(engine, 0x0F, rvi->capacity);
	}

	if (!HasBit(EngInfo(engine)->callbackmask, CBM_VEHICLE_ARTIC_ENGINE)) return capacity;

	for (uint i = 1; i < MAX_UVALUE(EngineID); i++) {
		uint16 callback = GetVehicleCallback(CBID_VEHICLE_ARTIC_ENGINE, i, 0, engine, NULL);
		if (callback == CALLBACK_FAILED || GB(callback, 0, 8) == 0xFF) break;

		EngineID artic_engine = GetFirstEngineOfType(type) + GB(callback, 0, 7);

		if (type == VEH_TRAIN) {
			const RailVehicleInfo *rvi = RailVehInfo(artic_engine);
			capacity[rvi->cargo_type] += GetEngineProperty(artic_engine, 0x14, rvi->capacity);
		} else if (type == VEH_ROAD) {
			const RoadVehicleInfo *rvi = RoadVehInfo(artic_engine);
			capacity[rvi->cargo_type] += GetEngineProperty(artic_engine, 0x0F, rvi->capacity);
		}
	}

	return capacity;
}


void AddArticulatedParts(Vehicle **vl, VehicleType type)
{
	const Vehicle *v = vl[0];
	Vehicle *u = vl[0];

	if (!HasBit(EngInfo(v->engine_type)->callbackmask, CBM_VEHICLE_ARTIC_ENGINE)) return;

	for (uint i = 1; i < MAX_UVALUE(EngineID); i++) {
		uint16 callback = GetVehicleCallback(CBID_VEHICLE_ARTIC_ENGINE, i, 0, v->engine_type, v);
		if (callback == CALLBACK_FAILED || GB(callback, 0, 8) == 0xFF) return;

		/* Attempt to use pre-allocated vehicles until they run out. This can happen
		 * if the callback returns different values depending on the cargo type. */
		u->SetNext(vl[i]);
		if (u->Next() == NULL) return;

		Vehicle *previous = u;
		u = u->Next();

		EngineID engine_type = GetFirstEngineOfType(type) + GB(callback, 0, 7);
		bool flip_image = HasBit(callback, 7);

		/* get common values from first engine */
		u->direction = v->direction;
		u->owner = v->owner;
		u->tile = v->tile;
		u->x_pos = v->x_pos;
		u->y_pos = v->y_pos;
		u->z_pos = v->z_pos;
		u->build_year = v->build_year;
		u->vehstatus = v->vehstatus & ~VS_STOPPED;

		u->cargo_subtype = 0;
		u->max_speed = 0;
		u->max_age = 0;
		u->engine_type = engine_type;
		u->value = 0;
		u->subtype = 0;
		u->cur_image = 0xAC2;
		u->random_bits = VehicleRandomBits();

		switch (type) {
			default: NOT_REACHED();

			case VEH_TRAIN: {
				const RailVehicleInfo *rvi_artic = RailVehInfo(engine_type);

				u = new (u) Train();
				previous->SetNext(u);
				u->u.rail.track = v->u.rail.track;
				u->u.rail.railtype = v->u.rail.railtype;
				u->u.rail.first_engine = v->engine_type;

				u->spritenum = rvi_artic->image_index;
				u->cargo_type = rvi_artic->cargo_type;
				u->cargo_cap = rvi_artic->capacity;

				SetArticulatedPart(u);
				break;
			}

			case VEH_ROAD: {
				const RoadVehicleInfo *rvi_artic = RoadVehInfo(engine_type);

				u = new (u) RoadVehicle();
				previous->SetNext(u);
				u->u.road.first_engine = v->engine_type;
				u->u.road.cached_veh_length = GetRoadVehLength(u);
				u->u.road.state = RVSB_IN_DEPOT;

				u->u.road.roadtype = v->u.road.roadtype;
				u->u.road.compatible_roadtypes = v->u.road.compatible_roadtypes;

				u->spritenum = rvi_artic->image_index;
				u->cargo_type = rvi_artic->cargo_type;
				u->cargo_cap = rvi_artic->capacity;

				SetRoadVehArticPart(u);
				break;
			}
		}

		if (flip_image) u->spritenum++;

		VehiclePositionChanged(u);
	}
}
