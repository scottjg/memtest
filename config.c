/* config.c - MemTest-86  Version 3.4
 *
 * Released under version 2 of the Gnu Public License.
 * By Chris Brady
 * ----------------------------------------------------
 * MemTest86+ V4.00 Specific code (GPL V2.0)
 * By Samuel DEMEULEMEESTER, sdemeule@memtest.org
 * http://www.x86-secret.com - http://www.memtest.org
 */
#include "test.h"
#include "screen_buffer.h"
#include "controller.h"
#include "dmi.h"
#define ITER 20

extern int bail, beepmode;
extern struct tseq tseq[];
extern short e820_nr;
extern char memsz_mode;
extern int find_ticks_for_pass();
//void performance();

char save[2][POP_H][POP_W];
char save2[2][POP2_H][POP2_W];

void get_config()
{
	int flag = 0, sflag = 0, i, prt = 0;
        int reprint_screen = 0;
	ulong page;

	popup();
	wait_keyup();
	while(!flag) {
		cprint(POP_Y+1,  POP_X+2, "Settings:");
		cprint(POP_Y+3,  POP_X+6, "(1) Test Selection");
		cprint(POP_Y+4,  POP_X+6, "(2) Address Range");
		cprint(POP_Y+5,  POP_X+6, "(3) Memory Sizing");
		cprint(POP_Y+6,  POP_X+6, "(4) Error Report Mode");
		cprint(POP_Y+7,  POP_X+6, "(5) Show DMI Memory Info");
		cprint(POP_Y+8,  POP_X+6, "(6) ECC Mode"); 
		cprint(POP_Y+9,  POP_X+6, "(7) Restart");
		cprint(POP_Y+10, POP_X+6, "(8) Refresh Screen");
		cprint(POP_Y+11, POP_X+6, "(9) Display SPD Data");
		cprint(POP_Y+12, POP_X+6, "(0) Continue");

		/* Wait for key release */
		/* Fooey! This nuts'es up the serial input. */
		sflag = 0;
		switch(get_key()) {
		case 2:
			/* 1 - Test Selection */
			popclear();
			cprint(POP_Y+1, POP_X+2, "Test Selection:");
			cprint(POP_Y+3, POP_X+6, "(1) Default Tests");
			cprint(POP_Y+4, POP_X+6, "(2) Skip Current Test");
			cprint(POP_Y+5, POP_X+6, "(3) Select Test");
			cprint(POP_Y+6, POP_X+6, "(4) Select Bit Fade Test");
			//cprint(POP_Y+7, POP_X+6, "(5) Select Uncached Test");
			cprint(POP_Y+8, POP_X+6, "(0) Continue");
			if (v->testsel < 0) {
				cprint(POP_Y+3, POP_X+5, ">");
			} else {
				cprint(POP_Y+5, POP_X+5, ">");
			}
			wait_keyup();
			while (!sflag) {
				switch(get_key()) {
				case 2:
					/* Default */
					if (v->testsel >= 9) {
						bail++;
					}
					v->testsel = -1;
					find_ticks_for_pass();
					sflag++;
					cprint(LINE_INFO, COL_TST, "Std");
					break;
				case 3:
					/* Skip test */
					bail++;
					sflag++;
					break;
				case 4:
					/* Select test */
					popclear();
					cprint(POP_Y+1, POP_X+3,
						"Test Selection:");
					cprint(POP_Y+4, POP_X+5,
						"Test Number [0-9]: ");
					i = getval(POP_Y+4, POP_X+24, 0);
					if (i <= 9) {
						if (i != v->testsel) {
							v->pass = -1;
							v->test = -1;
						}
						v->testsel = i;
					}
					find_ticks_for_pass();
					sflag++;
					bail++;
					cprint(LINE_INFO, COL_TST, "#");
					dprint(LINE_INFO, COL_TST+1, i, 2, 1);
					break;
				case 5:
					if (v->testsel != 9) {
						v->pass = -1;
						v->test = -1;
					}
					v->testsel = 9;
					find_ticks_for_pass();
					sflag++;
					bail++;
					cprint(LINE_INFO, COL_TST, "#");
					dprint(LINE_INFO, COL_TST+1, 9, 3, 1);
					break;
/*
				case 6:
					if (v->testsel != 10) {
						v->pass = -1;
						v->test = -1;
					}
					v->testsel = 9+1;
					find_ticks_for_pass();
					sflag++;
					bail++;
					cprint(LINE_INFO, COL_TST, "#");
					dprint(LINE_INFO, COL_TST+1, 10, 3, 1);
					break;
*/					
				case 11:
				case 57:
					sflag++;
					break;
				}
			}
			popclear();
			break;
		case 3:
			/* 2 - Address Range */
			popclear();
			cprint(POP_Y+1, POP_X+2, "Test Address Range:");
			cprint(POP_Y+3, POP_X+6, "(1) Set Lower Limit");
			cprint(POP_Y+4, POP_X+6, "(2) Set Upper Limit");
			cprint(POP_Y+5, POP_X+6, "(3) Test All Memory");
			cprint(POP_Y+6, POP_X+6, "(0) Continue");
			wait_keyup();
			while (!sflag) {
				switch(get_key()) {
				case 2:
					/* Lower Limit */
					popclear();
					cprint(POP_Y+2, POP_X+4,
						"Lower Limit: ");
					cprint(POP_Y+4, POP_X+4,
						"Current: ");
					aprint(POP_Y+4, POP_X+13, v->plim_lower);
					cprint(POP_Y+6, POP_X+4,
						"New: ");
					page = getval(POP_Y+6, POP_X+9, 12);
					if (page + 1 <= v->plim_upper) {
						v->plim_lower = page;
						v->test--;
						bail++;
					}
					adj_mem();
					find_ticks_for_pass();
					sflag++;
					break;
				case 3:
					/* Upper Limit */
					popclear();
					cprint(POP_Y+2, POP_X+4,
						"Upper Limit: ");
					cprint(POP_Y+4, POP_X+4,
						"Current: ");
					aprint(POP_Y+4, POP_X+13, v->plim_upper);
					cprint(POP_Y+6, POP_X+4,
						"New: ");
					page = getval(POP_Y+6, POP_X+9, 12);
					if  (page - 1 >= v->plim_lower) {
						v->plim_upper = page;
						v->test--;
						bail++;
					}
					adj_mem();
					find_ticks_for_pass();
					sflag++;
					break;
				case 4:
					/* All of memory */
					v->plim_lower = 0;
					v->plim_upper = v->pmap[v->msegs - 1].end;
					v->test--;
					bail++;
					adj_mem();
					find_ticks_for_pass();
					sflag++;
					break;
				case 11:
				case 57:
					/* 0/CR - Continue */
					sflag++;
					break;
				}
			}
			popclear();
			break;
		case 4:
			/* 3 - Memory Sizing */
			popclear();
			cprint(POP_Y+1, POP_X+2, "Memory Sizing:");
			cprint(POP_Y+3, POP_X+6, "(1) BIOS - Std");
				cprint(POP_Y+4, POP_X+6, "(2) Probe");
				cprint(POP_Y+5, POP_X+6, "(0) Continue");
			if(!e820_nr){
				if (memsz_mode == SZ_MODE_BIOS) {
					cprint(POP_Y+3, POP_X+5, ">");
				} else {
					cprint(POP_Y+4, POP_X+5, ">");
				}
			}
			wait_keyup();
			while (!sflag) {
				switch(get_key()) {
				case 2:
					memsz_mode = SZ_MODE_BIOS;
					wait_keyup();
					restart();

					break;
				case 3:
					memsz_mode = SZ_MODE_PROBE;
					wait_keyup();
					restart();

					break;
				case 11:
				case 57:
					/* 0/CR - Continue */
					sflag++;
					break;
				}
			}
			popclear();
			break;
		case 5:
			/* 4 - Show error Mode */
			popclear();
			cprint(POP_Y+1, POP_X+2, "Printing Mode:");
			cprint(POP_Y+3, POP_X+6, "(1) Error Summary");
			cprint(POP_Y+4, POP_X+6, "(2) Individual Errors");
			cprint(POP_Y+5, POP_X+6, "(3) BadRAM Patterns");
			cprint(POP_Y+6, POP_X+6, "(4) Error Counts Only");
			cprint(POP_Y+7, POP_X+6, "(5) DMI Device Name");
			cprint(POP_Y+8, POP_X+6, "(6) Beep on Error");
			cprint(POP_Y+10, POP_X+6, "(0) Cancel");
			cprint(POP_Y+3+v->printmode, POP_X+5, ">");
			if (beepmode) { cprint(POP_Y+8, POP_X+5, ">"); }
			wait_keyup();
			while (!sflag) {
								switch(get_key()) {
				case 2:
					/* Error Summary */
					v->printmode=PRINTMODE_SUMMARY;
					v->erri.eadr = 0;
					v->erri.hdr_flag = 0;
					sflag++;
					break;
				case 3:
					/* Separate Addresses */
					v->printmode=PRINTMODE_ADDRESSES;
					v->erri.eadr = 0;
					v->erri.hdr_flag = 0;
					v->msg_line = LINE_SCROLL-1;
					sflag++;
					break;
				case 4:
					/* BadRAM Patterns */
					v->printmode=PRINTMODE_PATTERNS;
					v->erri.hdr_flag = 0;
					sflag++;
					prt++;
					break;
				case 5:
					/* Error Counts Only */
					v->printmode=PRINTMODE_NONE;
					v->erri.hdr_flag = 0;
					sflag++;
					break;
				case 6:
					/* Error Counts Only */
					v->printmode=PRINTMODE_DMI;
					v->erri.hdr_flag = 0;
					sflag++;
					break;
				case 7:
					/* Set Beep On Error mode */
					beepmode = !beepmode;
					sflag++;
					break;					
				case 11:
				case 57:
					/* 0/CR - Continue */
					sflag++;
					break;
				}
			}
			popclear();
			break;
		case 6:
			/* Display DMI Memory Info */
			pop2up();
      print_dmi_info();
			pop2down();
      break;
		case 7:
			/* 6 - ECC Polling Mode */
			popclear();
			cprint(POP_Y+1, POP_X+2, "ECC Polling Mode:");
			cprint(POP_Y+3, POP_X+6, "(1) Recommended");
			cprint(POP_Y+4, POP_X+6, "(2) On");
			cprint(POP_Y+5, POP_X+6, "(3) Off");
			cprint(POP_Y+6, POP_X+6, "(0) Continue");
			wait_keyup();
			while(!sflag) {
				switch(get_key()) {
				case 2:
					set_ecc_polling(-1);
					sflag++;
					break;
				case 3:
					set_ecc_polling(1);
					sflag++;
					break;
				case 4:
					set_ecc_polling(0);
					sflag++;
					break;
				case 11:
				case 57:
					/* 0/CR - Continue */
					sflag++;
					break;
				}
			}
			popclear();
			break;
		case 8:
			wait_keyup();
			restart();
			break;
		case 9:
			reprint_screen = 1;
			flag++;
			break;
		case 10:
			popdown();
			show_spd();
			popup();
			sflag++;
			break;
		case 11:
		case 57:
		case 28:
			/* 0/CR/SP - Continue */
			flag++;
			break;
		}
	}
	popdown();
	if (prt) {
		printpatn();
	}
	if (reprint_screen){
		tty_print_screen();
	}
}


