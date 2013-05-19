/* Memtest86 SPD extension 
 * added by Reto Sonderegger, 2004, reto@swissbit.com
 * 
 * Released under version 2 of the Gnu Puclic License
 * ----------------------------------------------------
 * MemTest86+ V4.20 Specific code (GPL V2.0)
 * By Samuel DEMEULEMEESTER, sdemeule@memtest.org
 * http://www.canardpc.com - http://www.memtest.org
 */

 
#include "test.h"
#include "io.h"
#include "pci.h"
#include "msr.h"
#include "spd.h"
#include "screen_buffer.h"
#include "jedec_id.h"

#define NULL 0

#define SMBHSTSTS smbusbase
#define SMBHSTCNT smbusbase + 2
#define SMBHSTCMD smbusbase + 3
#define SMBHSTADD smbusbase + 4
#define SMBHSTDAT smbusbase + 5

extern void wait_keyup();

int smbdev, smbfun;
unsigned short smbusbase;
unsigned char spd[256];
char s[] = {'/', 0, '-', 0, '\\', 0, '|', 0};	

static void ich5_get_smb(void)
{
    unsigned long x;
    int result;
    result = pci_conf_read(0, smbdev, smbfun, 0x20, 2, &x);
    if (result == 0) smbusbase = (unsigned short) x & 0xFFFE;
}

unsigned char ich5_smb_read_byte(unsigned char adr, unsigned char cmd)
{
    int l1, h1, l2, h2;
    unsigned long long t;
    __outb(0x1f, SMBHSTSTS);			// reset SMBus Controller
    __outb(0xff, SMBHSTDAT);
    while(__inb(SMBHSTSTS) & 0x01);		// wait until ready
    __outb(cmd, SMBHSTCMD);
    __outb((adr << 1) | 0x01, SMBHSTADD);
    __outb(0x48, SMBHSTCNT);
    rdtsc(l1, h1);
    //cprint(POP2_Y, POP2_X + 16, s + cmd % 8);	// progress bar
    while (!(__inb(SMBHSTSTS) & 0x02)) {	// wait til command finished
	rdtsc(l2, h2);
	t = ((h2 - h1) * 0xffffffff + (l2 - l1)) / v->clks_msec;
	if (t > 10) break;			// break after 10ms
    }
    return __inb(SMBHSTDAT);
}

static int ich5_read_spd(int dimmadr)
{
    int x;
    spd[0] = ich5_smb_read_byte(0x50 + dimmadr, 0);
    if (spd[0] == 0xff)	return -1;		// no spd here
    for (x = 1; x < 256; x++) {
			spd[x] = ich5_smb_read_byte(0x50 + dimmadr, (unsigned char) x);
    }
    return 0;
}

static void us15w_get_smb(void)
{
    unsigned long x;
    int result;
    result = pci_conf_read(0, 0x1f, 0, 0x40, 2, &x);
    if (result == 0) smbusbase = (unsigned short) x & 0xFFC0;
}

unsigned char us15w_smb_read_byte(unsigned char adr, unsigned char cmd)
{
    int l1, h1, l2, h2;
    unsigned long long t;
    //__outb(0x00, smbusbase + 1);			// reset SMBus Controller
    //__outb(0x00, smbusbase + 6);
    //while((__inb(smbusbase + 1) & 0x08) != 0);		// wait until ready
    __outb(0x02, smbusbase + 0);    // Byte read
    __outb(cmd, smbusbase + 5);     // Command
    __outb(0x07, smbusbase + 1);    // Clear status
    __outb((adr << 1) | 0x01, smbusbase + 4);   // DIMM address
    __outb(0x12, smbusbase + 0);    // Start
    //while (((__inb(smbusbase + 1) & 0x08) == 0)) {}	// wait til busy
    rdtsc(l1, h1);
    cprint(POP2_Y, POP2_X + 16, s + cmd % 8);	// progress bar
    while (((__inb(smbusbase + 1) & 0x01) == 0) ||
		((__inb(smbusbase + 1) & 0x08) != 0)) {	// wait til command finished
	rdtsc(l2, h2);
	t = ((h2 - h1) * 0xffffffff + (l2 - l1)) / v->clks_msec;
	if (t > 10) break;			// break after 10ms
    }
    return __inb(smbusbase + 6);
}

