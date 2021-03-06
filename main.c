#include <machine.h>
#include <stdint.h>
#include <string.h> //memcpy
#include <sega_dma.h>
#define	_SPR2_
#include <sega_spr.h>
#include <sega_scl.h> 
#include <sega_mth.h>
#include <sega_cdc.h>
#include <sega_sys.h>
#include <sega_per.h>
#include <sega_xpt.h>

#include "bg.h"
#include "cd.h"
#include "devcart.h"
#include "game.h"
#include "rank.h"
#include "release.h"
#include "scroll.h"
#include "sound.h"
#include "sprite.h"
#include "title.h"
#include "print.h"
#include "vblank.h"

volatile Uint32 frame = 0;

typedef enum {
    STATE_TITLE = 0,
    STATE_GAME,
    STATE_RANK,
} GAME_STATE;

int state;

int main() {
	frame = 0;

	CdcStat cd_status;
//
//	set_imask(0); // enable interrupts
//
	SCL_Vdp2Init();
    DMA_ScuInit();
	CD_Init();
	Sprite_Init();
	Scroll_Init();
	Print_Init();
	Print_Load();
	Sound_Init();
    Sound_CDVolume(6, 6);
    SCL_DisplayFrame();
    SCL_DisplayFrame();
    state = STATE_TITLE;
    Title_Init();
    
    /*
    state = STATE_GAME;
    Game_Init();
    */
    while (1) {
        frame++;

		// if the cd drive is opened, return to menu (don't do this with devcart
		// because leaving the door open skips the boot animation which is nice)
		if (!DEVCART_LOAD) {
			CDC_GetPeriStat(&cd_status);
			if ((cd_status.status & 0xF) == CDC_ST_OPEN) {
				SYS_EXECDMP();
			}
		}

		//if player hits A+B+C+Start, return to menu
		if (PadData1 == (PAD_A | PAD_B | PAD_C | PAD_S)) {
			if (DEVCART_LOAD) {
				Devcart_Reset();
			}
			else {
				SYS_EXECDMP();
			}
		}

		Sprite_StartDraw();
        
        switch (state) {
            case STATE_TITLE:
                if (Title_Run()) {
                    Game_Init();
                    state = STATE_GAME;
                }
                break;

            case STATE_GAME:
                if (Game_Run()) {
                    Rank_Init();
                    state = STATE_RANK;
                }
                break;

            case STATE_RANK:
                if (Rank_Run()) {
                    Title_Init();
                    state = STATE_TITLE;
                }
                break;
        }
       
		Sprite_DrawAll();
		if (DEBUG) {
			Print_Display();
		}
		SPR_2CloseCommand();

		SCL_DisplayFrame();		// wait for vblank int to set flag to 0
	}

	return 0;
}