void popup()
{
	int i, j;
	char *pp;
	
	for (i=POP_Y; i<POP_Y + POP_H; i++) { 
		for (j=POP_X; j<POP_X + POP_W; j++) { 
			pp = (char *)(SCREEN_ADR + (i * 160) + (j * 2));
			save[0][i-POP_Y][j-POP_X] = *pp;  /* Save screen */
			set_scrn_buf(i, j, ' ');
			*pp = ' ';		/* Clear */
			pp++;
			save[1][i-POP_Y][j-POP_X] = *pp;
			*pp = 0x07;		/* Change Background to black */
		}
	}
        tty_print_region(POP_Y, POP_X, POP_Y+POP_H, POP_X+POP_W);
}

void popdown()
{
	int i, j;
	char *pp;
	
	for (i=POP_Y; i<POP_Y + POP_H; i++) { 
		for (j=POP_X; j<POP_X + POP_W; j++) { 
			pp = (char *)(SCREEN_ADR + (i * 160) + (j * 2));
			*pp = save[0][i-POP_Y][j-POP_X]; /* Restore screen */
			set_scrn_buf(i, j, save[0][i-POP_Y][j-POP_X]);
			pp++;
			*pp = save[1][i-POP_Y][j-POP_X]; /* Restore color */
		}
	}
        tty_print_region(POP_Y, POP_X, POP_Y+POP_H, POP_X+POP_W);
}