static int us15w_read_spd(int dimmadr)
{
    int x;
    spd[0] = us15w_smb_read_byte(0x50 + dimmadr, 0);
    if (spd[0] == 0xff)	return -1;		// no spd here
    for (x = 1; x < 256; x++) {
	spd[x] = us15w_smb_read_byte(0x50 + dimmadr, (unsigned char) x);
    }
    return 0;
}
    
struct pci_smbus_controller {
    unsigned vendor;
    unsigned device;
    char *name;
    void (*get_adr)(void);
    int (*read_spd)(int dimmadr);
};

static struct pci_smbus_controller smbcontrollers[] = {
{0x8086, 0x1C22, "Intel P67", 		ich5_get_smb, ich5_read_spd},
{0x8086, 0x3B30, "Intel P55", 		ich5_get_smb, ich5_read_spd},
{0x8086, 0x3A60, "Intel ICH10B", 	ich5_get_smb, ich5_read_spd},
{0x8086, 0x3A30, "Intel ICH10R", 	ich5_get_smb, ich5_read_spd},
{0x8086, 0x2930, "Intel ICH9", 		ich5_get_smb, ich5_read_spd},
{0x8086, 0x283E, "Intel ICH8", 		ich5_get_smb, ich5_read_spd},
{0x8086, 0x27DA, "Intel ICH7", 		ich5_get_smb, ich5_read_spd},
{0x8086, 0x266A, "Intel ICH6", 		ich5_get_smb, ich5_read_spd},
{0x8086, 0x24D3, "Intel ICH5", 		ich5_get_smb, ich5_read_spd},
{0x8086, 0x24C3, "Intel ICH4", 		ich5_get_smb, ich5_read_spd},
{0x8086, 0x25A4, "Intel 6300ESB", ich5_get_smb, ich5_read_spd},
{0x8086, 0x269B, "Intel ESB2", 		ich5_get_smb, ich5_read_spd},
{0x8086, 0x8119, "Intel US15W", 	us15w_get_smb, us15w_read_spd},
{0x8086, 0x5032, "Intel EP80579", ich5_get_smb, ich5_read_spd},
{0, 0, "", NULL, NULL}
};


int find_smb_controller(void)
{
    int i = 0;
    unsigned long valuev, valued;
    for (smbdev = 0; smbdev < 32; smbdev++) {
			for (smbfun = 0; smbfun < 8; smbfun++) {
		    pci_conf_read(0, smbdev, smbfun, 0, 2, &valuev);
		    if (valuev != 0xFFFF) {					// if there is something look what's it..
					for (i = 0; smbcontrollers[i].vendor > 0; i++) {	// check if this is a known smbus controller
			    	if (valuev == smbcontrollers[i].vendor) {
							pci_conf_read(0, smbdev, smbfun, 2, 2, &valued);	// read the device id
							if (valued == smbcontrollers[i].device) {
				    		return i;
							}
			    	}
					}
		    }	
			}
    }
    return -1;
}



