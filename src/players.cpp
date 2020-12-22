/* $Id$ */

/** @file players.cpp Handling of players. */

#include "stdafx.h"
#include "openttd.h"
#include "engine.h"
#include "player_func.h"
#include "player_gui.h"
#include "town.h"
#include "station.h"
#include "news.h"
#include "saveload.h"
#include "command_func.h"
#include "network/network.h"
#include "network/network_internal.h"
#include "variables.h"
#include "engine.h"
#include "ai/ai.h"
#include "player_face.h"
#include "group.h"
#include "window_func.h"
#include "tile_map.h"
#include "strings_func.h"
#include "gfx_func.h"
#include "functions.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "autoreplace_func.h"
#include "autoreplace_gui.h"
#include "string_func.h"
#include "ai/default/default.h"
#include "ai/trolly/trolly.h"
#include "road_func.h"
#include "rail.h"

#include "table/strings.h"
#include "table/sprites.h"

Player _players[MAX_PLAYERS];
PlayerByte _local_player;
PlayerByte _current_player;
/* NOSAVE: can be determined from player structs */
byte _player_colors[MAX_PLAYERS];
PlayerFace _player_face; ///< for player face storage in openttd.cfg
uint _next_competitor_start;      ///< the number of ticks before the next AI is started
uint _cur_player_tick_index;     ///< used to generate a name for one company that doesn't have a name yet per tick
HighScore _highscore_table[5][5]; // 4 difficulty-settings (+ network); top 5

/**
 * Sets the local player and updates the patch settings that are set on a
 * per-company (player) basis to reflect the core's state in the GUI.
 * @param new_player the new player
 * @pre IsValidPlayer(new_player) || new_player == PLAYER_SPECTATOR || new_player == OWNER_NONE
 */
void SetLocalPlayer(PlayerID new_player)
{
	/* Player could also be PLAYER_SPECTATOR or OWNER_NONE */
	assert(IsValidPlayer(new_player) || new_player == PLAYER_SPECTATOR || new_player == OWNER_NONE);

	_local_player = new_player;

	/* Do not update the patches if we are in the intro GUI */
	if (IsValidPlayer(new_player) && _game_mode != GM_MENU) {
		const Player *p = GetPlayer(new_player);
		_patches.autorenew        = p->engine_renew;
		_patches.autorenew_months = p->engine_renew_months;
		_patches.autorenew_money  = p->engine_renew_money;
		InvalidateWindow(WC_GAME_OPTIONS, 0);
	}
}

bool IsHumanPlayer(PlayerID pi)
{
	return !GetPlayer(pi)->is_ai;
}


uint16 GetDrawStringPlayerColor(PlayerID player)
{
	/* Get the color for DrawString-subroutines which matches the color
	 * of the player */
	if (!IsValidPlayer(player)) return _colour_gradient[COLOUR_WHITE][4] | TC_IS_PALETTE_COLOUR;
	return (_colour_gradient[_player_colors[player]][4]) | TC_IS_PALETTE_COLOUR;
}

void DrawPlayerIcon(PlayerID p, int x, int y)
{
	DrawSprite(SPR_PLAYER_ICON, PLAYER_SPRITE_COLOR(p), x, y);
}

/**
 * Converts an old player face format to the new player face format
 *
 * Meaning of the bits in the old face (some bits are used in several times):
 * - 4 and 5: chin
 * - 6 to 9: eyebrows
 * - 10 to 13: nose
 * - 13 to 15: lips (also moustache for males)
 * - 16 to 19: hair
 * - 20 to 22: eye color
 * - 20 to 27: tie, ear rings etc.
 * - 28 to 30: glasses
 * - 19, 26 and 27: race (bit 27 set and bit 19 equal to bit 26 = black, otherwise white)
 * - 31: gender (0 = male, 1 = female)
 *
 * @param face the face in the old format
 * @return the face in the new format
 */
PlayerFace ConvertFromOldPlayerFace(uint32 face)
{
	PlayerFace pf = 0;
	GenderEthnicity ge = GE_WM;

	if (HasBit(face, 31)) SetBit(ge, GENDER_FEMALE);
	if (HasBit(face, 27) && (HasBit(face, 26) == HasBit(face, 19))) SetBit(ge, ETHNICITY_BLACK);

	SetPlayerFaceBits(pf, PFV_GEN_ETHN,    ge, ge);
	SetPlayerFaceBits(pf, PFV_HAS_GLASSES, ge, GB(face, 28, 3) <= 1);
	SetPlayerFaceBits(pf, PFV_EYE_COLOUR,  ge, HasBit(ge, ETHNICITY_BLACK) ? 0 : ClampU(GB(face, 20, 3), 5, 7) - 5);
	SetPlayerFaceBits(pf, PFV_CHIN,        ge, ScalePlayerFaceValue(PFV_CHIN,     ge, GB(face,  4, 2)));
	SetPlayerFaceBits(pf, PFV_EYEBROWS,    ge, ScalePlayerFaceValue(PFV_EYEBROWS, ge, GB(face,  6, 4)));
	SetPlayerFaceBits(pf, PFV_HAIR,        ge, ScalePlayerFaceValue(PFV_HAIR,     ge, GB(face, 16, 4)));
	SetPlayerFaceBits(pf, PFV_JACKET,      ge, ScalePlayerFaceValue(PFV_JACKET,   ge, GB(face, 20, 2)));
	SetPlayerFaceBits(pf, PFV_COLLAR,      ge, ScalePlayerFaceValue(PFV_COLLAR,   ge, GB(face, 22, 2)));
	SetPlayerFaceBits(pf, PFV_GLASSES,     ge, GB(face, 28, 1));

	uint lips = GB(face, 10, 4);
	if (!HasBit(ge, GENDER_FEMALE) && lips < 4) {
		SetPlayerFaceBits(pf, PFV_HAS_MOUSTACHE, ge, true);
		SetPlayerFaceBits(pf, PFV_MOUSTACHE,     ge, max(lips, 1U) - 1);
	} else {
		if (!HasBit(ge, GENDER_FEMALE)) {
			lips = lips * 15 / 16;
			lips -= 3;
			if (HasBit(ge, ETHNICITY_BLACK) && lips > 8) lips = 0;
		} else {
			lips = ScalePlayerFaceValue(PFV_LIPS, ge, lips);
		}
		SetPlayerFaceBits(pf, PFV_LIPS, ge, lips);

		uint nose = GB(face, 13, 3);
		if (ge == GE_WF) {
			nose = (nose * 3 >> 3) * 3 >> 2; // There is 'hole' in the nose sprites for females
		} else {
			nose = ScalePlayerFaceValue(PFV_NOSE, ge, nose);
		}
		SetPlayerFaceBits(pf, PFV_NOSE, ge, nose);
	}

	uint tie_earring = GB(face, 24, 4);
	if (!HasBit(ge, GENDER_FEMALE) || tie_earring < 3) { // Not all females have an earring
		if (HasBit(ge, GENDER_FEMALE)) SetPlayerFaceBits(pf, PFV_HAS_TIE_EARRING, ge, true);
		SetPlayerFaceBits(pf, PFV_TIE_EARRING, ge, HasBit(ge, GENDER_FEMALE) ? tie_earring : ScalePlayerFaceValue(PFV_TIE_EARRING, ge, tie_earring / 2));
	}

	return pf;
}

