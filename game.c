#include <sega_scl.h>

#include "bg.h"
#include "cd.h"
#include "game.h"
#include "gravity.h"
#include "piece.h"
#include "print.h"
#include "rank.h"
#include "release.h"
#include "rng.h"
#include "scroll.h"
#include "sprite.h"
#include "sound.h"
#include "vblank.h"

typedef enum {
    STATE_NORMAL,
    STATE_LINE,
    STATE_ARE,
    STATE_GAMEOVER,
    STATE_GAMEOVER_DONE,
    STATE_PAUSED,
} GAME_STATE;

static int borderBase;
#define BORDER_WIDTH (13)
#define BORDER_HEIGHT (22)
#define NEXT_TILE (borderBase + 286) 
#define SCORE_TILE (borderBase + 290)
#define LEVEL_TILE (borderBase + 294)
#define RANKING_TILE (borderBase + 299)
#define DIGITS_TILE (borderBase + 312)
#define BLACK_TILE (borderBase + 322)

static int gameState;
static int prevState; // used to keep track of state when the game is paused
static int gameTimer;

#define ARE_FRAMES (30)
#define LINE_FRAMES (41)

static int blockStart;
static SPRITE_INFO blockSpr;

// ranking icon
#define RANKING_X (22)
#define RANKING_Y (4)
static int ranking;
static int iconStart;
static SPRITE_INFO iconSpr;

#define GAME_ROWS (20)
#define GAME_COLS (10)
static int gameBoard[GAME_ROWS][GAME_COLS];
static int clearedLines[GAME_ROWS];

#define ROW_OFFSET (64)
#define TILE_SIZE (8)
#define BOARD_X (10)
#define BOARD_Y (5)
volatile Uint16 *boardVram;

#define SPAWN_X (3)
#define SPAWN_Y (-1)
static PIECE currPiece;

#define PREVIEW_X (3)
#define PREVIEW_Y (-4)
static PIECE nextPiece;

#define SCORE_X (22)
#define SCORE_Y (14)
static int score;
// these two are used to calculate the score
static int drop; 
static int combo;

#define LEVEL_X (SCORE_X)
#define LEVEL_Y (SCORE_Y + 4)
static int level;
static int levelCursor;
// the level where stuff starts moving fast
#define FAST_LEVEL (800)

#define MOVE_FRAMES (14)
#define DAS_FRAMES (2)
static int leftTimer;
static int rightTimer;

#define DOWN_FRAMES (3)
#define LOCK_FRAMES (30)
#define FAST_LOCK_FRAMES (22)
static int downTimer;
static int lockTimer;

static int gravityTimer;

#define GAME_OVER_FRAMES (5)
static int gameOverRow;

#define ROTATE_CLOCKWISE (-1)
#define ROTATE_COUNTERCLOCKWISE (1)

#define GAME_TRACK (3)
#define MUSIC_VOLUME (6)
int song;

// initializes a new piece
static void Game_MakePiece(PIECE *gamePiece, PIECE *previewPiece) {
    gamePiece->num = previewPiece->num;
    gamePiece->rotation = 0;
    gamePiece->x = SPAWN_X;
    gamePiece->y = SPAWN_Y;

    previewPiece->num = RNG_Get();
    previewPiece->rotation = 0;
    previewPiece->x = PREVIEW_X;
    previewPiece->y = PREVIEW_Y;
    
    Sound_Play(previewPiece->num);
}