void get_spd_spec(void)
{
	  int index;
    int h, i, j, z;
    int k = 0;
    int module_size;
    int curcol;
    int temp_nbd;

    index = find_smb_controller();
    
    if (index == -1) 
    {
    	// Unknown SMBUS Controller, exit
			return;
    }

    smbcontrollers[index].get_adr();
		cprint(LINE_SPD-2, 0, "Memory SPD Informations");
		cprint(LINE_SPD-1, 0, "-----------------------------------");    
		
    for (j = 0; j < 8; j++) {
			if (smbcontrollers[index].read_spd(j) == 0) {	
				curcol = 1;
				if(spd[2] == 0x0b){
				  // We are here if DDR3 present
				 
				  // First print slot#, module capacity
					cprint(LINE_SPD+k, curcol, " - Slot   :");
					dprint(LINE_SPD+k, curcol+8, k, 1, 0);

					module_size = get_ddr3_module_size(spd[4] & 0xF, spd[8] & 0x7, spd[7] & 0x7, spd[7] >> 3);
					temp_nbd = getnum(module_size); curcol += 12;
					dprint(LINE_SPD+k, curcol, module_size, temp_nbd, 0); curcol += temp_nbd;
					cprint(LINE_SPD+k, curcol, " MB"); curcol += 4;
					
					// Then module jedec speed
					switch(spd[12])
					{
						default:
						case 20:
							cprint(LINE_SPD+k, curcol, "PC3-6400");
							curcol += 8;
							break;
						case 15:
							cprint(LINE_SPD+k, curcol, "PC3-8500");
							curcol += 8;
							break;
						case 12:
							cprint(LINE_SPD+k, curcol, "PC3-10600");
							curcol += 9;
							break;
						case 10:
							cprint(LINE_SPD+k, curcol, "PC3-12800");
							curcol += 9;
							break;
						case 8:
							cprint(LINE_SPD+k, curcol, "PC3-15000");
							curcol += 9;
							break;
						case 6:
							cprint(LINE_SPD+k, curcol, "PC3-16000");
							curcol += 9;
							break;
						}
					
					curcol++;
					
					// Then print module infos (manufacturer & part number)	
					spd[117] &= 0x0F; // Parity odd or even
					for (i = 0; jep106[i].cont_code < 9; i++) {	
			    	if (spd[117] == jep106[i].cont_code && spd[118] == jep106[i].hex_byte) {
			    		// We are here if a Jedec manufacturer is detected
							cprint(LINE_SPD+k, curcol, "-"); curcol += 2;							
							cprint(LINE_SPD+k, curcol, jep106[i].name);
							for(z = 0; jep106[i].name[z] != '\0'; z++) { curcol++; }
							curcol++;
							// Display module serial number
							for (h = 128; h < 146; h++) {	
								cprint(16+k, curcol, convert_hex_to_char(spd[h]));
								curcol++;		
							}			
															
							// Detect XMP Memory
							if(spd[176] == 0x0C && spd[177] == 0x4A)
								{
									cprint(LINE_SPD+k, curcol, "*XMP*");					
								}
			    	}
					}		
				}
			// We enter this function if DDR2 is detected
			if(spd[2] == 0x08){				
					 // First print slot#, module capacity
					cprint(LINE_SPD+k, curcol, " - Slot   :");
					dprint(LINE_SPD+k, curcol+8, k, 1, 0);

					module_size = get_ddr2_module_size(spd[31], spd[5]);
					temp_nbd = getnum(module_size); curcol += 12;
					dprint(LINE_SPD+k, curcol, module_size, temp_nbd, 0); curcol += temp_nbd;
					cprint(LINE_SPD+k, curcol, " MB"); curcol += 4;		

					// Then module jedec speed
					float ddr2_speed, byte1, byte2;
					
					byte1 = (spd[9] >> 4) * 10;
					byte2 = spd[9] & 0xF;
					
					ddr2_speed = 1 / (byte1 + byte2) * 10000;

					temp_nbd = getnum(ddr2_speed);
					cprint(LINE_SPD+k, curcol, "DDR2-"); curcol += 5;	 
					dprint(LINE_SPD+k, curcol, ddr2_speed, temp_nbd, 0); curcol += temp_nbd;
			
					// Then print module infos (manufacturer & part number)	
					int ccode = 0;
					
					for(i = 64; i < 72; i++)
					{
						if(spd[i] == 0x7F) { ccode++; }			
					}
					
					curcol++;
					
					for (i = 0; jep106[i].cont_code < 9; i++) {	
			    	if (ccode == jep106[i].cont_code && spd[64+ccode] == jep106[i].hex_byte) {
			    		// We are here if a Jedec manufacturer is detected
							cprint(LINE_SPD+k, curcol, "-"); curcol += 2;							
							cprint(LINE_SPD+k, curcol, jep106[i].name);
							for(z = 0; jep106[i].name[z] != '\0'; z++) { curcol++; }
							curcol++;
							// Display module serial number
							for (h = 73; h < 91; h++) {	
								cprint(16+k, curcol, convert_hex_to_char(spd[h]));
								curcol++;		
							}			
															
			    	}
					}				

				}	
			k++;
			}
    }
}

	        
void show_spd(void)
{
    int index;
    int i, j;
    int flag = 0;
    pop2up();
    wait_keyup();
    index = find_smb_controller();
    if (index == -1) {
	cprint(POP2_Y, POP2_X+1, "SMBus Controller not known");
	while (!get_key());
	wait_keyup();
	pop2down();
	return;
    }
    else cprint(POP2_Y, POP2_X+1, "SPD Data: Slot");    
    smbcontrollers[index].get_adr();
    for (j = 0; j < 16; j++) {
	if (smbcontrollers[index].read_spd(j) == 0) {
	    dprint(POP2_Y, POP2_X + 15, j, 2, 0);		
    	    for (i = 0; i < 256; i++) {
		hprint2(2 + POP2_Y + i / 16, 3 + POP2_X + (i % 16) * 3, spd[i], 2);
	    }
	    flag = 0;
    	    while(!flag) {
		if (get_key()) flag++;
	    }
	    wait_keyup();
	}
    }
    pop2down();
}