/**
 * Checks whether a player's face is a valid encoding.
 * Unused bits are not enforced to be 0.
 * @param pf the fact to check
 * @return true if and only if the face is valid
 */
bool IsValidPlayerFace(PlayerFace pf)
{
	if (!ArePlayerFaceBitsValid(pf, PFV_GEN_ETHN, GE_WM)) return false;

	GenderEthnicity ge   = (GenderEthnicity)GetPlayerFaceBits(pf, PFV_GEN_ETHN, GE_WM);
	bool has_moustache   = !HasBit(ge, GENDER_FEMALE) && GetPlayerFaceBits(pf, PFV_HAS_MOUSTACHE,   ge) != 0;
	bool has_tie_earring = !HasBit(ge, GENDER_FEMALE) || GetPlayerFaceBits(pf, PFV_HAS_TIE_EARRING, ge) != 0;
	bool has_glasses     = GetPlayerFaceBits(pf, PFV_HAS_GLASSES, ge) != 0;

	if (!ArePlayerFaceBitsValid(pf, PFV_EYE_COLOUR, ge)) return false;
	for (PlayerFaceVariable pfv = PFV_CHEEKS; pfv < PFV_END; pfv++) {
		switch (pfv) {
			case PFV_MOUSTACHE:   if (!has_moustache)   continue; break;
			case PFV_LIPS:        /* FALL THROUGH */
			case PFV_NOSE:        if (has_moustache)    continue; break;
			case PFV_TIE_EARRING: if (!has_tie_earring) continue; break;
			case PFV_GLASSES:     if (!has_glasses)     continue; break;
			default: break;
		}
		if (!ArePlayerFaceBitsValid(pf, pfv, ge)) return false;
	}

	return true;
}

void InvalidatePlayerWindows(const Player *p)
{
	PlayerID pid = p->index;

	if (pid == _local_player) InvalidateWindow(WC_STATUS_BAR, 0);
	InvalidateWindow(WC_FINANCES, pid);
}

bool CheckPlayerHasMoney(CommandCost cost)
{
	if (cost.GetCost() > 0) {
		PlayerID pid = _current_player;
		if (IsValidPlayer(pid) && cost.GetCost() > GetPlayer(pid)->player_money) {
			SetDParam(0, cost.GetCost());
			_error_message = STR_0003_NOT_ENOUGH_CASH_REQUIRES;
			return false;
		}
	}
	return true;
}

static void SubtractMoneyFromAnyPlayer(Player *p, CommandCost cost)
{
	if (cost.GetCost() == 0) return;
	assert(cost.GetExpensesType() != INVALID_EXPENSES);

	p->player_money -= cost.GetCost();
	p->yearly_expenses[0][cost.GetExpensesType()] += cost.GetCost();

	if (HasBit(1 << EXPENSES_TRAIN_INC    |
	           1 << EXPENSES_ROADVEH_INC  |
	           1 << EXPENSES_AIRCRAFT_INC |
	           1 << EXPENSES_SHIP_INC, cost.GetExpensesType())) {
		p->cur_economy.income -= cost.GetCost();
	} else if (HasBit(1 << EXPENSES_TRAIN_RUN    |
	                  1 << EXPENSES_ROADVEH_RUN  |
	                  1 << EXPENSES_AIRCRAFT_RUN |
	                  1 << EXPENSES_SHIP_RUN     |
	                  1 << EXPENSES_PROPERTY     |
	                  1 << EXPENSES_LOAN_INT, cost.GetExpensesType())) {
		p->cur_economy.expenses -= cost.GetCost();
	}

	InvalidatePlayerWindows(p);
}

void SubtractMoneyFromPlayer(CommandCost cost)
{
	PlayerID pid = _current_player;

	if (IsValidPlayer(pid)) SubtractMoneyFromAnyPlayer(GetPlayer(pid), cost);
}

void SubtractMoneyFromPlayerFract(PlayerID player, CommandCost cst)
{
	Player *p = GetPlayer(player);
	byte m = p->player_money_fraction;
	Money cost = cst.GetCost();

	p->player_money_fraction = m - (byte)cost;
	cost >>= 8;
	if (p->player_money_fraction > m) cost++;
	if (cost != 0) SubtractMoneyFromAnyPlayer(p, CommandCost(cst.GetExpensesType(), cost));
}

void GetNameOfOwner(Owner owner, TileIndex tile)
{
	SetDParam(2, owner);

	if (owner != OWNER_TOWN) {
		if (!IsValidPlayer(owner)) {
			SetDParam(0, STR_0150_SOMEONE);
		} else {
			const Player* p = GetPlayer(owner);

			SetDParam(0, STR_COMPANY_NAME);
			SetDParam(1, p->index);
		}
	} else {
		const Town* t = ClosestTownFromTile(tile, (uint)-1);

		SetDParam(0, STR_TOWN);
		SetDParam(1, t->index);
	}
}


bool CheckOwnership(PlayerID owner)
{
	assert(owner < OWNER_END);

	if (owner == _current_player) return true;
	_error_message = STR_013B_OWNED_BY;
	GetNameOfOwner(owner, 0);
	return false;
}

bool CheckTileOwnership(TileIndex tile)
{
	Owner owner = GetTileOwner(tile);

	assert(owner < OWNER_END);

	if (owner == _current_player) return true;
	_error_message = STR_013B_OWNED_BY;

	/* no need to get the name of the owner unless we're the local player (saves some time) */
	if (IsLocalPlayer()) GetNameOfOwner(owner, tile);
	return false;
}