void Game_Init() {
    // clear out previous scroll data
    for (int i = 0; i < 0x40000; i++) {
        ((volatile Uint8 *)SCL_VDP2_VRAM)[i] = 0;
    }

    // setup background
    BG_Init();

    // load assets
    Uint8 *gameBuf = (Uint8 *)LWRAM;
    CD_ChangeDir("GAME");
    
    blockStart = Sprite_Load("BLOCKS.SPR", NULL); // sprites for active blocks
    iconStart = Sprite_Load("ICONS.SPR", NULL);
    boardVram = (volatile Uint16 *)MAP_PTR(0) + (BOARD_Y * ROW_OFFSET) + BOARD_X;
    
    // load piece tiles
    CD_Load("PLACED.TLE", gameBuf);
    int blockBytes = Scroll_LoadTile(gameBuf, (volatile void *)SCL_VDP2_VRAM_A1, SCL_NBG0, 0);
    borderBase = blockBytes / 64;

    // load border tiles
    CD_Load("BORDER.TLE", gameBuf);
    Scroll_LoadTile(gameBuf, (volatile void *)(SCL_VDP2_VRAM_A1 + blockBytes), SCL_NBG1, 0);
    int counter = borderBase;
    for (int y = 0; y < BORDER_HEIGHT; y++) {
        for (int x = 0; x < BORDER_WIDTH; x++) {
            ((volatile Uint16 *)MAP_PTR(1))[(y + BOARD_Y - 1) * ROW_OFFSET + (x + BOARD_X - 2)] = (counter * 2);
            counter++;
        }
    }
    
    // load playfield background
    for (int y = 0; y < GAME_ROWS; y++) {
        for (int x = 0; x < GAME_COLS; x++) {
            ((volatile Uint16 *)MAP_PTR(2))[(y + BOARD_Y) * ROW_OFFSET + (x + BOARD_X)] = BLACK_TILE * 2;
        }
    }
    // set transparent
    SCL_SetColMixRate(SCL_NBG2, 20);

    // load next text
    for (int i = 0; i < 4; i++) { 
        ((volatile Uint16 *)MAP_PTR(1))[(BOARD_Y + PREVIEW_Y + 2) * ROW_OFFSET 
            + BOARD_X + PREVIEW_X - 4 + i] = (NEXT_TILE + i) * 2; 
    }

    // setup ranking
    for (int i = 0; i < 5; i++) {
        ((volatile Uint16 *)MAP_PTR(1))[RANKING_Y * ROW_OFFSET + RANKING_X + i] = (RANKING_TILE + i) * 2;
    }
    ranking = 0;

    // setup score
    for (int i = 0; i < 4; i++) { 
        ((volatile Uint16 *)MAP_PTR(1))[SCORE_Y * ROW_OFFSET + SCORE_X + i] = (SCORE_TILE + i) * 2; 
    }
    score = 0;
    combo = 1;

    // setup level
    for (int i = 0; i < 4; i++) { 
        ((volatile Uint16 *)MAP_PTR(1))[LEVEL_Y * ROW_OFFSET + LEVEL_X + i] = (LEVEL_TILE + i) * 2; 
    }
    level = 0;
    levelCursor = 0;
   
    CD_ChangeDir("..");

    // initialize the RNG
    RNG_Init();

    // initialize the board
    for (int y = 0; y < GAME_ROWS; y++) {
        for (int x = 0; x < GAME_COLS; x++) {
            gameBoard[y][x] = 0;
        }
    }
    
    // set up game state
    gameState = STATE_NORMAL;
    prevState = STATE_NORMAL;

    // initialize movement timers
    leftTimer = MOVE_FRAMES;
    rightTimer = MOVE_FRAMES;
    downTimer = DOWN_FRAMES;
    lockTimer = -1;
    gravityTimer = 0;

    // set the first piece
    nextPiece.num = RNG_Get();
    Game_MakePiece(&currPiece, &nextPiece);
    
    Sound_CDVolume(MUSIC_VOLUME, MUSIC_VOLUME);
    Sound_CDDA(GAME_TRACK, 1);
    song = 0;
}

// draws a piece
static void Game_DrawPiece(PIECE *piece) {
    int tileNo;

    for (int y = 0; y < PIECE_SIZE; y++) {
        for (int x = 0; x < PIECE_SIZE; x++) {
            tileNo = pieces[piece->num][piece->rotation][y][x];
            if (tileNo != 0) {
                // subtract 1 from the sprite number because the piece arrays have the first
                // block sprite as 1 and 0 as "nothing"
                Sprite_Make(blockStart + tileNo - 1, MTH_IntToFixed((BOARD_X + piece->x + x) * TILE_SIZE),
                        MTH_IntToFixed((BOARD_Y + piece->y + y) * TILE_SIZE), &blockSpr);
                Sprite_Draw(&blockSpr);
            }
        }
    }
}