int get_ddr3_module_size(int sdram_capacity, int prim_bus_width, int sdram_width, int ranks)
{
	int module_size;
	
	switch(sdram_capacity)
	{
		case 0:
			module_size = 256;
			break;
		case 1:
			module_size = 512;
			break;		
		default:
		case 2:
			module_size = 1024;
			break;		
		case 3:
			module_size = 2048;
			break;
		case 4:
			module_size = 4096;
			break;		
		case 5:
			module_size = 8192;
			break;	
		case 6:
			module_size = 16384;		
			break;		
		}
		
		module_size /= 8;
	
	switch(prim_bus_width)
	{
		case 0:
			module_size *= 8;
			break;
		case 1:
			module_size *= 16;
			break;		
		case 2:
			module_size *= 32;
			break;		
		case 3:
			module_size *= 64;
			break;		
		}		
	
		switch(sdram_width)
	{
		case 0:
			module_size /= 4;
			break;
		case 1:
			module_size /= 8;
			break;		
		case 2:
			module_size /= 16;
			break;		
		case 3:
			module_size /= 32;
			break;		

		}	
	
	module_size *= (ranks + 1);
	
	return module_size;
}


int get_ddr2_module_size(int rank_density_byte, int rank_num_byte)
{
	int module_size;
	
	switch(rank_density_byte)
	{
		case 1:
			module_size = 1024;
			break;
		case 2:
			module_size = 2048;
			break;		
		case 4:
			module_size = 4096;
			break;		
		case 8:
			module_size = 8192;
			break;		
		case 16:
			module_size = 16384;
			break;
		case 32:
			module_size = 128;
			break;		
		case 64:
			module_size = 256;
			break;		
		default:
		case 128:
			module_size = 512;
			break;	
		}	
		
	module_size *= (rank_num_byte & 7) + 1;
	
	return module_size;
		
}


struct ascii_map {
    unsigned hex_code;
    char *name;
};


char* convert_hex_to_char(unsigned hex_org) {
        static char buf[2] = " ";
        if (hex_org >= 0x20 && hex_org < 0x80) {
                buf[0] = hex_org;
        } else {
                //buf[0] = '\0';
                buf[0] = ' ';
        }

        return buf;
}