static void GenerateCompanyName(Player *p)
{
	TileIndex tile;
	Town *t;
	StringID str;
	Player *pp;
	uint32 strp;
	char buffer[100];

	if (p->name_1 != STR_SV_UNNAMED) return;

	tile = p->last_build_coordinate;
	if (tile == 0) return;

	t = ClosestTownFromTile(tile, (uint)-1);

	if (t->name == NULL && IsInsideMM(t->townnametype, SPECSTR_TOWNNAME_START, SPECSTR_TOWNNAME_LAST + 1)) {
		str = t->townnametype - SPECSTR_TOWNNAME_START + SPECSTR_PLAYERNAME_START;
		strp = t->townnameparts;

verify_name:;
		/* No player must have this name already */
		FOR_ALL_PLAYERS(pp) {
			if (pp->name_1 == str && pp->name_2 == strp) goto bad_town_name;
		}

		GetString(buffer, str, lastof(buffer));
		if (strlen(buffer) >= 32 || GetStringBoundingBox(buffer).width >= 150)
			goto bad_town_name;

set_name:;
		p->name_1 = str;
		p->name_2 = strp;

		MarkWholeScreenDirty();

		if (!IsHumanPlayer(p->index)) {
			SetDParam(0, t->index);
			AddNewsItem((StringID)(p->index | NB_BNEWCOMPANY), NEWS_FLAGS(NM_CALLBACK, NF_TILE, NT_COMPANY_INFO, DNC_BANKRUPCY), p->last_build_coordinate, 0);
		}
		return;
	}
bad_town_name:;

	if (p->president_name_1 == SPECSTR_PRESIDENT_NAME) {
		str = SPECSTR_ANDCO_NAME;
		strp = p->president_name_2;
		goto set_name;
	} else {
		str = SPECSTR_ANDCO_NAME;
		strp = Random();
		goto verify_name;
	}
}

#define COLOR_SWAP(i, j) do { byte t = colors[i];colors[i] = colors[j];colors[j] = t; } while(0)

static const byte _color_sort[16] = {2, 2, 3, 2, 3, 2, 3, 2, 3, 2, 2, 2, 3, 1, 1, 1};
static const byte _color_similar_1[16] = {8, 6, 255, 12,  255, 0, 1, 1, 0, 13,  11,  10, 3,   9,  15, 14};
static const byte _color_similar_2[16] = {5, 7, 255, 255, 255, 8, 7, 6, 5, 12, 255, 255, 9, 255, 255, 255};

static byte GeneratePlayerColor()
{
	byte colors[16], pcolor, t2;
	int i, j, n;
	uint32 r;
	Player *p;

	/* Initialize array */
	for (i = 0; i != 16; i++) colors[i] = i;

	/* And randomize it */
	n = 100;
	do {
		r = Random();
		COLOR_SWAP(GB(r, 0, 4), GB(r, 4, 4));
	} while (--n);

	/* Bubble sort it according to the values in table 1 */
	i = 16;
	do {
		for (j = 0; j != 15; j++) {
			if (_color_sort[colors[j]] < _color_sort[colors[j + 1]]) {
				COLOR_SWAP(j, j + 1);
			}
		}
	} while (--i);

	/* Move the colors that look similar to each player's color to the side */
	FOR_ALL_PLAYERS(p) if (p->is_active) {
		pcolor = p->player_color;
		for (i = 0; i != 16; i++) if (colors[i] == pcolor) {
			colors[i] = 0xFF;

			t2 = _color_similar_1[pcolor];
			if (t2 == 0xFF) break;
			for (i = 0; i != 15; i++) {
				if (colors[i] == t2) {
					do COLOR_SWAP(i, i + 1); while (++i != 15);
					break;
				}
			}

			t2 = _color_similar_2[pcolor];
			if (t2 == 0xFF) break;
			for (i = 0; i != 15; i++) {
				if (colors[i] == t2) {
					do COLOR_SWAP(i, i + 1); while (++i != 15);
					break;
				}
			}
			break;
		}
	}

	/* Return the first available color */
	for (i = 0;; i++) {
		if (colors[i] != 0xFF) return colors[i];
	}
}

static void GeneratePresidentName(Player *p)
{
	Player *pp;
	char buffer[100], buffer2[40];

	for (;;) {
restart:;

		p->president_name_2 = Random();
		p->president_name_1 = SPECSTR_PRESIDENT_NAME;

		SetDParam(0, p->index);
		GetString(buffer, STR_PLAYER_NAME, lastof(buffer));
		if (strlen(buffer) >= 32 || GetStringBoundingBox(buffer).width >= 94)
			continue;

		FOR_ALL_PLAYERS(pp) {
			if (pp->is_active && p != pp) {
				SetDParam(0, pp->index);
				GetString(buffer2, STR_PLAYER_NAME, lastof(buffer2));
				if (strcmp(buffer2, buffer) == 0)
					goto restart;
			}
		}
		return;
	}
}

static Player *AllocatePlayer()
{
	Player *p;
	/* Find a free slot */
	FOR_ALL_PLAYERS(p) {
		if (!p->is_active) {
			free(p->name);
			free(p->president_name);
			PlayerID i = p->index;
			memset(p, 0, sizeof(Player));
			memset(&_players_ai[i], 0, sizeof(PlayerAI));
			memset(&_players_ainew[i], 0, sizeof(PlayerAiNew));
			p->index = i;
			return p;
		}
	}
	return NULL;
}

void ResetPlayerLivery(Player *p)
{
	for (LiveryScheme scheme = LS_BEGIN; scheme < LS_END; scheme++) {
		p->livery[scheme].in_use  = false;
		p->livery[scheme].colour1 = p->player_color;
		p->livery[scheme].colour2 = p->player_color;
	}
}

/**
 * Create a new player and sets all player variables default values
 *
 * @param is_ai is a ai player?
 * @return the player struct
 */