static void Game_DrawRanking(int num) {
    Sprite_Make(iconStart + num, MTH_FIXED((RANKING_X - 1) * 8), MTH_FIXED((RANKING_Y + 1) * 8), &iconSpr);
    Sprite_Draw(&iconSpr);
}

// draws the score and level
static void Game_DrawNums() {
    int tmp = score;
    volatile Uint16 *scorePtr = (volatile Uint16 *)MAP_PTR(1) + ((SCORE_Y + 1) * ROW_OFFSET) + SCORE_X;
    for (int i = 0; i < 6; i++) {
        scorePtr[5 - i] = (DIGITS_TILE + (tmp % 10)) * 2;
        scorePtr[ROW_OFFSET + (5 - i)] = (DIGITS_TILE + (tmp % 10) + BORDER_WIDTH) * 2;
        tmp /= 10;
    }

    tmp = level;
    volatile Uint16 *levelPtr = (volatile Uint16 *)MAP_PTR(1) + ((LEVEL_Y + 1) * ROW_OFFSET) + LEVEL_X;
    for (int i = 0; i < 3; i++) {
        levelPtr[2 - i] = (DIGITS_TILE + (tmp % 10)) * 2;
        levelPtr[ROW_OFFSET + (2 - i)] = (DIGITS_TILE + (tmp % 10) + BORDER_WIDTH) * 2;
        tmp /= 10;
    }
}

// copies a piece to the board
static void Game_CopyPiece(PIECE *piece) {
    int tile;

    for (int y = 0; y < PIECE_SIZE; y++) {
        for (int x = 0; x < PIECE_SIZE; x++) {
            tile = pieces[piece->num][piece->rotation][y][x];
            if (((piece->x + x) >= 0) && ((piece->y + y) >= 0) && (tile != 0)) {
                gameBoard[piece->y + y][piece->x + x] = tile;
            }
        }
    }
}

static inline int Game_BoardGet(int x, int y) {
    if ((y < 0) || (y >= GAME_ROWS)) {
        return 1;
    }

    if ((x < 0) || (x >= GAME_COLS)) {
        return 1;
    }

    return gameBoard[y][x];
}

// returns 1 if a piece can fit on the board
static int Game_CheckPiece(PIECE *piece) {
    for (int y = 0; y < PIECE_SIZE; y++) {
        for (int x = 0; x < PIECE_SIZE; x++) {
            if (Game_BoardGet(piece->x + x, piece->y + y) &&
                    pieces[piece->num][piece->rotation][y][x]) {
                return 0;
            }
        }
    }

    return 1;
}

// checks the center column rule for kicks
static int Game_CanKick(PIECE *piece) {
    if ((piece->num == PIECE_L) || (piece->num == PIECE_J) || (piece->num == PIECE_T)) {
        for (int y = 0; y < PIECE_SIZE; y++) {
            for (int x = 0; x < PIECE_SIZE; x++) {
                if (Game_BoardGet(piece->x + x, piece->y + y) &&
                        pieces[piece->num][piece->rotation][y][x]) {
                    if (x == 1) {
                        return 0;
                    }
                    else {
                        return 1;
                    }
                }
            }
        }
    }

    return 1;
}

static int Game_CanMoveLeft() {
    if (PadData1E & PAD_L) {
        leftTimer = MOVE_FRAMES;
        return 1;
    }

    else if (PadData1 & PAD_L) {
        if (leftTimer == 0) {
            leftTimer = DAS_FRAMES;
            return 1;
        }
        else {
            if (level >= FAST_LEVEL) {
                leftTimer -= 2;
            }
            else {
                leftTimer--;
            }
        }
    }
    return 0;
}

