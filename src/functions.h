/* $Id$ */

/** @file functions.h */

#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include "core/random_func.hpp"
#include "command_type.h"
#include "tile_cmd.h"

/* clear_land.cpp */
void DrawHillyLandTile(const TileInfo *ti);
void DrawClearLandTile(const TileInfo *ti, byte set);
void DrawClearLandFence(const TileInfo *ti);
void TileLoopClearHelper(TileIndex tile);

/* players.cpp */
bool CheckPlayerHasMoney(CommandCost cost);
void SubtractMoneyFromPlayer(CommandCost cost);
void SubtractMoneyFromPlayerFract(PlayerID player, CommandCost cost);
bool CheckOwnership(Owner owner);
bool CheckTileOwnership(TileIndex tile);

/* texteff.cpp */
void AddAnimatedTile(TileIndex tile);
void DeleteAnimatedTile(TileIndex tile);
void AnimateAnimatedTiles();
void InitializeAnimatedTiles();

/* misc_cmd.cpp */
void PlaceTreesRandomly();

void InitializeLandscapeVariables(bool only_constants);

/* misc.cpp */
bool IsCustomName(StringID id);
char *CopyFromOldName(StringID id);

/* misc functions */
/**
 * Mark a tile given by its coordinate dirty for repaint.
 *
 * @ingroup dirty
 */
void MarkTileDirty(int x, int y);

/**
 * Mark a tile given by its index dirty for repaint.
 *
 * @ingroup dirty
 */
void MarkTileDirtyByTile(TileIndex tile);

/**
 * Mark all viewports dirty for repaint.
 *
 * @ingroup dirty
 */
void MarkAllViewportsDirty(int left, int top, int right, int bottom);
void ShowCostOrIncomeAnimation(int x, int y, int z, Money cost);
void ShowFeederIncomeAnimation(int x, int y, int z, Money cost);

void ShowHighscoreTable(int difficulty, int8 rank);

void AskExitGame();
void AskExitToGameMenu();

void RedrawAutosave();

StringID RemapOldStringID(StringID s);

void UpdateViewportSignPos(ViewportSign *sign, int left, int top, StringID str);


/* callback from drivers that is called if the game size changes dynamically */
void GameSizeChanged();
const char *GetCurrentLocale(const char *param);

int ttd_main(int argc, char* argv[]);
void HandleExitGameRequest();

#endif /* FUNCTIONS_H */