Player *DoStartupNewPlayer(bool is_ai)
{
	Player *p;

	p = AllocatePlayer();
	if (p == NULL) return NULL;

	/* Make a color */
	p->player_color = GeneratePlayerColor();
	ResetPlayerLivery(p);
	_player_colors[p->index] = p->player_color;
	p->name_1 = STR_SV_UNNAMED;
	p->is_active = true;

	p->player_money = p->current_loan = 100000;

	p->is_ai = is_ai;
	_players_ai[p->index].state = 5; // AIS_WANT_NEW_ROUTE
	p->share_owners[0] = p->share_owners[1] = p->share_owners[2] = p->share_owners[3] = PLAYER_SPECTATOR;

	p->avail_railtypes = GetPlayerRailtypes(p->index);
	p->avail_roadtypes = GetPlayerRoadtypes(p->index);
	p->inaugurated_year = _cur_year;
	RandomPlayerFaceBits(p->face, (GenderEthnicity)Random(), false); // create a random player face

	/* Engine renewal settings */
	p->engine_renew_list = NULL;
	p->renew_keep_length = false;
	p->engine_renew = _patches_newgame.autorenew;
	p->engine_renew_months = _patches_newgame.autorenew_months;
	p->engine_renew_money = _patches_newgame.autorenew_money;

	GeneratePresidentName(p);

	InvalidateWindow(WC_GRAPH_LEGEND, 0);
	InvalidateWindow(WC_TOOLBAR_MENU, 0);
	InvalidateWindow(WC_CLIENT_LIST, 0);

	if (is_ai && (!_networking || _network_server) && _ai.enabled)
		AI_StartNewAI(p->index);

	memset(p->num_engines, 0, sizeof(p->num_engines));

	return p;
}

void StartupPlayers()
{
	/* The AI starts like in the setting with +2 month max */
	_next_competitor_start = _opt.diff.competitor_start_time * 90 * DAY_TICKS + RandomRange(60 * DAY_TICKS) + 1;
}

static void MaybeStartNewPlayer()
{
	uint n;
	Player *p;

	/* count number of competitors */
	n = 0;
	FOR_ALL_PLAYERS(p) {
		if (p->is_active && p->is_ai) n++;
	}

	/* if we've got an AI controlled local player, allow for a full roster of AI players */
	if (_local_player == PLAYER_SPECTATOR) {
		n--;
	}

	/* when there's a lot of computers in game, the probability that a new one starts is lower */
	if (n < (uint)_opt.diff.max_no_competitors &&
			n < (_network_server ?
				InteractiveRandomRange(_opt.diff.max_no_competitors + 2) :
				RandomRange(_opt.diff.max_no_competitors + 2)
			)) {
		/* Send a command to all clients to start up a new AI.
		 * Works fine for Multiplayer and Singleplayer */
		DoCommandP(0, 1, 0, NULL, CMD_PLAYER_CTRL);
	}

	/* The next AI starts like the difficulty setting said, with +2 month max */
	_next_competitor_start = _opt.diff.competitor_start_time * 90 * DAY_TICKS + 1;
	_next_competitor_start += _network_server ? InteractiveRandomRange(60 * DAY_TICKS) : RandomRange(60 * DAY_TICKS);
}

void InitializePlayers()
{
	memset(_players, 0, sizeof(_players));
	for (PlayerID i = PLAYER_FIRST; i != MAX_PLAYERS; i++) {
		_players[i].index = i;
		for (uint j = 0; j < 4; j++) _players[i].share_owners[j] = PLAYER_SPECTATOR;
	}
	_cur_player_tick_index = 0;
}

void OnTick_Players()
{
	Player *p;

	if (_game_mode == GM_EDITOR) return;

	p = GetPlayer((PlayerID)_cur_player_tick_index);
	_cur_player_tick_index = (_cur_player_tick_index + 1) % MAX_PLAYERS;
	if (p->name_1 != 0) GenerateCompanyName(p);

	if (AI_AllowNewAI() && _game_mode != GM_MENU && !--_next_competitor_start)
		MaybeStartNewPlayer();
}

extern void ShowPlayerFinances(PlayerID player);

void PlayersYearlyLoop()
{
	Player *p;

	/* Copy statistics */
	FOR_ALL_PLAYERS(p) {
		if (p->is_active) {
			memmove(&p->yearly_expenses[1], &p->yearly_expenses[0], sizeof(p->yearly_expenses) - sizeof(p->yearly_expenses[0]));
			memset(&p->yearly_expenses[0], 0, sizeof(p->yearly_expenses[0]));
			InvalidateWindow(WC_FINANCES, p->index);
		}
	}

	if (_patches.show_finances && _local_player != PLAYER_SPECTATOR) {
		ShowPlayerFinances(_local_player);
		p = GetPlayer(_local_player);
		if (p->num_valid_stat_ent > 5 && p->old_economy[0].performance_history < p->old_economy[4].performance_history) {
			SndPlayFx(SND_01_BAD_YEAR);
		} else {
			SndPlayFx(SND_00_GOOD_YEAR);
		}
	}
}

static void DeletePlayerStuff(PlayerID pi)
{
	Player *p;

	DeletePlayerWindows(pi);
	p = GetPlayer(pi);
	p->name_1 = STR_NULL;
	p->president_name_1 = STR_NULL;
	free(p->name);
	free(p->president_name);
	p->name = NULL;
	p->president_name = NULL;
}

/** Change engine renewal parameters
 * @param tile unused
 * @param flags operation to perform
 * @param p1 bits 0-3 command
 * - p1 = 0 - change auto renew bool
 * - p1 = 1 - change auto renew months
 * - p1 = 2 - change auto renew money
 * - p1 = 3 - change auto renew array
 * - p1 = 4 - change bool, months & money all together
 * - p1 = 5 - change renew_keep_length
 * @param p2 value to set
 * if p1 = 0, then:
 * - p2 = enable engine renewal
 * if p1 = 1, then:
 * - p2 = months left before engine expires to replace it
 * if p1 = 2, then
 * - p2 = minimum amount of money available
 * if p1 = 3, then:
 * - p1 bits  8-15 = engine group
 * - p2 bits  0-15 = old engine type
 * - p2 bits 16-31 = new engine type
 * if p1 = 4, then:
 * - p1 bit     15 = enable engine renewal
 * - p1 bits 16-31 = months left before engine expires to replace it
 * - p2 bits  0-31 = minimum amount of money available
 * if p1 = 5, then
 * - p2 = enable renew_keep_length
 */