static int Game_CanMoveRight() {
    if (PadData1E & PAD_R) {
        rightTimer = MOVE_FRAMES;
        return 1;
    }

    else if (PadData1 & PAD_R) {
        if (rightTimer == 0) {
            rightTimer = DAS_FRAMES;
            return 1;
        }
        else {
            if (level >= FAST_LEVEL) {
                rightTimer -= 2;
            }
            else {
                rightTimer--;
            }
        }
    }
    return 0;
}

// returns 1 if the piece is on ground or another piece
static int Game_CheckBelow(PIECE *piece) {
    for (int y = 0; y < PIECE_SIZE; y++) {
        for (int x = 0; x < PIECE_SIZE; x++) {
            if (pieces[piece->num][piece->rotation][y][x] &&
                    Game_BoardGet(piece->x + x, piece->y + y + 1)) {
                return 1;
            }
        }
    }
    return 0;
}

static int Game_CanMoveDown() {
    if (PadData1E & (PAD_D | PAD_A)) {
        downTimer = DOWN_FRAMES;
        return 1;
    }

    else if (PadData1 & (PAD_D | PAD_A)) {
        if (downTimer == 0) {
            downTimer = DOWN_FRAMES;
            return 1;
        }
        else {
            downTimer--;
        }
    }

    gravityTimer--;
    return 0;
}

static int Game_Drop(PIECE *piece) {
    if (!Game_CheckBelow(piece)) {
        piece->y++;
        drop++;
        return 1;
    }
    return 0;
}

static int Game_Rotate(PIECE *piece, int rotation) {
    int originalX = piece->x;
    int originalY = piece->y;
    Uint8 originalRotation = piece->rotation;
    piece->rotation += rotation;
    piece->rotation %= PIECE_ROTATIONS;
    if (Game_CheckPiece(piece)) {
        return 1;
    }

    // ceiling kicks
    if ((piece->y == SPAWN_Y) && !Game_CheckBelow(piece)) {
        piece->y++;
        if (Game_CheckPiece(piece)) {
            return 1;
        }
    } 

    if (Game_CanKick(piece)) {
        // try going to the left
        piece->x++;
        if (Game_CheckPiece(piece)) {
            return 1;
        }

        // try going to the left
        piece->x -= 2;
        if (Game_CheckPiece(piece)) {
            return 1;
        }
    }
    
    // move back
    piece->x = originalX;
    piece->y = originalY;
    piece->rotation = originalRotation;
    return 0;
}

// buffer in next rotate before piece spawns
static void Game_BufferRotate() {
    if (PadData1 & PAD_C) {
        Game_Rotate(&currPiece, ROTATE_CLOCKWISE);
        Sound_Play(SOUND_ROTATE);
    }

    if (PadData1 & PAD_B) {
        Game_Rotate(&currPiece, ROTATE_COUNTERCLOCKWISE);
        Sound_Play(SOUND_ROTATE);
    }
}

static inline void Game_CopyRow(int dst, int src) {
    for (int i = 0; i < GAME_COLS; i++) {
        if (src >= 0) {
            gameBoard[dst][i] = gameBoard[src][i];
        }
        else {
            gameBoard[dst][i] = 0;
        }
    }
}

static inline void Game_MoveDown(int row) {
    for (int i = row; i >= 0; i--) {
        Game_CopyRow(i, i - 1);
    } 
}

// returns number of filled lines.
static int Game_CheckLines() {
    int full;
    int lines = 0;

    // clear the "cleared lines" array
    for (int i = 0; i < GAME_ROWS; i++) {
        clearedLines[i] = 0;
    }

    // check and mark all filled lines
    for (int y = 0; y < GAME_ROWS; y++) {
        full = 1;
        for (int x = 0; x < GAME_COLS; x++) {
            if (gameBoard[y][x] == 0) {
                full = 0;
                break;
            }
        }

        if (full) {
            for (int x = 0; x < GAME_COLS; x++) {
                gameBoard[y][x] = 0;
            }
            clearedLines[y] = 1;
            lines++;
        }
    }
    return lines;
}