void popclear()
{
	int i, j;
	char *pp;
	
	for (i=POP_Y; i<POP_Y + POP_H; i++) { 
		for (j=POP_X; j<POP_X + POP_W; j++) { 
			pp = (char *)(SCREEN_ADR + (i * 160) + (j * 2));
			*pp = ' ';		/* Clear popup */
			set_scrn_buf(i, j, ' ');
			pp++;
		}
	}
        tty_print_region(POP_Y, POP_X, POP_Y+POP_H, POP_X+POP_W);
}


void pop2up()
{
	int i, j;
	char *pp;

	for (i=POP2_Y; i<POP2_Y + POP2_H; i++) { 
		for (j=POP2_X; j<POP2_X + POP2_W; j++) { 
			pp = (char *)(SCREEN_ADR + (i * 160) + (j * 2));
			save2[0][i-POP2_Y][j-POP2_X] = *pp;  /* Save screen */
			set_scrn_buf(i, j, ' ');
			*pp = ' ';		/* Clear */
			pp++;
			save2[1][i-POP2_Y][j-POP2_X] = *pp;
			*pp = 0x07;		/* Change Background to black */
		}
	}
        tty_print_region(POP2_Y, POP2_X, POP2_Y+POP2_H, POP2_X+POP2_W);
}