CommandCost CmdSetAutoReplace(TileIndex tile, uint32 flags, uint32 p1, uint32 p2)
{
	Player *p;
	if (!IsValidPlayer(_current_player)) return CMD_ERROR;

	p = GetPlayer(_current_player);
	switch (GB(p1, 0, 3)) {
		case 0:
			if (p->engine_renew == HasBit(p2, 0))
				return CMD_ERROR;

			if (flags & DC_EXEC) {
				p->engine_renew = HasBit(p2, 0);
				if (IsLocalPlayer()) {
					_patches.autorenew = p->engine_renew;
					InvalidateWindow(WC_GAME_OPTIONS, 0);
				}
			}
			break;
		case 1:
			if (Clamp((int16)p2, -12, 12) != (int16)p2) return CMD_ERROR;
			if (p->engine_renew_months == (int16)p2)
				return CMD_ERROR;

			if (flags & DC_EXEC) {
				p->engine_renew_months = (int16)p2;
				if (IsLocalPlayer()) {
					_patches.autorenew_months = p->engine_renew_months;
					InvalidateWindow(WC_GAME_OPTIONS, 0);
				}
			}
			break;
		case 2:
			if (ClampU(p2, 0, 2000000) != p2) return CMD_ERROR;
			if (p->engine_renew_money == (uint32)p2)
				return CMD_ERROR;

			if (flags & DC_EXEC) {
				p->engine_renew_money = (uint32)p2;
				if (IsLocalPlayer()) {
					_patches.autorenew_money = p->engine_renew_money;
					InvalidateWindow(WC_GAME_OPTIONS, 0);
				}
			}
			break;
		case 3: {
			EngineID old_engine_type = GB(p2, 0, 16);
			EngineID new_engine_type = GB(p2, 16, 16);
			GroupID id_g = GB(p1, 16, 16);
			CommandCost cost;

			if (!IsValidGroupID(id_g) && !IsAllGroupID(id_g) && !IsDefaultGroupID(id_g)) return CMD_ERROR;
			if (new_engine_type != INVALID_ENGINE) {
				/* First we make sure that it's a valid type the user requested
				 * check that it's an engine that is in the engine array */
				if (!IsEngineIndex(new_engine_type))
					return CMD_ERROR;

				/* check that the new vehicle type is the same as the original one */
				if (GetEngine(old_engine_type)->type != GetEngine(new_engine_type)->type)
					return CMD_ERROR;

				/* make sure that we do not replace a plane with a helicopter or vise versa */
				if (GetEngine(new_engine_type)->type == VEH_AIRCRAFT &&
						(AircraftVehInfo(old_engine_type)->subtype & AIR_CTOL) != (AircraftVehInfo(new_engine_type)->subtype & AIR_CTOL))
					return CMD_ERROR;

				/* make sure that the player can actually buy the new engine */
				if (!HasBit(GetEngine(new_engine_type)->player_avail, _current_player))
					return CMD_ERROR;

				cost = AddEngineReplacementForPlayer(p, old_engine_type, new_engine_type, id_g, flags);
			} else {
				cost = RemoveEngineReplacementForPlayer(p, old_engine_type, id_g, flags);
			}

			if (IsLocalPlayer()) InvalidateAutoreplaceWindow(old_engine_type, id_g);

			return cost;
		}

		case 4:
			if (Clamp((int16)GB(p1, 16, 16), -12, 12) != (int16)GB(p1, 16, 16)) return CMD_ERROR;
			if (ClampU(p2, 0, 2000000) != p2) return CMD_ERROR;
			if (flags & DC_EXEC) {
				p->engine_renew = HasBit(p1, 15);
				p->engine_renew_months = (int16)GB(p1, 16, 16);
				p->engine_renew_money = (uint32)p2;

				if (IsLocalPlayer()) {
					_patches.autorenew = p->engine_renew;
					_patches.autorenew_months = p->engine_renew_months;
					_patches.autorenew_money = p->engine_renew_money;
					InvalidateWindow(WC_GAME_OPTIONS, 0);
				}
			}
			break;
		case 5:
			if (p->renew_keep_length == HasBit(p2, 0))
				return CMD_ERROR;

			if (flags & DC_EXEC) {
				p->renew_keep_length = HasBit(p2, 0);
				if (IsLocalPlayer()) {
					InvalidateWindow(WC_REPLACE_VEHICLE, VEH_TRAIN);
				}
			}
		break;

	}
	return CommandCost();
}

/** Control the players: add, delete, etc.
 * @param tile unused
 * @param flags operation to perform
 * @param p1 various functionality
 * - p1 = 0 - create a new player, Which player (network) it will be is in p2
 * - p1 = 1 - create a new AI player
 * - p1 = 2 - delete a player. Player is identified by p2
 * - p1 = 3 - merge two companies together. Player to merge #1 with player #2. Identified by p2
 * @param p2 various functionality, dictated by p1
 * - p1 = 0 - ClientID of the newly created player
 * - p1 = 2 - PlayerID of the that is getting deleted
 * - p1 = 3 - #1 p2 = (bit  0-15) - player to merge (p2 & 0xFFFF)
 *          - #2 p2 = (bit 16-31) - player to be merged into ((p2>>16)&0xFFFF)
 * @todo In the case of p1=0, create new player, the clientID of the new player is in parameter
 * p2. This parameter is passed in at function DEF_SERVER_RECEIVE_COMMAND(PACKET_CLIENT_COMMAND)
 * on the server itself. First of all this is unbelievably ugly; second of all, well,
 * it IS ugly! <b>Someone fix this up :)</b> So where to fix?@n
 * @arg - network_server.c:838 DEF_SERVER_RECEIVE_COMMAND(PACKET_CLIENT_COMMAND)@n
 * @arg - network_client.c:536 DEF_CLIENT_RECEIVE_COMMAND(PACKET_SERVER_MAP) from where the map has been received
 */
