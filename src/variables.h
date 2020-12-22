/* $Id$ */

/** @file variables.h Messing file that will cease to exist some time in the future. */

#ifndef VARIABLES_H
#define VARIABLES_H

#include "tile_type.h"
#include "strings_type.h"

/* Amount of game ticks */
VARDEF uint16 _tick_counter;

/* Skip aging of cargo? */
VARDEF byte _age_cargo_skip_counter;

/* Position in tile loop */
VARDEF TileIndex _cur_tileloop_tile;

/* Also save scrollpos_x, scrollpos_y and zoom */
VARDEF uint16 _disaster_delay;

/* Determines what station to operate on in the
 *  tick handler. */
VARDEF uint16 _station_tick_ctr;

/* Determines how often to run the tree loop */
VARDEF byte _trees_tick_ctr;

/* Keep track of current game position */
VARDEF int _saved_scrollpos_x;
VARDEF int _saved_scrollpos_y;

struct Cheat {
	bool been_used; // has this cheat been used before?
	bool value;     // tells if the bool cheat is active or not
};


/* WARNING! Do _not_ remove entries in Cheats struct or change the order
 * of the existing ones! Would break downward compatibility.
 * Only add new entries at the end of the struct! */

struct Cheats {
	Cheat magic_bulldozer;  // dynamite industries, unmovables
	Cheat switch_player;    // change to another player
	Cheat money;            // get rich
	Cheat crossing_tunnels; // allow tunnels that cross each other
	Cheat build_in_pause;   // build while in pause mode
	Cheat no_jetcrash;      // no jet will crash on small airports anymore
	Cheat switch_climate;
	Cheat change_date;      // changes date ingame
	Cheat setup_prod;       // setup raw-material production in game
	Cheat dummy;            // empty cheat (enable running el-engines on normal rail)
};

VARDEF Cheats _cheats;

/* NOSAVE: Used in palette animations only, not really important. */
VARDEF int _palette_animation_counter;


VARDEF uint32 _frame_counter;
VARDEF uint32 _realtime_tick;

VARDEF bool _is_old_ai_player; // current player is an oldAI player? (enables a lot of cheats..)

VARDEF bool _do_autosave;
VARDEF int _autosave_ctr;

VARDEF byte _display_opt;

VARDEF bool _rightclick_emulate;

/* IN/OUT parameters to commands */
VARDEF TileIndex _build_tunnel_endtile;
VARDEF bool _generating_world;

/* Used when switching from the intro menu. */
VARDEF byte _switch_mode;
VARDEF StringID _switch_mode_errorstr;

VARDEF char *_config_file;
VARDEF char *_highscore_file;
VARDEF char *_log_file;

/* landscape.cpp */
extern const byte _tileh_to_sprite[32];

extern bool _draw_bounding_boxes;

/* misc */
VARDEF byte _vehicle_design_names;

/* Forking stuff */
VARDEF bool _dedicated_forks;

#endif /* VARIABLES_H */