void pop2down()
{
	int i, j;
	char *pp;

	for (i=POP2_Y; i<POP2_Y + POP2_H; i++) { 
		for (j=POP2_X; j<POP2_X + POP2_W; j++) { 
			pp = (char *)(SCREEN_ADR + (i * 160) + (j * 2));
			*pp = save2[0][i-POP2_Y][j-POP2_X]; /* Restore screen */
			set_scrn_buf(i, j, save2[0][i-POP2_Y][j-POP2_X]);
			pp++;
			*pp = save2[1][i-POP2_Y][j-POP2_X]; /* Restore color */
		}
	}
        tty_print_region(POP2_Y, POP2_X, POP2_Y+POP2_H, POP2_X+POP2_W);
}

void pop2clear()
{
	int i, j;
	char *pp;

	for (i=POP2_Y; i<POP2_Y + POP2_H; i++) { 
		for (j=POP2_X; j<POP2_X + POP2_W; j++) { 
			pp = (char *)(SCREEN_ADR + (i * 160) + (j * 2));
			*pp = ' ';		/* Clear popup */
			set_scrn_buf(i, j, ' ');
			pp++;
		}
	}
        tty_print_region(POP2_Y, POP2_X, POP2_Y+POP2_H, POP2_X+POP2_W);
}

void clear_screen()
{
	int i;
	volatile char *pp;

	for(i=0, pp=(char *)(SCREEN_ADR); i<80*24; i++) {
		*pp++ = ' ';
		*pp++ = 0x07;
	}
}