CommandCost CmdPlayerCtrl(TileIndex tile, uint32 flags, uint32 p1, uint32 p2)
{
	if (flags & DC_EXEC) _current_player = OWNER_NONE;

	switch (p1) {
	case 0: { /* Create a new player */
		/* Joining Client:
		 * _local_player: PLAYER_SPECTATOR
		 * _network_playas/cid = requested company/player
		 *
		 * Other client(s)/server:
		 * _local_player/_network_playas: what they play as
		 * cid = requested company/player of joining client */
		Player *p;
#ifdef ENABLE_NETWORK
		uint16 cid = p2; // ClientID
#endif /* ENABLE_NETWORK */

		/* This command is only executed in a multiplayer game */
		if (!_networking) return CMD_ERROR;

		/* Has the network client a correct ClientID? */
		if (!(flags & DC_EXEC)) return CommandCost();
#ifdef ENABLE_NETWORK
		if (cid >= MAX_CLIENT_INFO) return CommandCost();
#endif /* ENABLE_NETWORK */

		/* Delete multiplayer progress bar */
		DeleteWindowById(WC_NETWORK_STATUS_WINDOW, 0);

		p = DoStartupNewPlayer(false);

		/* A new player could not be created, revert to being a spectator */
		if (p == NULL) {
#ifdef ENABLE_NETWORK
			if (_network_server) {
				NetworkClientInfo *ci = &_network_client_info[cid];
				ci->client_playas = PLAYER_SPECTATOR;
				NetworkUpdateClientInfo(ci->client_index);
			} else if (_local_player == PLAYER_SPECTATOR) {
				_network_playas = PLAYER_SPECTATOR;
			}
#endif /* ENABLE_NETWORK */
			break;
		}

		/* This is the joining client who wants a new company */
		if (_local_player != _network_playas && _network_playas == p->index) {
			assert(_local_player == PLAYER_SPECTATOR);
			SetLocalPlayer(p->index);
#ifdef ENABLE_NETWORK
			if (!StrEmpty(_network_default_company_pass)) {
				char *password = _network_default_company_pass;
				NetworkChangeCompanyPassword(1, &password);
			}
#endif /* ENABLE_NETWORK */

			_current_player = _local_player;

			/* Now that we have a new player, broadcast our autorenew settings to
			 * all clients so everything is in sync */
			NetworkSend_Command(0,
				(_patches_newgame.autorenew << 15 ) | (_patches_newgame.autorenew_months << 16) | 4,
				_patches_newgame.autorenew_money,
				CMD_SET_AUTOREPLACE,
				NULL
			);

			MarkWholeScreenDirty();
		}

#ifdef ENABLE_NETWORK
		if (_network_server) {
			/* XXX - UGLY! p2 (pid) is mis-used to fetch the client-id, done at
			 * server-side in network_server.c:838, function
			 * DEF_SERVER_RECEIVE_COMMAND(PACKET_CLIENT_COMMAND) */
			NetworkClientInfo *ci = &_network_client_info[cid];
			ci->client_playas = p->index;
			NetworkUpdateClientInfo(ci->client_index);

			if (IsValidPlayer(ci->client_playas)) {
				PlayerID player_backup = _local_player;
				_network_player_info[p->index].months_empty = 0;

				/* XXX - When a client joins, we automatically set its name to the
				 * player's name (for some reason). As it stands now only the server
				 * knows the client's name, so it needs to send out a "broadcast" to
				 * do this. To achieve this we send a network command. However, it
				 * uses _local_player to execute the command as.  To prevent abuse
				 * (eg. only yourself can change your name/company), we 'cheat' by
				 * impersonation _local_player as the server. Not the best solution;
				 * but it works.
				 * TODO: Perhaps this could be improved by when the client is ready
				 * with joining to let it send itself the command, and not the server?
				 * For example in network_client.c:534? */
				_cmd_text = ci->client_name;
				_local_player = ci->client_playas;
				NetworkSend_Command(0, 0, 0, CMD_CHANGE_PRESIDENT_NAME, NULL);
				_local_player = player_backup;
			}
		}
#endif /* ENABLE_NETWORK */
		break;
	}

	case 1: /* Make a new AI player */
		if (!(flags & DC_EXEC)) return CommandCost();

		DoStartupNewPlayer(true);
		break;

	case 2: { /* Delete a player */
		Player *p;

		if (!IsValidPlayer((PlayerID)p2)) return CMD_ERROR;

		if (!(flags & DC_EXEC)) return CommandCost();

		p = GetPlayer((PlayerID)p2);

		/* Only allow removal of HUMAN companies */
		if (IsHumanPlayer(p->index)) {
			/* Delete any open window of the company */
			DeletePlayerWindows(p->index);

			/* Show the bankrupt news */
			SetDParam(0, p->index);
			AddNewsItem( (StringID)(p->index | NB_BBANKRUPT), NEWS_FLAGS(NM_CALLBACK, 0, NT_COMPANY_INFO, DNC_BANKRUPCY),0,0);

			/* Remove the company */
			ChangeOwnershipOfPlayerItems(p->index, PLAYER_SPECTATOR);
			p->is_active = false;
		}
		break;
	}

	case 3: { /* Merge a company (#1) into another company (#2), elimination company #1 */
		PlayerID pid_old = (PlayerID)GB(p2,  0, 16);
		PlayerID pid_new = (PlayerID)GB(p2, 16, 16);

		if (!IsValidPlayer(pid_old) || !IsValidPlayer(pid_new)) return CMD_ERROR;

		if (!(flags & DC_EXEC)) return CMD_ERROR;

		ChangeOwnershipOfPlayerItems(pid_old, pid_new);
		DeletePlayerStuff(pid_old);
		break;
	}

	default: return CMD_ERROR;
	}

	return CommandCost();
}

static const StringID _endgame_perf_titles[] = {
	STR_0213_BUSINESSMAN,
	STR_0213_BUSINESSMAN,
	STR_0213_BUSINESSMAN,
	STR_0213_BUSINESSMAN,
	STR_0213_BUSINESSMAN,
	STR_0214_ENTREPRENEUR,
	STR_0214_ENTREPRENEUR,
	STR_0215_INDUSTRIALIST,
	STR_0215_INDUSTRIALIST,
	STR_0216_CAPITALIST,
	STR_0216_CAPITALIST,
	STR_0217_MAGNATE,
	STR_0217_MAGNATE,
	STR_0218_MOGUL,
	STR_0218_MOGUL,
	STR_0219_TYCOON_OF_THE_CENTURY
};

StringID EndGameGetPerformanceTitleFromValue(uint value)
{
	value = minu(value / 64, lengthof(_endgame_perf_titles) - 1);

	return _endgame_perf_titles[value];
}

/** Return true if any cheat has been used, false otherwise */
static bool CheatHasBeenUsed()
{
	const Cheat* cht = (Cheat*)&_cheats;
	const Cheat* cht_last = &cht[sizeof(_cheats) / sizeof(Cheat)];

	for (; cht != cht_last; cht++) {
		if (cht->been_used) return true;
	}

	return false;
}

/** Save the highscore for the player */
int8 SaveHighScoreValue(const Player *p)
{
	HighScore *hs = _highscore_table[_opt.diff_level];
	uint i;
	uint16 score = p->old_economy[0].performance_history;

	/* Exclude cheaters from the honour of being in the highscore table */
	if (CheatHasBeenUsed()) return -1;

	for (i = 0; i < lengthof(_highscore_table[0]); i++) {
		/* You are in the TOP5. Move all values one down and save us there */
		if (hs[i].score <= score) {
			/* move all elements one down starting from the replaced one */
			memmove(&hs[i + 1], &hs[i], sizeof(HighScore) * (lengthof(_highscore_table[0]) - i - 1));
			SetDParam(0, p->index);
			SetDParam(1, p->index);
			GetString(hs[i].company, STR_HIGHSCORE_NAME, lastof(hs[i].company)); // get manager/company name string
			hs[i].score = score;
			hs[i].title = EndGameGetPerformanceTitleFromValue(score);
			return i;
		}
	}

	return -1; // too bad; we did not make it into the top5
}