static int Game_Normal() {
    int oldLevel;
    int lines;
    int lockSound = 1;

    if (DEBUG && (PadData1E & PAD_Z)) {
        for (int y = 0; y < GAME_ROWS; y++) {
            for (int x = 0; x < GAME_COLS; x++) {
                gameBoard[y][x] = 0;
            }
        }
    }
    
    // clockwise rotation
    if (PadData1E & PAD_C) {
        Game_Rotate(&currPiece, ROTATE_CLOCKWISE);
    }

    // counterclockwise rotation
    if (PadData1E & PAD_B) {
        Game_Rotate(&currPiece, ROTATE_COUNTERCLOCKWISE);
    }
    

    if ((lockTimer == -1) && Game_CheckBelow(&currPiece)) {
        if (level >= FAST_LEVEL) {
            lockTimer = FAST_LOCK_FRAMES;
        }
        else {
            lockTimer = LOCK_FRAMES;
        }
        Sound_Play(SOUND_LAND);

        // don't play lock sound & lock immediately if player's holding down
        // when piece lands (aka soft drop)
        if (PadData1 & (PAD_D | PAD_A)) {
            lockSound = 0;
            lockTimer = 0;
        }
    }
    else if (!Game_CheckBelow(&currPiece)) {
        lockTimer = -1;
    }

    // hard drop
    if ((PadData1 & PAD_U) && (lockTimer == -1)) {
        while (Game_Drop(&currPiece));
        if (level >= FAST_LEVEL) {
            lockTimer = FAST_LOCK_FRAMES;
        }
        else {
            lockTimer = LOCK_FRAMES;
        }
        Sound_Play(SOUND_LAND);
    }

    // gravity
    while (level >= levels[levelCursor + 1]) {
        levelCursor++;
    }
    gravityTimer += gravity[levelCursor];
    Print_Num(gravityTimer, 0, 0);
    Print_Num(levelCursor, 1, 0);
    Print_Num(gravity[levelCursor], 2, 0);
    while (gravityTimer >> 8) {
        Game_Drop(&currPiece);
        gravityTimer -= 256;
    }

    // soft drop
    if (Game_CanMoveDown()) {
        Game_Drop(&currPiece);
    }

    // allow player to interrupt lock timer if we're on the ground
    if ((lockTimer > 0) && (PadData1 & (PAD_D | PAD_A))) {
        lockTimer = 0;
    }

    if (lockTimer == 0) {
        lockTimer = -1;
        drop = 0;
        Game_CopyPiece(&currPiece);
        
        lines = Game_CheckLines();
        oldLevel = level;
        if (lines) {
            gameState = STATE_LINE;
            gameTimer = LINE_FRAMES;
            Sound_Play(SOUND_CLEAR);
            combo = combo + (lines * 2) - 2;
            level += lines;
            score += ((((level + lines) / 4) + 1) + drop) * lines * combo;
            while (score >= ranks[ranking + 1]) {
                ranking++;
            }
        }
        else {
            combo = 1;
            gameState = STATE_ARE;
            gameTimer = ARE_FRAMES;
        }

        if (DEBUG && (PadData1 & PAD_Y)) {
            level += 50;
        }

        // bg changing

        // change every 100 levels before 600
        if ((level < 600) && ((level / 100) > (oldLevel / 100))) {
            BG_Next();
        }
        // change at 800
        else if ((level >= 800) && (level < 900) && ((level / 100) > (oldLevel / 100))) {
            BG_Next();
        }

        if (level > 999) {
            Rank_Setup(9);
            gameState = STATE_GAMEOVER_DONE;
        }

        // cut volume before a song change for dramatic effect
        if ((songs[song + 1] - level) <= 10) {
            Sound_CDVolume(0, 0);
        }

        // if we've gotten to a song change, switch to the next song
        if ((oldLevel < songs[song + 1]) && (level >= songs[song + 1])) {
            song++;
            Sound_CDDA(GAME_TRACK + song, 1);
            Sound_CDVolume(MUSIC_VOLUME, MUSIC_VOLUME);
        }
        
        if (lockSound) {
            Sound_Play(SOUND_LOCK);
        }
    }
    else if (lockTimer > 0) {
        lockTimer--;
    }

    // horizontal movement
    if (Game_CanMoveLeft()) {
        currPiece.x--;
        if (!Game_CheckPiece(&currPiece)) {
            currPiece.x++;
        }
    }

    if (Game_CanMoveRight()) {
        currPiece.x++;
        if (!Game_CheckPiece(&currPiece)) {
            currPiece.x--;
        }
    }

    // don't draw the piece if we're replacing it with another one
    if (gameState == STATE_NORMAL) {
        Game_DrawPiece(&currPiece);
    }

    return 0;
}