void adj_mem(void)
{
	int i;

	v->selected_pages = 0;
	for (i=0; i< v->msegs; i++) {
		/* Segment inside limits ? */
		if (v->pmap[i].start >= v->plim_lower &&
				v->pmap[i].end <= v->plim_upper) {
			v->selected_pages += (v->pmap[i].end - v->pmap[i].start);
			continue;
		}
		/* Segment starts below limit? */
		if (v->pmap[i].start < v->plim_lower) {
			/* Also ends below limit? */
			if (v->pmap[i].end < v->plim_lower) {
				continue;
			}
			
			/* Ends past upper limit? */
			if (v->pmap[i].end > v->plim_upper) {
				v->selected_pages += 
					v->plim_upper - v->plim_lower;
			} else {
				/* Straddles lower limit */
				v->selected_pages += 
					(v->pmap[i].end - v->plim_lower);
			}
			continue;
		}
		/* Segment ends above limit? */
		if (v->pmap[i].end > v->plim_upper) {
			/* Also starts above limit? */
			if (v->pmap[i].start > v->plim_upper) {
				continue;
			}
			/* Straddles upper limit */
			v->selected_pages += 
				(v->plim_upper - v->pmap[i].start);
		}
	}
}

/*
void performance()
{ 
	extern int l1_cache, l2_cache;
	ulong speed;
	int i;
	
	popclear();

	cprint(POP_Y+1, POP_X+1, "             Read   Write    Copy");
	cprint(POP_Y+3, POP_X+1, "L1 Cache:");
	speed=memspeed((ulong)mapping(0x100), (l1_cache/4)*1024, 500, MS_READ);
	dprint(POP_Y+3, POP_X+10, speed, 6, 0);
	speed=memspeed((ulong)mapping(0x100), (l1_cache/4)*1024, 50, MS_WRITE);
	dprint(POP_Y+3, POP_X+17, speed, 6, 0);
	speed=memspeed((ulong)mapping(0x100), (l1_cache/4)*1024, 50, MS_COPY);
	dprint(POP_Y+3, POP_X+24, speed, 6, 0);

	if (l2_cache < l1_cache) {
		i = l1_cache / 4 + l2_cache / 4;
	} else {
		i = l1_cache;
	}
	cprint(POP_Y+5, POP_X+1, "L2 Cache:");
	speed=memspeed((ulong)mapping(0x100), i*1024, 500, MS_READ);
	dprint(POP_Y+5, POP_X+10, speed, 6, 0);
	speed=memspeed((ulong)mapping(0x100), i*1024, 50, MS_WRITE);
	dprint(POP_Y+5, POP_X+17, speed, 6, 0);
	speed=memspeed((ulong)mapping(0x100), i*1024, 50, MS_COPY);
	dprint(POP_Y+5, POP_X+24, speed, 6, 0);

	// Determine memory speed.  To find the memory spped we use
  // A block size that is 5x the sum of the L1 and L2 caches
        i = (l2_cache + l1_cache) * 5;

  // Make sure that we have enough memory to do the test
     if ((1 + (i * 2)) > (v->plim_upper << 2)) {
             i = ((v->plim_upper <<2) - 1) / 2;
     }
        
        
	cprint(POP_Y+7, POP_X+1, "Memory:");
	speed=memspeed((ulong)mapping(0x100), i*1024, 500, MS_READ);
	dprint(POP_Y+7, POP_X+10, speed, 6, 0);
	speed=memspeed((ulong)mapping(0x100), i*1024, 50, MS_WRITE);
	dprint(POP_Y+7, POP_X+17, speed, 6, 0);
	speed=memspeed((ulong)mapping(0x100), i*1024, 50, MS_COPY);
	dprint(POP_Y+7, POP_X+24, speed, 6, 0);

	wait_keyup();
	while (get_key() == 0);
	popclear();
}
*/