/** Sort all players given their performance */
static int CDECL HighScoreSorter(const void *a, const void *b)
{
	const Player *pa = *(const Player* const*)a;
	const Player *pb = *(const Player* const*)b;

	return pb->old_economy[0].performance_history - pa->old_economy[0].performance_history;
}

/* Save the highscores in a network game when it has ended */
#define LAST_HS_ITEM lengthof(_highscore_table) - 1
int8 SaveHighScoreValueNetwork()
{
	const Player* p;
	const Player* pl[MAX_PLAYERS];
	size_t count = 0;
	int8 player = -1;

	/* Sort all active players with the highest score first */
	FOR_ALL_PLAYERS(p) if (p->is_active) pl[count++] = p;
	qsort((Player*)pl, count, sizeof(pl[0]), HighScoreSorter);

	{
		uint i;

		memset(_highscore_table[LAST_HS_ITEM], 0, sizeof(_highscore_table[0]));

		/* Copy over Top5 companies */
		for (i = 0; i < lengthof(_highscore_table[LAST_HS_ITEM]) && i < count; i++) {
			HighScore* hs = &_highscore_table[LAST_HS_ITEM][i];

			SetDParam(0, pl[i]->index);
			SetDParam(1, pl[i]->index);
			GetString(hs->company, STR_HIGHSCORE_NAME, lastof(hs->company)); // get manager/company name string
			hs->score = pl[i]->old_economy[0].performance_history;
			hs->title = EndGameGetPerformanceTitleFromValue(hs->score);

			/* get the ranking of the local player */
			if (pl[i]->index == _local_player) player = i;
		}
	}

	/* Add top5 players to highscore table */
	return player;
}

/** Save HighScore table to file */
void SaveToHighScore()
{
	FILE *fp = fopen(_highscore_file, "wb");

	if (fp != NULL) {
		uint i;
		HighScore *hs;

		for (i = 0; i < LAST_HS_ITEM; i++) { // don't save network highscores
			for (hs = _highscore_table[i]; hs != endof(_highscore_table[i]); hs++) {
				/* First character is a command character, so strlen will fail on that */
				byte length = min(sizeof(hs->company), (hs->company[0] == '\0') ? 0 : (int)strlen(&hs->company[1]) + 1);

				fwrite(&length, sizeof(length), 1, fp); // write away string length
				fwrite(hs->company, length, 1, fp);
				fwrite(&hs->score, sizeof(hs->score), 1, fp);
				fwrite("", 2, 1, fp); // XXX - placeholder for hs->title, not saved anymore; compatibility
			}
		}
		fclose(fp);
	}
}

/** Initialize the highscore table to 0 and if any file exists, load in values */
void LoadFromHighScore()
{
	FILE *fp = fopen(_highscore_file, "rb");

	memset(_highscore_table, 0, sizeof(_highscore_table));

	if (fp != NULL) {
		uint i;
		HighScore *hs;

		for (i = 0; i < LAST_HS_ITEM; i++) { // don't load network highscores
			for (hs = _highscore_table[i]; hs != endof(_highscore_table[i]); hs++) {
				byte length;
				fread(&length, sizeof(length), 1, fp);

				fread(hs->company, 1, length, fp);
				fread(&hs->score, sizeof(hs->score), 1, fp);
				fseek(fp, 2, SEEK_CUR); // XXX - placeholder for hs->title, not saved anymore; compatibility
				hs->title = EndGameGetPerformanceTitleFromValue(hs->score);
			}
		}
		fclose(fp);
	}

	/* Initialize end of game variable (when to show highscore chart) */
	_patches.ending_year = 2051;
}

/* Save/load of players */
static const SaveLoad _player_desc[] = {
	    SLE_VAR(Player, name_2,          SLE_UINT32),
	    SLE_VAR(Player, name_1,          SLE_STRINGID),
	SLE_CONDSTR(Player, name,            SLE_STR, 0,                       84, SL_MAX_VERSION),

	    SLE_VAR(Player, president_name_1, SLE_UINT16),
	    SLE_VAR(Player, president_name_2, SLE_UINT32),
	SLE_CONDSTR(Player, president_name,  SLE_STR, 0,                       84, SL_MAX_VERSION),

	    SLE_VAR(Player, face,            SLE_UINT32),

	/* money was changed to a 64 bit field in savegame version 1. */
	SLE_CONDVAR(Player, player_money,          SLE_VAR_I64 | SLE_FILE_I32,  0, 0),
	SLE_CONDVAR(Player, player_money,          SLE_INT64,                   1, SL_MAX_VERSION),

	SLE_CONDVAR(Player, current_loan,          SLE_VAR_I64 | SLE_FILE_I32,  0, 64),
	SLE_CONDVAR(Player, current_loan,          SLE_INT64,                  65, SL_MAX_VERSION),

	    SLE_VAR(Player, player_color,          SLE_UINT8),
	    SLE_VAR(Player, player_money_fraction, SLE_UINT8),
	SLE_CONDVAR(Player, avail_railtypes,       SLE_UINT8,                   0, 57),
	    SLE_VAR(Player, block_preview,         SLE_UINT8),

	    SLE_VAR(Player, cargo_types,           SLE_UINT16),
	SLE_CONDVAR(Player, location_of_house,     SLE_FILE_U16 | SLE_VAR_U32,  0,  5),
	SLE_CONDVAR(Player, location_of_house,     SLE_UINT32,                  6, SL_MAX_VERSION),
	SLE_CONDVAR(Player, last_build_coordinate, SLE_FILE_U16 | SLE_VAR_U32,  0,  5),
	SLE_CONDVAR(Player, last_build_coordinate, SLE_UINT32,                  6, SL_MAX_VERSION),
	SLE_CONDVAR(Player, inaugurated_year,      SLE_FILE_U8  | SLE_VAR_I32,  0, 30),
	SLE_CONDVAR(Player, inaugurated_year,      SLE_INT32,                  31, SL_MAX_VERSION),

	    SLE_ARR(Player, share_owners,          SLE_UINT8, 4),

	    SLE_VAR(Player, num_valid_stat_ent,    SLE_UINT8),

	    SLE_VAR(Player, quarters_of_bankrupcy, SLE_UINT8),
	    SLE_VAR(Player, bankrupt_asked,        SLE_UINT8),
	    SLE_VAR(Player, bankrupt_timeout,      SLE_INT16),
	SLE_CONDVAR(Player, bankrupt_value,        SLE_VAR_I64 | SLE_FILE_I32,  0, 64),
	SLE_CONDVAR(Player, bankrupt_value,        SLE_INT64,                  65, SL_MAX_VERSION),

	/* yearly expenses was changed to 64-bit in savegame version 2. */
	SLE_CONDARR(Player, yearly_expenses,       SLE_FILE_I32 | SLE_VAR_I64, 3 * 13, 0, 1),
	SLE_CONDARR(Player, yearly_expenses,       SLE_INT64, 3 * 13,                  2, SL_MAX_VERSION),

	SLE_CONDVAR(Player, is_ai,                 SLE_BOOL, 2, SL_MAX_VERSION),
	SLE_CONDVAR(Player, is_active,             SLE_BOOL, 4, SL_MAX_VERSION),

	/* Engine renewal settings */
	SLE_CONDNULL(512, 16, 18),
	SLE_CONDREF(Player, engine_renew_list,     REF_ENGINE_RENEWS,          19, SL_MAX_VERSION),
	SLE_CONDVAR(Player, engine_renew,          SLE_BOOL,                   16, SL_MAX_VERSION),
	SLE_CONDVAR(Player, engine_renew_months,   SLE_INT16,                  16, SL_MAX_VERSION),
	SLE_CONDVAR(Player, engine_renew_money,    SLE_UINT32,                 16, SL_MAX_VERSION),
	SLE_CONDVAR(Player, renew_keep_length,     SLE_BOOL,                    2, SL_MAX_VERSION), // added with 16.1, but was blank since 2

	/* reserve extra space in savegame here. (currently 63 bytes) */
	SLE_CONDNULL(63, 2, SL_MAX_VERSION),

	SLE_END()
};