static void Game_Line() {
    if (gameTimer > 0) {
        if (level >= 800) {
            gameTimer -= 2;
        }
        else {
            gameTimer--;
        }
    }
    else {
        // delete all cleared rows
        for (int i = 0; i < GAME_ROWS; i++) {
            if (clearedLines[i]) {
                Game_MoveDown(i);
            }
        }
        Sound_Play(SOUND_FALL);
        gameTimer = ARE_FRAMES;
        gameState = STATE_ARE;
    }
}

static void Game_Are() {
    if (gameTimer > 0) {
        if (level >= 800) {
            gameTimer -= 2;
        }
        else {
            gameTimer--;
        }
    }
    else {
        Game_MakePiece(&currPiece, &nextPiece);
        // if the new piece collides with the board, it's game over
        if (!Game_CheckPiece(&currPiece)) {
            Game_CopyPiece(&currPiece);
            gameState = STATE_GAMEOVER;
            gameTimer = GAME_OVER_FRAMES;
            gameOverRow = 0;
            return;
        }
        if ((level % 100) != 99) {
            level++;
        }
        gameState = STATE_NORMAL;
        Game_BufferRotate();
    }
    // allow to charge DAS for the next piece
    Game_CanMoveLeft();
    Game_CanMoveRight();
}

static void Game_Over() {
    if (gameTimer > 0) {
        gameTimer--;
    }
    else {
        // replace the blocks with the "grayed out" block one row at a time
        for (int i = 0; i < GAME_COLS; i++) {
            if (gameBoard[gameOverRow][i]) {
                gameBoard[gameOverRow][i] = 8;
            }
        }
        gameOverRow++;
        gameTimer = GAME_OVER_FRAMES;
        if (gameOverRow == GAME_ROWS) {
            gameState = STATE_GAMEOVER_DONE;
            Rank_Setup(ranking);
        }
    }
}

int Game_Run() {
    // handle pause button
    if (PadData1E & PAD_S) {
        // pause: save previous state
        if (gameState != STATE_PAUSED) {
            prevState = gameState;
            gameState = STATE_PAUSED;
            
            // clear board on screen so player can't cheat
            for (int y = 0; y < GAME_ROWS; y++) {
                for (int x = 0; x < GAME_COLS; x++) {
                    boardVram[(y * ROW_OFFSET) + x] = 0;
                }
            }
        }
        // resume: restore state
        else {
            gameState = prevState;
        }
    }

    switch (gameState) {
        case STATE_NORMAL:
            Game_Normal();
            break;

        case STATE_LINE:
            Game_Line();
            break;

        case STATE_ARE:
            Game_Are();
            break;

        case STATE_GAMEOVER:
            Game_Over();
            break;

        case STATE_PAUSED:
            break;
    }
    
    Game_DrawPiece(&nextPiece); 
    Game_DrawRanking(ranking);
    Game_DrawNums();
    


    // copy the board to VRAM
    if (gameState != STATE_PAUSED) {
        for (int y = 0; y < GAME_ROWS; y++) {
            for (int x = 0; x < GAME_COLS; x++) {
                boardVram[(y * ROW_OFFSET) + x] = (gameBoard[y][x] * 2);
            }
        }
    }

    BG_Run();

    if (gameState == STATE_GAMEOVER_DONE) {
        return 1;
    }

    return 0;
}