static const SaveLoad _player_economy_desc[] = {
	/* these were changed to 64-bit in savegame format 2 */
	SLE_CONDVAR(PlayerEconomyEntry, income,              SLE_FILE_I32 | SLE_VAR_I64, 0, 1),
	SLE_CONDVAR(PlayerEconomyEntry, income,              SLE_INT64,                  2, SL_MAX_VERSION),
	SLE_CONDVAR(PlayerEconomyEntry, expenses,            SLE_FILE_I32 | SLE_VAR_I64, 0, 1),
	SLE_CONDVAR(PlayerEconomyEntry, expenses,            SLE_INT64,                  2, SL_MAX_VERSION),
	SLE_CONDVAR(PlayerEconomyEntry, company_value,       SLE_FILE_I32 | SLE_VAR_I64, 0, 1),
	SLE_CONDVAR(PlayerEconomyEntry, company_value,       SLE_INT64,                  2, SL_MAX_VERSION),

	    SLE_VAR(PlayerEconomyEntry, delivered_cargo,     SLE_INT32),
	    SLE_VAR(PlayerEconomyEntry, performance_history, SLE_INT32),

	SLE_END()
};

static const SaveLoad _player_livery_desc[] = {
	SLE_CONDVAR(Livery, in_use,  SLE_BOOL,  34, SL_MAX_VERSION),
	SLE_CONDVAR(Livery, colour1, SLE_UINT8, 34, SL_MAX_VERSION),
	SLE_CONDVAR(Livery, colour2, SLE_UINT8, 34, SL_MAX_VERSION),
	SLE_END()
};

static void SaveLoad_PLYR(Player* p)
{
	int i;

	SlObject(p, _player_desc);

	/* Write AI? */
	if (!IsHumanPlayer(p->index)) {
		SaveLoad_AI(p->index);
	}

	/* Write economy */
	SlObject(&p->cur_economy, _player_economy_desc);

	/* Write old economy entries. */
	for (i = 0; i < p->num_valid_stat_ent; i++) {
		SlObject(&p->old_economy[i], _player_economy_desc);
	}

	/* Write each livery entry. */
	int num_liveries = CheckSavegameVersion(63) ? LS_END - 4 : (CheckSavegameVersion(85) ? LS_END - 2: LS_END);
	for (i = 0; i < num_liveries; i++) {
		SlObject(&p->livery[i], _player_livery_desc);
	}

	if (num_liveries < LS_END) {
		/* We want to insert some liveries somewhere in between. This means some have to be moved. */
		memmove(&p->livery[LS_FREIGHT_WAGON], &p->livery[LS_PASSENGER_WAGON_MONORAIL], (LS_END - LS_FREIGHT_WAGON) * sizeof(p->livery[0]));
		p->livery[LS_PASSENGER_WAGON_MONORAIL] = p->livery[LS_MONORAIL];
		p->livery[LS_PASSENGER_WAGON_MAGLEV]   = p->livery[LS_MAGLEV];
	}

	if (num_liveries == LS_END - 4) {
		/* Copy bus/truck liveries over to trams */
		p->livery[LS_PASSENGER_TRAM] = p->livery[LS_BUS];
		p->livery[LS_FREIGHT_TRAM]   = p->livery[LS_TRUCK];
	}
}

static void Save_PLYR()
{
	Player *p;
	FOR_ALL_PLAYERS(p) {
		if (p->is_active) {
			SlSetArrayIndex(p->index);
			SlAutolength((AutolengthProc*)SaveLoad_PLYR, p);
		}
	}
}

static void Load_PLYR()
{
	int index;
	while ((index = SlIterateArray()) != -1) {
		Player *p = GetPlayer((PlayerID)index);
		SaveLoad_PLYR(p);
		_player_colors[index] = p->player_color;

		/* This is needed so an AI is attached to a loaded AI */
		if (p->is_ai && (!_networking || _network_server) && _ai.enabled) {
			/* Clear the memory of the new AI, otherwise we might be doing wrong things. */
			memset(&_players_ainew[index], 0, sizeof(PlayerAiNew));
			AI_StartNewAI(p->index);
		}
	}
}

extern const ChunkHandler _player_chunk_handlers[] = {
	{ 'PLYR', Save_PLYR, Load_PLYR, CH_ARRAY | CH_LAST},
};
