/* controller.c - MemTest-86  Version 3.0
 *
 * Released under version 2 of the Gnu Public License.
 * By Chris Brady, cbrady@sgi.com
 * ----------------------------------------------------
 * MemTest86+ V4.20 Specific code (GPL V2.0)
 * By Samuel DEMEULEMEESTER, sdemeule@memtest.org
 * http://www.canardpc.com - http://www.memtest.org
 */

#include "defs.h"
#include "config.h"
#include "test.h"
#include "pci.h"
#include "controller.h"
#include "spd.h"
#include "test.h"


int col, col2;
int nhm_bus = 0x3F;
	
extern ulong extclock;
extern unsigned long imc_type;
extern struct cpu_ident cpu_id;
extern int fail_safe;

#define rdmsr(msr,val1,val2) \
	__asm__ __volatile__("rdmsr" \
			  : "=a" (val1), "=d" (val2) \
			  : "c" (msr))

#define wrmsr(msr,val1,val2) \
	__asm__ __volatile__("wrmsr" \
			  : /* no outputs */ \
			  : "c" (msr), "a" (val1), "d" (val2))

/* controller ECC capabilities and mode */
#define __ECC_UNEXPECTED 1      /* Unknown ECC capability present */
#define __ECC_DETECT     2	/* Can detect ECC errors */
#define __ECC_CORRECT    4	/* Can correct some ECC errors */
#define __ECC_SCRUB      8	/* Can scrub corrected ECC errors */
#define __ECC_CHIPKILL  16	/* Can corrected multi-errors */

#define ECC_UNKNOWN      (~0UL)    /* Unknown error correcting ability/status */
#define ECC_NONE         0       /* Doesnt support ECC (or is BIOS disabled) */
#define ECC_RESERVED     __ECC_UNEXPECTED  /* Reserved ECC type */
#define ECC_DETECT       __ECC_DETECT
#define ECC_CORRECT      (__ECC_DETECT | __ECC_CORRECT)
#define ECC_CHIPKILL	 (__ECC_DETECT | __ECC_CORRECT | __ECC_CHIPKILL)
#define ECC_SCRUB        (__ECC_DETECT | __ECC_CORRECT | __ECC_SCRUB)


static struct ecc_info {
	int index;
	int poll;
	unsigned bus;
	unsigned dev;
	unsigned fn;
	unsigned cap;
	unsigned mode;
} ctrl =
{
	.index = 0,
	/* I know of no case where the memory controller is not on the
	 * host bridge, and the host bridge is not on bus 0  device 0
	 * fn 0.  But just in case leave these as variables.
	 */
	.bus = 0,
	.dev = 0,
	.fn = 0,
	/* Properties of the current memory controller */
	.cap = ECC_UNKNOWN,
	.mode = ECC_UNKNOWN,
};

struct pci_memory_controller {
	unsigned vendor;
	unsigned device;
	char *name;
	int tested;
	void (*poll_fsb)(void);
	void (*poll_timings)(void);
	void (*setup_ecc)(void);
	void (*poll_errors)(void);
};


void print_timings_info(float cas, int rcd, int rp, int ras) {

	/* Now, we could print some additionnals timings infos) */
	cprint(LINE_CPU+6, col2 +1, "/ CAS : ");
	col2 += 9;

	// CAS Latency (tCAS)
	if (cas == 1.5) {
		cprint(LINE_CPU+6, col2, "1.5"); col2 += 3;
	} else if (cas == 2.5) {
		cprint(LINE_CPU+6, col2, "2.5"); col2 += 3;
	} else if (cas < 10) {
		dprint(LINE_CPU+6, col2, cas, 1, 0); col2 += 1;
	} else {
		dprint(LINE_CPU+6, col2, cas, 2, 0); col2 += 2;		
	}
	cprint(LINE_CPU+6, col2, "-"); col2 += 1;

	// RAS-To-CAS (tRCD)
	if (rcd < 10) {
		dprint(LINE_CPU+6, col2, rcd, 1, 0);
		col2 += 1;
	} else {
		dprint(LINE_CPU+6, col2, rcd, 2, 0);
		col2 += 2;		
	}
	cprint(LINE_CPU+6, col2, "-"); col2 += 1;

	// RAS Precharge (tRP)
	if (rp < 10) {
		dprint(LINE_CPU+6, col2, rp, 1, 0);
		col2 += 1;
	} else {
		dprint(LINE_CPU+6, col2, rp, 2, 0);
		col2 += 2;		
	}
	cprint(LINE_CPU+6, col2, "-"); col2 += 1;

	// RAS Active to precharge (tRAS)
	if (ras < 10) {
		dprint(LINE_CPU+6, col2, ras, 1, 0);
		col2 += 2;
	} else {
		dprint(LINE_CPU+6, col2, ras, 2, 0);
		col2 += 3;
	}

}

void print_fsb_info(float val, const char *text_fsb, const char *text_ddr) {

	int i;

	cprint(LINE_CPU+6, col2, "Settings: ");
	col2 += 10;
	cprint(LINE_CPU+6, col2, text_fsb);
	col2 += 6;
	dprint(LINE_CPU+6, col2, val ,3 ,0);
	col2 += 3;
	cprint(LINE_CPU+6, col2 +1, "MHz (");
	col2 += 6;
	
	cprint(LINE_CPU+6, col2, text_ddr);
	for(i = 0; text_ddr[i] != '\0'; i++) { col2++; }
	
	if(val < 500) {
	dprint(LINE_CPU+6, col2, val*2 ,3 ,0);
	col2 += 3; 
	} else {
	dprint(LINE_CPU+6, col2, val*2 ,4 ,0);
	col2 += 4; 		
	}
	cprint(LINE_CPU+6, col2, ")");
	col2 += 1;
}



static void poll_fsb_nothing(void)
{
/* Code to run for no specific fsb detection */
	return;
}

static void poll_timings_nothing(void)
{
/* Code to run for no specific timings detection */
	return;
}

static void poll_fsb_failsafe(void)
{
/* Code to run for no specific fsb detection */
	cprint(LINE_CPU+5, 0, "Chipset/IMC : ***FAIL SAFE***FAIL SAFE***FAIL SAFE***FAIL SAFE***FAIL SAFE***");
	cprint(LINE_CPU+6, 0, "*** Memtest86+ is running in fail safe mode. Same reliability, less details ***");
	return;
}
static void setup_nothing(void)
{
	ctrl.cap = ECC_NONE;
	ctrl.mode = ECC_NONE;
}

static void poll_nothing(void)
{
/* Code to run when we don't know how, or can't ask the memory
 * controller about memory errors.
 */
	return;
}

static void setup_wmr(void)
{

	// Activate MMR I/O
	ulong dev0;
	ctrl.cap = ECC_CORRECT;
	
	pci_conf_read( 0, 0, 0, 0x48, 4, &dev0);
	if (!(dev0 & 0x1)) {
		pci_conf_write( 0, 0, 0, 0x48, 1, dev0 | 1);
	}

	ctrl.mode = ECC_NONE; 
	
}


static void setup_nhm(void)
{
	static float possible_nhm_bus[] = {0xFF, 0x7F, 0x3F};
	unsigned long did, vid, mc_control, mc_ssrcontrol;
	int i;
	
	//Nehalem supports Scrubbing */
	ctrl.cap = ECC_SCRUB;
	ctrl.mode = ECC_NONE;

	/* First, locate the PCI bus where the MCH is located */

	for(i = 0; i < sizeof(possible_nhm_bus); i++) {
		pci_conf_read( possible_nhm_bus[i], 3, 4, 0x00, 2, &vid);
		pci_conf_read( possible_nhm_bus[i], 3, 4, 0x02, 2, &did);
		vid &= 0xFFFF;
		did &= 0xFF00;
		if(vid == 0x8086 && did >= 0x2C00) { 
			nhm_bus = possible_nhm_bus[i]; 
			}
}

	/* Now, we have the last IMC bus number in nhm_bus */
	/* Check for ECC & Scrub */
	
	pci_conf_read(nhm_bus, 3, 0, 0x4C, 2, &mc_control);	
	if((mc_control >> 4) & 1) { 
		ctrl.mode = ECC_CORRECT; 
		pci_conf_read(nhm_bus, 3, 2, 0x48, 2, &mc_ssrcontrol);	
		if(mc_ssrcontrol & 3) { 
			ctrl.mode = ECC_SCRUB; 
		}		
	}
	
}

static void setup_nhm32(void)
{
	static float possible_nhm_bus[] = {0xFF, 0x7F, 0x3F};
	unsigned long did, vid, mc_control, mc_ssrcontrol;
	int i;
	
	//Nehalem supports Scrubbing */
	ctrl.cap = ECC_SCRUB;
	ctrl.mode = ECC_NONE;

	/* First, locate the PCI bus where the MCH is located */

	for(i = 0; i < sizeof(possible_nhm_bus); i++) {
		pci_conf_read( possible_nhm_bus[i], 3, 4, 0x00, 2, &vid);
		pci_conf_read( possible_nhm_bus[i], 3, 4, 0x02, 2, &did);
		vid &= 0xFFFF;
		did &= 0xFF00;
		if(vid == 0x8086 && did >= 0x2C00) { 
			nhm_bus = possible_nhm_bus[i]; 
			}
}

	/* Now, we have the last IMC bus number in nhm_bus */
	/* Check for ECC & Scrub */
	pci_conf_read(nhm_bus, 3, 0, 0x48, 2, &mc_control);	
	if((mc_control >> 1) & 1) { 
		ctrl.mode = ECC_CORRECT; 
		pci_conf_read(nhm_bus, 3, 2, 0x48, 2, &mc_ssrcontrol);	
		if(mc_ssrcontrol & 1) { 
			ctrl.mode = ECC_SCRUB; 
		}		
	}
	
}

static void setup_amd64(void)
{

	static const int ddim[] = { ECC_NONE, ECC_CORRECT, ECC_RESERVED, ECC_CHIPKILL };
	unsigned long nbxcfg;
	unsigned int mcgsrl;
	unsigned int mcgsth;
	unsigned long mcanb;
	unsigned long dramcl;

	/* All AMD64 support Chipkill */
	ctrl.cap = ECC_CHIPKILL;

	/* Check First if ECC DRAM Modules are used */
	pci_conf_read(0, 24, 2, 0x90, 4, &dramcl);
	
	
	if (((cpu_id.ext >> 16) & 0xF) >= 4) {
		/* NEW K8 0Fh Family 90 nm */
		
		if ((dramcl >> 19)&1){
			/* Fill in the correct memory capabilites */
			pci_conf_read(0, 24, 3, 0x44, 4, &nbxcfg);
			ctrl.mode = ddim[(nbxcfg >> 22)&3];
		} else {
			ctrl.mode = ECC_NONE;
		}
		/* Enable NB ECC Logging by MSR Write */
		rdmsr(0x017B, mcgsrl, mcgsth);
		wrmsr(0x017B, 0x10, mcgsth);
	
		/* Clear any previous error */
		pci_conf_read(0, 24, 3, 0x4C, 4, &mcanb);
		pci_conf_write(0, 24, 3, 0x4C, 4, mcanb & 0x7FFFFFFF );		

	} else { 
		/* OLD K8 130 nm */
		
		if ((dramcl >> 17)&1){
			/* Fill in the correct memory capabilites */
			pci_conf_read(0, 24, 3, 0x44, 4, &nbxcfg);
			ctrl.mode = ddim[(nbxcfg >> 22)&3];
		} else {
			ctrl.mode = ECC_NONE;
		}
		/* Enable NB ECC Logging by MSR Write */
		rdmsr(0x017B, mcgsrl, mcgsth);
		wrmsr(0x017B, 0x10, mcgsth);
	
		/* Clear any previous error */
		pci_conf_read(0, 24, 3, 0x4C, 4, &mcanb);
		pci_conf_write(0, 24, 3, 0x4C, 4, mcanb & 0x7F801EFC );
	}
}

static void setup_k10(void)
{
	static const int ddim[] = { ECC_NONE, ECC_CORRECT, ECC_CHIPKILL, ECC_CHIPKILL };
	unsigned long nbxcfg;
	unsigned int mcgsrl;
	unsigned int mcgsth;
	unsigned long mcanb;
	unsigned long dramcl;
	ulong msr_low, msr_high;

	/* All AMD64 support Chipkill */
	ctrl.cap = ECC_CHIPKILL;

	/* Check First if ECC DRAM Modules are used */
	pci_conf_read(0, 24, 2, 0x90, 4, &dramcl);
	
		if ((dramcl >> 19)&1){
			/* Fill in the correct memory capabilites */
			pci_conf_read(0, 24, 3, 0x44, 4, &nbxcfg);
			ctrl.mode = ddim[(nbxcfg >> 22)&3];
		} else {
			ctrl.mode = ECC_NONE;
		}
		/* Enable NB ECC Logging by MSR Write */
		rdmsr(0x017B, mcgsrl, mcgsth);
		wrmsr(0x017B, 0x10, mcgsth);
	
		/* Clear any previous error */
		pci_conf_read(0, 24, 3, 0x4C, 4, &mcanb);
		pci_conf_write(0, 24, 3, 0x4C, 4, mcanb & 0x7FFFFFFF );	
		
		/* Enable ECS */
		rdmsr(0xC001001F, msr_low,  msr_high);
		wrmsr(0xC001001F, msr_low, (msr_high | 0x4000));
		rdmsr(0xC001001F, msr_low,  msr_high);

}

static void poll_amd64(void)
{

	unsigned long mcanb;
	unsigned long page, offset;
	unsigned long celog_syndrome;
	unsigned long mcanb_add;

	pci_conf_read(0, 24, 3, 0x4C, 4, &mcanb);

	if (((mcanb >> 31)&1) && ((mcanb >> 14)&1)) {
		/* Find out about the first correctable error */
		/* Syndrome code -> bits use a complex matrix. Will add this later */
		/* Read the error location */
		pci_conf_read(0, 24, 3, 0x50, 4, &mcanb_add);

		/* Read the syndrome */
		celog_syndrome = (mcanb >> 15)&0xFF;

		/* Parse the error location */
		page = (mcanb_add >> 12);
		offset = (mcanb_add >> 3) & 0xFFF;

		/* Report the error */
		print_ecc_err(page, offset, 1, celog_syndrome, 0);

		/* Clear the error registers */
		pci_conf_write(0, 24, 3, 0x4C, 4, mcanb & 0x7FFFFFFF );
	}
	if (((mcanb >> 31)&1) && ((mcanb >> 13)&1)) {
		/* Found out about the first uncorrectable error */
		/* Read the error location */
		pci_conf_read(0, 24, 3, 0x50, 4, &mcanb_add);

		/* Parse the error location */
		page = (mcanb_add >> 12);
		offset = (mcanb_add >> 3) & 0xFFF;

		/* Report the error */
		print_ecc_err(page, offset, 0, 0, 0);

		/* Clear the error registers */
		pci_conf_write(0, 24, 3, 0x4C, 4, mcanb & 0x7FFFFFF );

	}

}

static void setup_amd751(void)
{
	unsigned long dram_status;

	/* Fill in the correct memory capabilites */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x5a, 2, &dram_status);
	ctrl.cap = ECC_CORRECT;
	ctrl.mode = (dram_status & (1 << 2))?ECC_CORRECT: ECC_NONE;
}

static void poll_amd751(void)
{
	unsigned long ecc_status;
	unsigned long bank_addr;
	unsigned long bank_info;
	unsigned long page;
	int bits;
	int i;

	/* Read the error status */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x58, 2, &ecc_status);
	if (ecc_status & (3 << 8)) {
		for(i = 0; i < 6; i++) {
			if (!(ecc_status & (1 << i))) {
				continue;
			}
			/* Find the bank the error occured on */
			bank_addr = 0x40 + (i << 1);

			/* Now get the information on the erroring bank */
			pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, bank_addr, 2, &bank_info);

			/* Parse the error location and error type */
			page = (bank_info & 0xFF80) << 4;
			bits = (((ecc_status >> 8) &3) == 2)?1:2;

			/* Report the error */
			print_ecc_err(page, 0, bits==1?1:0, 0, 0);

		}

		/* Clear the error status */
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0x58, 2, 0);
	}
}

/* Still waiting for the CORRECT intel datasheet
static void setup_i85x(void)
{
	unsigned long drc;
	ctrl.cap = ECC_CORRECT;

	pci_conf_read(ctrl.bus, ctrl.dev, 1, 0x70, 4, &drc);
	ctrl.mode = ((drc>>20)&1)?ECC_CORRECT:ECC_NONE;

}
*/

static void setup_amd76x(void)
{
	static const int ddim[] = { ECC_NONE, ECC_DETECT, ECC_CORRECT, ECC_CORRECT };
	unsigned long ecc_mode_status;

	/* Fill in the correct memory capabilites */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x48, 4, &ecc_mode_status);
	ctrl.cap = ECC_CORRECT;
	ctrl.mode = ddim[(ecc_mode_status >> 10)&3];
}

static void poll_amd76x(void)
{
	unsigned long ecc_mode_status;
	unsigned long bank_addr;
	unsigned long bank_info;
	unsigned long page;

	/* Read the error status */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x48, 4, &ecc_mode_status);
	/* Multibit error */
	if (ecc_mode_status & (1 << 9)) {
		/* Find the bank the error occured on */
		bank_addr = 0xC0 + (((ecc_mode_status >> 4) & 0xf) << 2);

		/* Now get the information on the erroring bank */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, bank_addr, 4, &bank_info);

		/* Parse the error location and error type */
		page = (bank_info & 0xFF800000) >> 12;

		/* Report the error */
		print_ecc_err(page, 0, 1, 0, 0);

	}
	/* Singlebit error */
	if (ecc_mode_status & (1 << 8)) {
		/* Find the bank the error occured on */
		bank_addr = 0xC0 + (((ecc_mode_status >> 0) & 0xf) << 2);

		/* Now get the information on the erroring bank */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, bank_addr, 4, &bank_info);

		/* Parse the error location and error type */
		page = (bank_info & 0xFF800000) >> 12;

		/* Report the error */
		print_ecc_err(page, 0, 0, 0, 0);

	}
	/* Clear the error status */
	if (ecc_mode_status & (3 << 8)) {
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0x48, 4, ecc_mode_status);
	}
}

static void setup_cnb20(void)
{
	/* Fill in the correct memory capabilites */
	ctrl.cap = ECC_CORRECT;

	/* FIXME add ECC error polling.  I don't have the documentation
	 * do it right now.
	 */
}

static void setup_E5400(void)
{
	unsigned long mcs;


	/* Read the hardware capabilities */
	pci_conf_read(ctrl.bus, 16, 1, 0x40, 4, &mcs);

	/* Fill in the correct memory capabilities */
	ctrl.mode = 0;
	ctrl.cap = ECC_SCRUB;

	/* Checking and correcting enabled */
	if (((mcs >> 5) & 1) == 1) {
		ctrl.mode |= ECC_CORRECT;
	}

	/* scrub enabled */
	if (((mcs >> 7) & 1) == 1) {
		ctrl.mode |= __ECC_SCRUB;
	}
}


static void setup_iE7xxx(void)
{
	unsigned long mchcfgns;
	unsigned long drc;
	unsigned long device;
	unsigned long dvnp;

	/* Read the hardare capabilities */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x52, 2, &mchcfgns);
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x7C, 4, &drc);

	/* This is a check for E7205 */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x02, 2, &device);

	/* Fill in the correct memory capabilities */
	ctrl.mode = 0;
	ctrl.cap = ECC_CORRECT;

	/* checking and correcting enabled */
	if (((drc >> 20) & 3) == 2) {
		ctrl.mode |= ECC_CORRECT;
	}

	/* E7205 doesn't support scrubbing */
	if (device != 0x255d) {
		/* scrub enabled */
		/* For E7501, valid SCRUB operations is bit 0 / D0:F0:R70-73 */
		ctrl.cap = ECC_SCRUB;
		if (mchcfgns & 1) {
			ctrl.mode |= __ECC_SCRUB;
		}

		/* Now, we can active Dev1/Fun1 */
		/* Thanks to Tyan for providing us the board to solve this */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xE0, 2, &dvnp);
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn , 0xE0, 2, (dvnp & 0xFE));

		/* Clear any routing of ECC errors to interrupts that the BIOS might have set up */
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x88, 1, 0x0);
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x8A, 1, 0x0);
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x8C, 1, 0x0);
	

	}

	/* Clear any prexisting error reports */
	pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x80, 1, 3);
	pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x82, 1, 3);


}

static void setup_iE7520(void)
{
	unsigned long mchscrb;
	unsigned long drc;
	unsigned long dvnp1;

	/* Read the hardare capabilities */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x52, 2, &mchscrb);
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x7C, 4, &drc);

	/* Fill in the correct memory capabilities */
	ctrl.mode = 0;
	ctrl.cap = ECC_CORRECT;

	/* Checking and correcting enabled */
	if (((drc >> 20) & 3) != 0) {
		ctrl.mode |= ECC_CORRECT;
	}

	/* scrub enabled */
	ctrl.cap = ECC_SCRUB;
	if ((mchscrb & 3) == 2) {
		ctrl.mode |= __ECC_SCRUB;
	}

	/* Now, we can activate Fun1 */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xF4, 1, &dvnp1);
	pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn , 0xF4, 1, (dvnp1 | 0x20));

	/* Clear any prexisting error reports */
	pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x80, 2, 0x4747);
	pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x82, 2, 0x4747);
}

static void poll_iE7xxx(void)
{
	unsigned long ferr;
	unsigned long nerr;

	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x80, 1, &ferr);
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x82, 1, &nerr);

	if (ferr & 1) {
		/* Find out about the first correctable error */
		unsigned long celog_add;
		unsigned long celog_syndrome;
		unsigned long page;

		/* Read the error location */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn +1, 0xA0, 4, &celog_add);
		/* Read the syndrome */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn +1, 0xD0, 2, &celog_syndrome);

		/* Parse the error location */
		page = (celog_add & 0x0FFFFFC0) >> 6;

		/* Report the error */
		print_ecc_err(page, 0, 1, celog_syndrome, 0);

		/* Clear Bit */
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x80, 1, ferr & 3);
	}

	if (ferr & 2) {
		/* Found out about the first uncorrectable error */
		unsigned long uccelog_add;
		unsigned long page;

		/* Read the error location */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn +1, 0xB0, 4, &uccelog_add);

		/* Parse the error location */
		page = (uccelog_add & 0x0FFFFFC0) >> 6;

		/* Report the error */
		print_ecc_err(page, 0, 0, 0, 0);

		/* Clear Bit */
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x80, 1, ferr & 3);
	}

	/* Check if DRAM_NERR contains data */
	if (nerr & 3) {
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x82, 1, nerr & 3);
	}

}

static void setup_i440gx(void)
{
	static const int ddim[] = { ECC_NONE, ECC_DETECT, ECC_CORRECT, ECC_CORRECT };
	unsigned long nbxcfg;

	/* Fill in the correct memory capabilites */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x50, 4, &nbxcfg);
	ctrl.cap = ECC_CORRECT;
	ctrl.mode = ddim[(nbxcfg >> 7)&3];
}

static void poll_i440gx(void)
{
	unsigned long errsts;
	unsigned long page;
	int bits;
	/* Read the error status */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x91, 2, &errsts);
	if (errsts & 0x11) {
		unsigned long eap;
		/* Read the error location */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x80, 4, &eap);

		/* Parse the error location and error type */
		page = (eap & 0xFFFFF000) >> 12;
		bits = 0;
		if (eap &3) {
			bits = ((eap & 3) == 1)?1:2;
		}

		if (bits) {
			/* Report the error */
			print_ecc_err(page, 0, bits==1?1:0, 0, 0);
		}

		/* Clear the error status */
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0x91, 2, 0x11);
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0x80, 4, 3);
	}

}
static void setup_i840(void)
{
	static const int ddim[] = { ECC_NONE, ECC_RESERVED, ECC_CORRECT, ECC_CORRECT };
	unsigned long mchcfg;

	/* Fill in the correct memory capabilites */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x50, 2, &mchcfg);
	ctrl.cap = ECC_CORRECT;
	ctrl.mode = ddim[(mchcfg >> 7)&3];
}

static void poll_i840(void)
{
	unsigned long errsts;
	unsigned long page;
	unsigned long syndrome;
	int channel;
	int bits;
	/* Read the error status */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, &errsts);
	if (errsts & 3) {
		unsigned long eap;
		unsigned long derrctl_sts;
		/* Read the error location */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xE4, 4, &eap);
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xE2, 2, &derrctl_sts);

		/* Parse the error location and error type */
		page = (eap & 0xFFFFF800) >> 11;
		channel = eap & 1;
		syndrome = derrctl_sts & 0xFF;
		bits = ((errsts & 3) == 1)?1:2;

		/* Report the error */
		print_ecc_err(page, 0, bits==1?1:0, syndrome, channel);

		/* Clear the error status */
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0xE2, 2, 3 << 10);
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, 3);
	}
}
static void setup_i875(void)
{

	long *ptr;
	ulong dev0, dev6 ;

	/* Fill in the correct memory capabilites */

	ctrl.cap = ECC_CORRECT;
	ctrl.mode = ECC_NONE;

	/* From my article : http://www.x86-secret.com/articles/tweak/pat/patsecrets-2.htm */
	/* Activate Device 6 */
	pci_conf_read( 0, 0, 0, 0xF4, 1, &dev0);
	pci_conf_write( 0, 0, 0, 0xF4, 1, (dev0 | 0x2));

	/* Activate Device 6 MMR */
	pci_conf_read( 0, 6, 0, 0x04, 2, &dev6);
	pci_conf_write( 0, 6, 0, 0x04, 2, (dev6 | 0x2));

	/* Read the MMR Base Address & Define the pointer*/
	pci_conf_read( 0, 6, 0, 0x10, 4, &dev6);
	ptr=(long*)(dev6+0x68);

	if (((*ptr >> 18)&1) == 1) { ctrl.mode = ECC_CORRECT; }

	/* Reseting state */
	pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2,  0x81);
}

static void setup_i925(void)
{

	// Activate MMR I/O
	ulong dev0, drc;
	unsigned long tolm;
	long *ptr;
	
	pci_conf_read( 0, 0, 0, 0x54, 4, &dev0);
	dev0 = dev0 | 0x10000000;
	pci_conf_write( 0, 0, 0, 0x54, 4, dev0);
	
	// CDH start
	pci_conf_read( 0, 0, 0, 0x44, 4, &dev0);
	if (!(dev0 & 0xFFFFC000)) {
		pci_conf_read( 0, 0, 0, 0x9C, 1, &tolm);
		pci_conf_write( 0, 0, 0, 0x47, 1, tolm & 0xF8);
	}
	// CDH end

	// ECC Checking
	ctrl.cap = ECC_CORRECT;

	dev0 &= 0xFFFFC000;
	ptr=(long*)(dev0+0x120);
	drc = *ptr & 0xFFFFFFFF;
	
	if (((drc >> 20) & 3) == 2) { 
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, 3);
		ctrl.mode = ECC_CORRECT; 
	} else { 
		ctrl.mode = ECC_NONE; 
	}

}

static void setup_p35(void)
{

	// Activate MMR I/O
	ulong dev0, capid0;
	
	pci_conf_read( 0, 0, 0, 0x48, 4, &dev0);
	if (!(dev0 & 0x1)) {
		pci_conf_write( 0, 0, 0, 0x48, 1, dev0 | 1);
	}

	// ECC Checking (No poll on X38/48 for now)
	pci_conf_read( 0, 0, 0, 0xE4, 4, &capid0);
	if ((capid0 >> 8) & 1) {
		ctrl.cap = ECC_NONE;
	} else {
		ctrl.cap = ECC_CORRECT;	
	}

	ctrl.mode = ECC_NONE; 
	
}

static void poll_i875(void)
{
	unsigned long errsts;
	unsigned long page;
	unsigned long des;
	unsigned long syndrome;
	int channel;
	int bits;
	/* Read the error status */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, &errsts);
	if (errsts & 0x81)  {
		unsigned long eap;
		unsigned long derrsyn;
		/* Read the error location, syndrome and channel */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x58, 4, &eap);
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x5C, 1, &derrsyn);
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x5D, 1, &des);

		/* Parse the error location and error type */
		page = (eap & 0xFFFFF000) >> 12;
		syndrome = derrsyn;
		channel = des & 1;
		bits = (errsts & 0x80)?0:1;

		/* Report the error */
		print_ecc_err(page, 0, bits, syndrome, channel);

		/* Clear the error status */
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2,  0x81);
	}
}

static void setup_i845(void)
{
	static const int ddim[] = { ECC_NONE, ECC_RESERVED, ECC_CORRECT, ECC_RESERVED };
	unsigned long drc;

	/* Fill in the correct memory capabilites */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x7C, 4, &drc);
	ctrl.cap = ECC_CORRECT;
	ctrl.mode = ddim[(drc >> 20)&3];
}

static void poll_i845(void)
{
	unsigned long errsts;
	unsigned long page, offset;
	unsigned long syndrome;
	int bits;
	/* Read the error status */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, &errsts);
	if (errsts & 3) {
		unsigned long eap;
		unsigned long derrsyn;
		/* Read the error location */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x8C, 4, &eap);
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x86, 1, &derrsyn);

		/* Parse the error location and error type */
		offset = (eap & 0xFE) << 4;
		page = (eap & 0x3FFFFFFE) >> 8;
		syndrome = derrsyn;
		bits = ((errsts & 3) == 1)?1:2;

		/* Report the error */
		print_ecc_err(page, offset, bits==1?1:0, syndrome, 0);

		/* Clear the error status */
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, 3);
	}
}
static void setup_i820(void)
{
	static const int ddim[] = { ECC_NONE, ECC_RESERVED, ECC_CORRECT, ECC_CORRECT };
	unsigned long mchcfg;

	/* Fill in the correct memory capabilites */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xbe, 2, &mchcfg);
	ctrl.cap = ECC_CORRECT;
	ctrl.mode = ddim[(mchcfg >> 7)&3];
}

static void poll_i820(void)
{
	unsigned long errsts;
	unsigned long page;
	unsigned long syndrome;
	int bits;
	/* Read the error status */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, &errsts);
	if (errsts & 3) {
		unsigned long eap;
		/* Read the error location */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xc4, 4, &eap);

		/* Parse the error location and error type */
		page = (eap & 0xFFFFF000) >> 4;
		syndrome = eap & 0xFF;
		bits = ((errsts & 3) == 1)?1:2;

		/* Report the error */
		print_ecc_err(page, 0, bits==1?1:0, syndrome, 0);

		/* Clear the error status */
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, 3);
	}
}

static void setup_i850(void)
{
	static const int ddim[] = { ECC_NONE, ECC_RESERVED, ECC_CORRECT, ECC_RESERVED };
	unsigned long mchcfg;

	/* Fill in the correct memory capabilites */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x50, 2, &mchcfg);
	ctrl.cap = ECC_CORRECT;
	ctrl.mode = ddim[(mchcfg >> 7)&3];
}

static void poll_i850(void)
{
	unsigned long errsts;
	unsigned long page;
	unsigned long syndrome;
	int channel;
	int bits;
	/* Read the error status */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, &errsts);
	if (errsts & 3) {
		unsigned long eap;
		unsigned long derrctl_sts;
		/* Read the error location */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xE4, 4, &eap);
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xE2, 2, &derrctl_sts);

		/* Parse the error location and error type */
		page = (eap & 0xFFFFF800) >> 11;
		channel = eap & 1;
		syndrome = derrctl_sts & 0xFF;
		bits = ((errsts & 3) == 1)?1:2;

		/* Report the error */
		print_ecc_err(page, 0, bits==1?1:0, syndrome, channel);

		/* Clear the error status */
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, errsts & 3);
	}
}

static void setup_i860(void)
{
	static const int ddim[] = { ECC_NONE, ECC_RESERVED, ECC_CORRECT, ECC_RESERVED };
	unsigned long mchcfg;
	unsigned long errsts;

	/* Fill in the correct memory capabilites */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x50, 2, &mchcfg);
	ctrl.cap = ECC_CORRECT;
	ctrl.mode = ddim[(mchcfg >> 7)&3];

	/* Clear any prexisting error reports */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, &errsts);
	pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, errsts & 3);
}

static void poll_i860(void)
{
	unsigned long errsts;
	unsigned long page;
	unsigned char syndrome;
	int channel;
	int bits;
	/* Read the error status */
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, &errsts);
	if (errsts & 3) {
		unsigned long eap;
		unsigned long derrctl_sts;
		/* Read the error location */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xE4, 4, &eap);
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xE2, 2, &derrctl_sts);

		/* Parse the error location and error type */
		page = (eap & 0xFFFFFE00) >> 9;
		channel = eap & 1;
		syndrome = derrctl_sts & 0xFF;
		bits = ((errsts & 3) == 1)?1:2;

		/* Report the error */
		print_ecc_err(page, 0, bits==1?1:0, syndrome, channel);

		/* Clear the error status */
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, errsts & 3);
	}
}

static void poll_iE7221(void)
{
	unsigned long errsts;
	unsigned long page;
	unsigned char syndrome;
	int channel;
	int bits;
	int errocc;
	
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, &errsts);
	
	errocc = errsts & 3;
	
	if ((errocc == 1) || (errocc == 2)) {
		unsigned long eap, offset;
		unsigned long derrctl_sts;		
		
		/* Read the error location */
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x58, 4, &eap);
		pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, 0x5C, 1, &derrctl_sts);		
		
		/* Parse the error location and error type */
		channel = eap & 1;
		eap = eap & 0xFFFFFF80;
		page = eap >> 12;
		offset = eap & 0xFFF;
		syndrome = derrctl_sts & 0xFF;		
		bits = errocc & 1;

		/* Report the error */
		print_ecc_err(page, offset, bits, syndrome, channel);

		/* Clear the error status */
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, errsts & 3);
	} 
	
	else if (errocc == 3) {
	
		pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn, 0xC8, 2, errsts & 3);	
	
	}
}

static void poll_iE7520(void)
{
	unsigned long ferr;
	unsigned long nerr;

	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x80, 2, &ferr);
	pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x82, 2, &nerr);

	if (ferr & 0x0101) {
			/* Find out about the first correctable error */
			unsigned long celog_add;
			unsigned long celog_syndrome;
			unsigned long page;

			/* Read the error location */
			pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn +1, 0xA0, 4,&celog_add);
			/* Read the syndrome */
			pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn +1, 0xC4, 2, &celog_syndrome);

			/* Parse the error location */
			page = (celog_add & 0x7FFFFFFC) >> 2;

			/* Report the error */
			print_ecc_err(page, 0, 1, celog_syndrome, 0);

			/* Clear Bit */
			pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x80, 2, ferr& 0x0101);
	}

	if (ferr & 0x4646) {
			/* Found out about the first uncorrectable error */
			unsigned long uccelog_add;
			unsigned long page;

			/* Read the error location */
			pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn +1, 0xA4, 4, &uccelog_add);

			/* Parse the error location */
			page = (uccelog_add & 0x7FFFFFFC) >> 2;

			/* Report the error */
			print_ecc_err(page, 0, 0, 0, 0);

			/* Clear Bit */
			pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x80, 2, ferr & 0x4646);
	}

	/* Check if DRAM_NERR contains data */
	if (nerr & 0x4747) {
			 pci_conf_write(ctrl.bus, ctrl.dev, ctrl.fn +1, 0x82, 2, nerr & 0x4747);
	}
}




/* ------------------ Here the code for FSB detection ------------------ */
/* --------------------------------------------------------------------- */

static float athloncoef[] = {11, 11.5, 12.0, 12.5, 5.0, 5.5, 6.0, 6.5, 7.0, 7.5, 8.0, 8.5, 9.0, 9.5, 10.0, 10.5};
static float athloncoef2[] = {12, 19.0, 12.0, 20.0, 13.0, 13.5, 14.0, 21.0, 15.0, 22, 16.0, 16.5, 17.0, 18.0, 23.0, 24.0};
static float p4model1ratios[] = {16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11, 12, 13, 14, 15};

static float getP4PMmultiplier(void)
{
	unsigned int msr_lo, msr_hi;
	float coef;
	/* Find multiplier (by MSR) */

	if (cpu_id.type == 6) {
		if((cpu_id.feature_flag >> 7) & 1) {
			rdmsr(0x198, msr_lo, msr_hi);
			coef = ((msr_lo >> 8) & 0x1F);
			if ((msr_lo >> 14) & 0x1) { coef = coef + 0.5f; }
		} else {
			rdmsr(0x2A, msr_lo, msr_hi);
			coef = (msr_lo >> 22) & 0x1F;
		}
	}
	else
	{
		if (cpu_id.model < 2)
		{
			rdmsr(0x2A, msr_lo, msr_hi);
			coef = (msr_lo >> 8) & 0xF;
			coef = p4model1ratios[(int)coef];
		}
		else
		{
			rdmsr(0x2C, msr_lo, msr_hi);
			coef = (msr_lo >> 24) & 0x1F;
		}
	}
	return coef;
}

static float getNHMmultiplier(void)
{
	unsigned int msr_lo, msr_hi;
	float coef;
	
	/* Find multiplier (by MSR) */
	/* First, check if Flexible Ratio is Enabled */
	rdmsr(0x194, msr_lo, msr_hi);
	if((msr_lo >> 16) & 1){
		coef = (msr_lo >> 8) & 0xFF;
	 } else {
		rdmsr(0xCE, msr_lo, msr_hi);
		coef = (msr_lo >> 8) & 0xFF;
	 }

	return coef;
}

static float getSNBmultiplier(void)
{
	unsigned int msr_lo, msr_hi;
	float coef;
	
	rdmsr(0x198, msr_lo, msr_hi);
	coef = (msr_lo >> 8) & 0xFF;
	if(coef < 4)
	{
		rdmsr(0xCE, msr_lo, msr_hi);
		coef = (msr_lo >> 16) & 0xFF;		
	}



	return coef;
}


void getIntelPNS(void)
{
	int i,j;
	long psn_eax, psn_ebx, psn_ecx, psn_edx;
	long char_hex;
	long ocpuid = 0x80000002;

	for(j = 0; j < 4; j++)
	{

		asm __volatile__(
			"pushl %%ebx\n\t" \
			"cpuid\n\t" \
			"movl %%ebx, %1\n\t" \
			"popl %%ebx\n\t" \
			: "=a" (psn_eax), "=r" (psn_ebx), "=c" (psn_ecx), "=d" (psn_edx)
			: "a" (ocpuid)
			: "cc"
		);
		

		for(i = 0; i < 4; i++)
		{
			char_hex = (psn_eax >> (i*8)) & 0xff;
			cprint(LINE_CPU+5, col + i, convert_hex_to_char(char_hex));
	
			char_hex = (psn_ebx >> (i*8)) & 0xff;
			cprint(LINE_CPU+5, col + i + 4, convert_hex_to_char(char_hex));
			
			if(psn_ecx != 0x20202020)
			{
				char_hex = (psn_ecx >> (i*8)) & 0xff;
				cprint(LINE_CPU+5, col + i + 8, convert_hex_to_char(char_hex));
							
				char_hex = (psn_edx >> (i*8)) & 0xff;
				cprint(LINE_CPU+5, col + i + 12, convert_hex_to_char(char_hex));
			}
			else 
			{
				char_hex = (psn_edx >> (i*8)) & 0xff;
				cprint(LINE_CPU+5, col + i + 8, convert_hex_to_char(char_hex));				
			}
		}
		(psn_ecx != 0x20202020)?(col += 16):(col +=12);
		if(psn_edx == 0x20202020) { col -= 4; }
		ocpuid++;
	}
	
	col -= 16;
}

static void poll_fsb_amd64(void) {

	unsigned int mcgsrl;
	unsigned int mcgsth;
	unsigned long fid, temp2;
	unsigned long dramchr;
	float clockratio;
	double dramclock;

	float coef = 10;

	/* First, got the FID by MSR */
	/* First look if Cool 'n Quiet is supported to choose the best msr */
	if (((cpu_id.pwrcap >> 1) & 1) == 1) {
		rdmsr(0xc0010042, mcgsrl, mcgsth);
		fid = (mcgsrl & 0x3F);
	} else {
		rdmsr(0xc0010015, mcgsrl, mcgsth);
		fid = ((mcgsrl >> 24)& 0x3F);
	}
	
	/* Extreme simplification. */
	coef = ( fid / 2 ) + 4.0;

	/* Support for .5 coef */
	if (fid & 1) { coef = coef + 0.5; }

	/* Next, we need the clock ratio */
	
	if (((cpu_id.ext >> 16) & 0xF) >= 4) {
	/* K8 0FH */
		pci_conf_read(0, 24, 2, 0x94, 4, &dramchr);
		temp2 = (dramchr & 0x7);
		clockratio = coef;
	
		switch (temp2) {
			case 0x0:
				clockratio = (int)(coef);
				break;
			case 0x1:
				clockratio = (int)(coef * 3.0f/4.0f);
				break;
			case 0x2:
				clockratio = (int)(coef * 3.0f/5.0f);
				break;
			case 0x3:
				clockratio = (int)(coef * 3.0f/6.0f);
				break;
			}	
	
	 } else {
	 /* OLD K8 */
		pci_conf_read(0, 24, 2, 0x94, 4, &dramchr);
		temp2 = (dramchr >> 20) & 0x7;
		clockratio = coef;
	
		switch (temp2) {
			case 0x0:
				clockratio = (int)(coef * 2.0f);
				break;
			case 0x2:
				clockratio = (int)((coef * 3.0f/2.0f) + 0.81f);
				break;
			case 0x4:
				clockratio = (int)((coef * 4.0f/3.0f) + 0.81f);
				break;
			case 0x5:
				clockratio = (int)((coef * 6.0f/5.0f) + 0.81f);
				break;
			case 0x6:
				clockratio = (int)((coef * 10.0f/9.0f) + 0.81f);
				break;
			case 0x7:
				clockratio = (int)(coef + 0.81f);
				break;
			}
	}

	/* Compute the final DRAM Clock */
	dramclock = (extclock /1000) / clockratio;

	/* ...and print */
	print_fsb_info(dramclock, "RAM : ", "DDR");

}

static void poll_fsb_k10(void) {

	unsigned int mcgsrl;
	unsigned int mcgsth;
	unsigned long temp2;
	unsigned long dramchr;
	unsigned long mainPllId;
	double dramclock;
	unsigned long pns_low;
	unsigned long pns_high;
	unsigned long  msr_psn;


		/* If ECC not enabled : display CPU name as IMC */
		if(ctrl.mode == ECC_NONE)
			{
				cprint(LINE_CPU+5, 0, "IMC : ");
				for(msr_psn = 0; msr_psn < 5; msr_psn++)
				{		
					rdmsr(0xC0010030+msr_psn, pns_low,  pns_high);
					cprint(LINE_CPU+5, 6+(msr_psn*8), convert_hex_to_char(pns_low & 0xff));
					cprint(LINE_CPU+5, 7+(msr_psn*8), convert_hex_to_char((pns_low >> 8) & 0xff));
					cprint(LINE_CPU+5, 8+(msr_psn*8), convert_hex_to_char((pns_low >> 16) & 0xff));
					cprint(LINE_CPU+5, 9+(msr_psn*8), convert_hex_to_char((pns_low >> 24) & 0xff));
					cprint(LINE_CPU+5, 10+(msr_psn*8), convert_hex_to_char(pns_high & 0xff));
					cprint(LINE_CPU+5, 11+(msr_psn*8), convert_hex_to_char((pns_high >> 8) & 0xff));
					cprint(LINE_CPU+5, 12+(msr_psn*8), convert_hex_to_char((pns_high >> 16) & 0xff));
					cprint(LINE_CPU+5, 13+(msr_psn*8), convert_hex_to_char((pns_high >> 24) & 0xff));
				}
				cprint(LINE_CPU+5, 41, "(ECC : Disabled)");
			}

		/* First, we need the clock ratio */
		pci_conf_read(0, 24, 2, 0x94, 4, &dramchr);
		temp2 = (dramchr & 0x7);

		switch (temp2) {
			case 0x7: temp2++;
			case 0x6: temp2++;
			case 0x5: temp2++;
			case 0x4: temp2++;
			default:  temp2 += 3;
		}	


	/* Compute the final DRAM Clock */
	if (((cpu_id.ext >> 20) & 0xFF) == 1)
		dramclock = ((temp2 * 200) / 3.0) + 0.25;
	else {
		unsigned long target;
		unsigned long dx;
		unsigned      divisor;


		target = temp2 * 400;

		/* Get the FID by MSR */
		rdmsr(0xc0010071, mcgsrl, mcgsth);

		pci_conf_read(0, 24, 3, 0xD4, 4, &mainPllId);

		if ( mainPllId & 0x40 )
			mainPllId &= 0x3F;
		else
			mainPllId = 8;	/* FID for 1600 */

		mcgsth = (mcgsth >> 17) & 0x3F;
		if ( mcgsth ) {
			if ( mainPllId > mcgsth )
				mainPllId = mcgsth;
		}

		dx = (mainPllId + 8) * 1200;
		for ( divisor = 3; divisor < 100; divisor++ )
			if ( (dx / divisor) <= target )
				break;

		dramclock = ((dx / divisor) / 6.0) + 0.25;
/*
 * 		dramclock = ((((dx * extclock) / divisor) / (mainPllId+8)) / 600000.0) + 0.25;
 */
}

	/* ...and print */
	print_fsb_info(dramclock, "RAM : ", "DDR");

}

static void poll_fsb_k14(void) {

	unsigned long temp2;
	unsigned long dramchr;
	double dramclock;
	unsigned long pns_low;
	unsigned long pns_high;
	unsigned long  msr_psn;


		/* If ECC not enabled : display CPU name as IMC */
		if(ctrl.mode == ECC_NONE)
			{
				cprint(LINE_CPU+5, 0, "IMC : ");
				for(msr_psn = 0; msr_psn < 5; msr_psn++)
				{		
					rdmsr(0xC0010030+msr_psn, pns_low,  pns_high);
					cprint(LINE_CPU+5, 6+(msr_psn*8), convert_hex_to_char(pns_low & 0xff));
					cprint(LINE_CPU+5, 7+(msr_psn*8), convert_hex_to_char((pns_low >> 8) & 0xff));
					cprint(LINE_CPU+5, 8+(msr_psn*8), convert_hex_to_char((pns_low >> 16) & 0xff));
					cprint(LINE_CPU+5, 9+(msr_psn*8), convert_hex_to_char((pns_low >> 24) & 0xff));
					cprint(LINE_CPU+5, 10+(msr_psn*8), convert_hex_to_char(pns_high & 0xff));
					cprint(LINE_CPU+5, 11+(msr_psn*8), convert_hex_to_char((pns_high >> 8) & 0xff));
					cprint(LINE_CPU+5, 12+(msr_psn*8), convert_hex_to_char((pns_high >> 16) & 0xff));
					cprint(LINE_CPU+5, 13+(msr_psn*8), convert_hex_to_char((pns_high >> 24) & 0xff));
				}
				cprint(LINE_CPU+5, 41, "(ECC : Disabled)");
			}

		/* First, we need the clock ratio */
		pci_conf_read(0, 24, 2, 0x94, 4, &dramchr);
		temp2 = (dramchr & 0x1F);

		switch (temp2) {
			default:
			case 6: 
				dramclock = 400; 
				break;
			case 10: 
				dramclock = 533; 
				break;
			case 14: 
				dramclock = 667; 
				break;
		}	


	/* print */
	print_fsb_info(dramclock, "RAM : ", "DDR-");

}

static void poll_fsb_i925(void) {

	double dramclock, dramratio, fsb;
	unsigned long mchcfg, mchcfg2, dev0, drc, idetect;
	float coef = getP4PMmultiplier();
	long *ptr;
	
	pci_conf_read( 0, 0, 0, 0x02, 2, &idetect);
	
	/* Find dramratio */
	pci_conf_read( 0, 0, 0, 0x44, 4, &dev0);
	dev0 = dev0 & 0xFFFFC000;
	ptr=(long*)(dev0+0xC00);
	mchcfg = *ptr & 0xFFFF;
	ptr=(long*)(dev0+0x120);
	drc = *ptr & 0xFFFF;
	dramratio = 1;

	mchcfg2 = (mchcfg >> 4)&3;
	
	if ((drc&3) != 2) {
		// We are in DDR1 Mode
		if (mchcfg2 == 1) { dramratio = 0.8; } else { dramratio = 1; }
	} else {
		// We are in DDR2 Mode
		if ((mchcfg >> 2)&1) {
			// We are in FSB1066 Mode
			if (mchcfg2 == 2) { dramratio = 0.75; } else { dramratio = 1; }
		} else {
			switch (mchcfg2) {
				case 1:
					dramratio = 0.66667;
					break;
				case 2:
					if (idetect != 0x2590) { dramratio = 1; } else { dramratio = 1.5; }
					break;
				case 3:
						// Checking for FSB533 Mode & Alviso
						if ((mchcfg & 1) == 0) { dramratio = 1.33334; }
						else if (idetect == 0x2590) { dramratio = 2; }
						else { dramratio = 1.5; }
			}
		}
	}
	// Compute RAM Frequency 
	fsb = ((extclock / 1000) / coef);
	dramclock = fsb * dramratio;

	// Print DRAM Freq 
	print_fsb_info(dramclock, "RAM : ", "DDR"); 
	
	/* Print FSB (only if ECC is not enabled) */
	cprint(LINE_CPU+5, col +1, "- FSB : ");
	col += 9;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");
	col += 4;
	
}

static void poll_fsb_i945(void) {

	double dramclock, dramratio, fsb;
	unsigned long mchcfg, dev0;
	float coef = getP4PMmultiplier();
	long *ptr;

	/* Find dramratio */
	pci_conf_read( 0, 0, 0, 0x44, 4, &dev0);
	dev0 &= 0xFFFFC000;
	ptr=(long*)(dev0+0xC00);
	mchcfg = *ptr & 0xFFFF;
	dramratio = 1;

	switch ((mchcfg >> 4)&7) {
		case 1:	dramratio = 1.0; break;
		case 2:	dramratio = 1.33334; break;
		case 3:	dramratio = 1.66667; break;
		case 4:	dramratio = 2.0; break;
	}

	// Compute RAM Frequency
	fsb = ((extclock / 1000) / coef);
	dramclock = fsb * dramratio;

	// Print DRAM Freq
	print_fsb_info(dramclock, "RAM : ", "DDR");

	/* Print FSB (only if ECC is not enabled) */
	cprint(LINE_CPU+5, col +1, "- FSB : ");
	col += 9;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");
	col += 4;

}

static void poll_fsb_i975(void) {

	double dramclock, dramratio, fsb;
	unsigned long mchcfg, dev0, fsb_mch;
	float coef = getP4PMmultiplier();
	long *ptr;

	/* Find dramratio */
	pci_conf_read( 0, 0, 0, 0x44, 4, &dev0);
	dev0 &= 0xFFFFC000;
	ptr=(long*)(dev0+0xC00);
	mchcfg = *ptr & 0xFFFF;
	dramratio = 1;

	switch (mchcfg & 7) {
		case 1: fsb_mch = 533; break;
		case 2:	fsb_mch = 800; break;
		case 3:	fsb_mch = 667; break;				
		default: fsb_mch = 1066; break;
	}


	switch (fsb_mch) {
	case 533:
		switch ((mchcfg >> 4)&7) {
			case 0:	dramratio = 1.25; break;
			case 1:	dramratio = 1.5; break;
			case 2:	dramratio = 2.0; break;
		}
		break;
		
	default:
	case 800:
		switch ((mchcfg >> 4)&7) {
			case 1:	dramratio = 1.0; break;
			case 2:	dramratio = 1.33334; break;
			case 3:	dramratio = 1.66667; break;
			case 4:	dramratio = 2.0; break;
		}
		break;

	case 1066:
		switch ((mchcfg >> 4)&7) {
			case 1:	dramratio = 0.75; break;
			case 2:	dramratio = 1.0; break;
			case 3:	dramratio = 1.25; break;
			case 4:	dramratio = 1.5; break;
		}
		break;
}


	// Compute RAM Frequency
	fsb = ((extclock / 1000) / coef);
	dramclock = fsb * dramratio;

	// Print DRAM Freq
	print_fsb_info(dramclock, "RAM : ", "DDR");

	/* Print FSB (only if ECC is not enabled) */
	cprint(LINE_CPU+5, col +1, "- FSB : ");
	col += 9;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");
	col += 4;

}

static void poll_fsb_i965(void) {

	double dramclock, dramratio, fsb;
	unsigned long mchcfg, dev0, fsb_mch;
	float coef = getP4PMmultiplier();
	long *ptr;

	/* Find dramratio */
	pci_conf_read( 0, 0, 0, 0x48, 4, &dev0);
	dev0 &= 0xFFFFC000;
	ptr=(long*)(dev0+0xC00);
	mchcfg = *ptr & 0xFFFF;
	dramratio = 1;

	switch (mchcfg & 7) {
		case 0: fsb_mch = 1066; break;
		case 1: fsb_mch = 533; break;
		default: case 2:	fsb_mch = 800; break;
		case 3:	fsb_mch = 667; break;		
		case 4: fsb_mch = 1333; break;
		case 6: fsb_mch = 1600; break;					
	}


	switch (fsb_mch) {
	case 533:
		switch ((mchcfg >> 4)&7) {
			case 1:	dramratio = 2.0; break;
			case 2:	dramratio = 2.5; break;
			case 3:	dramratio = 3.0; break;
		}
		break;
		
	default:
	case 800:
		switch ((mchcfg >> 4)&7) {
			case 0:	dramratio = 1.0; break;
			case 1:	dramratio = 5.0f/4.0f; break;
			case 2:	dramratio = 5.0f/3.0f; break;
			case 3:	dramratio = 2.0; break;
			case 4:	dramratio = 8.0f/3.0f; break;
			case 5:	dramratio = 10.0f/3.0f; break;
		}
		break;

	case 1066:
		switch ((mchcfg >> 4)&7) {
			case 1:	dramratio = 1.0f; break;
			case 2:	dramratio = 5.0f/4.0f; break;
			case 3:	dramratio = 3.0f/2.0f; break;
			case 4:	dramratio = 2.0f; break;
			case 5:	dramratio = 5.0f/2.0f; break;
		}
		break;
	
	case 1333:
		switch ((mchcfg >> 4)&7) {
			case 2:	dramratio = 1.0f; break;
			case 3:	dramratio = 6.0f/5.0f; break;
			case 4:	dramratio = 8.0f/5.0f; break;
			case 5:	dramratio = 2.0f; break;
		}
		break;

	case 1600:
		switch ((mchcfg >> 4)&7) {
			case 3:	dramratio = 1.0f; break;
			case 4:	dramratio = 4.0f/3.0f; break;
			case 5:	dramratio = 3.0f/2.0f; break;
			case 6:	dramratio = 2.0f; break;
		}
		break;

}

	// Compute RAM Frequency
	fsb = ((extclock / 1000) / coef);
	dramclock = fsb * dramratio;

	// Print DRAM Freq
	print_fsb_info(dramclock, "RAM : ", "DDR");

	/* Print FSB (only if ECC is not enabled) */
	cprint(LINE_CPU+5, col +1, "- FSB : ");
	col += 9;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");
	col += 4;

}

static void poll_fsb_im965(void) {

	double dramclock, dramratio, fsb;
	unsigned long mchcfg, dev0, fsb_mch;
	float coef = getP4PMmultiplier();
	long *ptr;

	/* Find dramratio */
	pci_conf_read( 0, 0, 0, 0x48, 4, &dev0);
	dev0 &= 0xFFFFC000;
	ptr=(long*)(dev0+0xC00);
	mchcfg = *ptr & 0xFFFF;
	dramratio = 1;

	switch (mchcfg & 7) {
		case 1: fsb_mch = 533; break;
		default: case 2:	fsb_mch = 800; break;
		case 3:	fsb_mch = 667; break;				
		case 6:	fsb_mch = 1066; break;			
	}


	switch (fsb_mch) {
	case 533:
		switch ((mchcfg >> 4)&7) {
			case 1:	dramratio = 5.0f/4.0f; break;
			case 2:	dramratio = 3.0f/2.0f; break;
			case 3:	dramratio = 2.0f; break;
		}
		break;

	case 667:
		switch ((mchcfg >> 4)&7) {
			case 1:	dramratio = 1.0f; break;
			case 2:	dramratio = 6.0f/5.0f; break;
			case 3:	dramratio = 8.0f/5.0f; break;
			case 4:	dramratio = 2.0f; break;
			case 5:	dramratio = 12.0f/5.0f; break;
		}
		break;
	default:
	case 800:
		switch ((mchcfg >> 4)&7) {
			case 1:	dramratio = 5.0f/6.0f; break;
			case 2:	dramratio = 1.0f; break;
			case 3:	dramratio = 4.0f/3.0f; break;
			case 4:	dramratio = 5.0f/3.0f; break;
			case 5:	dramratio = 2.0f; break;
		}
		break;
	case 1066:
		switch ((mchcfg >> 4)&7) {
			case 5:	dramratio = 3.0f/2.0f; break;
			case 6:	dramratio = 2.0f; break;
		}
		break;
}

	// Compute RAM Frequency
	fsb = ((extclock / 1000) / coef);
	dramclock = fsb * dramratio;

	// Print DRAM Freq
	print_fsb_info(dramclock, "RAM : ", "DDR");

	/* Print FSB (only if ECC is not enabled) */
	cprint(LINE_CPU+5, col +1, "- FSB : ");
	col += 9;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");
	col += 4;

}


static void poll_fsb_5400(void) {

	double dramclock, dramratio, fsb;
	unsigned long ambase_low, ambase_high, ddrfrq;
	float coef = getP4PMmultiplier();

	/* Find dramratio */
	pci_conf_read( 0, 16, 0, 0x48, 4, &ambase_low);
	ambase_low &= 0xFFFE0000;
	pci_conf_read( 0, 16, 0, 0x4C, 4, &ambase_high);
	ambase_high &= 0xFF;
	pci_conf_read( 0, 16, 1, 0x56, 1, &ddrfrq);
  ddrfrq &= 7;
  dramratio = 1;

	switch (ddrfrq) {
			case 0:	
			case 1:	
			case 4:					
				dramratio = 1.0; 
				break;
			case 2:	
				dramratio = 5.0f/4.0f; 
				break;
			case 3:	
			case 7:	
				dramratio = 4.0f/5.0f; 
				break;
		}


	// Compute RAM Frequency
	fsb = ((extclock / 1000) / coef);
	dramclock = fsb * dramratio;

	// Print DRAM Freq
	print_fsb_info(dramclock, "RAM : ", "DDR");

	/* Print FSB (only if ECC is not enabled) */
	cprint(LINE_CPU+5, col +1, "- FSB : ");
	col += 9;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");
	col += 4;

}


static void poll_fsb_nf4ie(void) {

	double dramclock, dramratio, fsb;
	float mratio, nratio;
	unsigned long reg74, reg60;
	float coef = getP4PMmultiplier();
	
	/* Find dramratio */
	pci_conf_read(0, 0, 2, 0x74, 2, &reg74);
	pci_conf_read(0, 0, 2, 0x60, 4, &reg60);
	mratio = reg74 & 0xF;
	nratio = (reg74 >> 4) & 0xF;

	// If M or N = 0, then M or N = 16
	if (mratio == 0) { mratio = 16; }
	if (nratio == 0) { nratio = 16; }
	
	// Check if synchro or pseudo-synchro mode
	if((reg60 >> 22) & 1) {
		dramratio = 1;
	} else {
		dramratio = nratio / mratio;
	}

	/* Compute RAM Frequency */
	fsb = ((extclock /1000) / coef);
	dramclock = fsb * dramratio;

	/* Print DRAM Freq */
	print_fsb_info(dramclock, "RAM : ", "DDR");

	/* Print FSB  */
	cprint(LINE_CPU+5, col, "- FSB : ");
	col += 9;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");
	col += 4;
	
}

static void poll_fsb_i875(void) {

	double dramclock, dramratio, fsb;
	unsigned long mchcfg, smfs;
	float coef = getP4PMmultiplier();

	/* Find dramratio */
	pci_conf_read(0, 0, 0, 0xC6, 2, &mchcfg);
	smfs = (mchcfg >> 10)&3;
	dramratio = 1;

	if ((mchcfg&3) == 3) { dramratio = 1; }
	if ((mchcfg&3) == 2) {
		if (smfs == 2) { dramratio = 1; }
		if (smfs == 1) { dramratio = 1.25; }
		if (smfs == 0) { dramratio = 1.5; }
	}
	if ((mchcfg&3) == 1) {
		if (smfs == 2) { dramratio = 0.6666666666; }
		if (smfs == 1) { dramratio = 0.8; }
		if (smfs == 0) { dramratio = 1; }
	}
	if ((mchcfg&3) == 0) { dramratio = 0.75; }


	/* Compute RAM Frequency */
	dramclock = ((extclock /1000) / coef) / dramratio;
	fsb = ((extclock /1000) / coef);

	/* Print DRAM Freq */
	print_fsb_info(dramclock, "RAM : ", "DDR");

	/* Print FSB (only if ECC is not enabled) */
	if ( ctrl.mode == ECC_NONE ) {
		cprint(LINE_CPU+5, col +1, "- FSB : ");
		col += 9;
		dprint(LINE_CPU+5, col, fsb, 3,0);
		col += 3;
		cprint(LINE_CPU+5, col +1, "MHz");
		col += 4;
	}
}

static void poll_fsb_p4(void) {

	ulong fsb, idetect;
	float coef = getP4PMmultiplier();

	fsb = ((extclock /1000) / coef);

	/* Print FSB */
	cprint(LINE_CPU+5, col +1, "/ FSB : ");
	col += 9;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");
	col += 4;

	/* For synchro only chipsets */
	pci_conf_read( 0, 0, 0, 0x02, 2, &idetect);
	if (idetect == 0x2540 || idetect == 0x254C) {
		print_fsb_info(fsb, "RAM : ", "DDR");
	}
}

static void poll_fsb_i855(void) {


	double dramclock, dramratio, fsb ;
	unsigned int msr_lo, msr_hi;
	ulong mchcfg, centri, idetect;
	int coef;

	pci_conf_read( 0, 0, 0, 0x02, 2, &idetect);

	/* Find multiplier (by MSR) */

	/* Is it a Pentium M ? */
	if (cpu_id.type == 6) {
		rdmsr(0x2A, msr_lo, msr_hi);
		coef = (msr_lo >> 22) & 0x1F;

		/* Is it an i855GM or PM ? */
		if (idetect == 0x3580) {
			cprint(LINE_CPU+5, col-1, "i855GM/GME ");
			col += 10;
		}
	} else {
		rdmsr(0x2C, msr_lo, msr_hi);
		coef = (msr_lo >> 24) & 0x1F;
		cprint(LINE_CPU+5, col-1, "i852PM/GM ");
		col += 9;
	}

	fsb = ((extclock /1000) / coef);

	/* Print FSB */
	cprint(LINE_CPU+5, col, "/ FSB : ");	col += 8;
	dprint(LINE_CPU+5, col, fsb, 3,0);	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");	col += 4;

	/* Is it a Centrino platform or only an i855 platform ? */
	pci_conf_read( 2, 2, 0, 0x02, 2, &centri);
	if (centri == 0x1043) {	cprint(LINE_CPU+5, col +1, "/ Centrino Mobile Platform"); }
	else { cprint(LINE_CPU+5, col +1, "/ Mobile Platform"); }

	/* Compute DRAM Clock */

	dramratio = 1;
	if (idetect == 0x3580) {
		pci_conf_read( 0, 0, 3, 0xC0, 2, &mchcfg);
		mchcfg = mchcfg & 0x7;

		if (mchcfg == 1 || mchcfg == 2 || mchcfg == 4 || mchcfg == 5) {	dramratio = 1; }
		if (mchcfg == 0 || mchcfg == 3) { dramratio = 1.333333333; }
		if (mchcfg == 6) { dramratio = 1.25; }
		if (mchcfg == 7) { dramratio = 1.666666667; }

	} else {
		pci_conf_read( 0, 0, 0, 0xC6, 2, &mchcfg);
		if (((mchcfg >> 10)&3) == 0) { dramratio = 1; }
		else if (((mchcfg >> 10)&3) == 1) { dramratio = 1.666667; }
		else { dramratio = 1.333333333; }
	}


	dramclock = fsb * dramratio;

	/* ...and print */
	print_fsb_info(dramclock, "RAM : ", "DDR");

}

static void poll_fsb_amd32(void) {

	unsigned int mcgsrl;
	unsigned int mcgsth;
	unsigned long temp;
	double dramclock;
	double coef2;

	/* First, got the FID */
	rdmsr(0x0c0010015, mcgsrl, mcgsth);
	temp = (mcgsrl >> 24)&0x0F;

	if ((mcgsrl >> 19)&1) { coef2 = athloncoef2[temp]; }
	else { coef2 = athloncoef[temp]; }

	if (coef2 == 0) { coef2 = 1; };

	/* Compute the final FSB Clock */
	dramclock = (extclock /1000) / coef2;

	/* ...and print */
	print_fsb_info(dramclock, "FSB : ", "DDR");

}

static void poll_fsb_nf2(void) {

	unsigned int mcgsrl;
	unsigned int mcgsth;
	unsigned long temp, mempll;
	double dramclock, fsb;
	double mem_m, mem_n;
	float coef;
	coef = 10;

	/* First, got the FID */
	rdmsr(0x0c0010015, mcgsrl, mcgsth);
	temp = (mcgsrl >> 24)&0x0F;

	if ((mcgsrl >> 19)&1) { coef = athloncoef2[temp]; }
	else { coef = athloncoef[temp]; }

	/* Get the coef (COEF = N/M) - Here is for Crush17 */
	pci_conf_read(0, 0, 3, 0x70, 4, &mempll);
	mem_m = (mempll&0x0F);
	mem_n = ((mempll >> 4) & 0x0F);

	/* If something goes wrong, the chipset is probably a Crush18 */
	if ( mem_m == 0 || mem_n == 0 ) {
		pci_conf_read(0, 0, 3, 0x7C, 4, &mempll);
		mem_m = (mempll&0x0F);
		mem_n = ((mempll >> 4) & 0x0F);
	}

	/* Compute the final FSB Clock */
	dramclock = ((extclock /1000) / coef) * (mem_n/mem_m);
	fsb = ((extclock /1000) / coef);

	/* ...and print */

	cprint(LINE_CPU+5, col, "/ FSB : ");
	col += 8;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");

	print_fsb_info(dramclock, "RAM : ", "DDR");

}

static void poll_fsb_us15w(void) {

	double dramclock, dramratio, fsb, gfx;
	unsigned long msr;

	/* Find dramratio */
	/* D0 MsgRd, 05 Zunit, 03 MSR */
	pci_conf_write(0, 0, 0, 0xD0, 4, 0xD0050300 );		
	pci_conf_read(0, 0, 0, 0xD4, 4, &msr );		
	fsb = ( msr >> 3 ) & 1;

	dramratio = 0.5; 

	// Compute RAM Frequency
	if (( msr >> 3 ) & 1) {
		fsb = 533;
	} else {
		fsb = 400;
	}

	switch (( msr >> 0 ) & 7) {
		case 0:
			gfx = 100;
			break;
		case 1:
			gfx = 133;
			break;
		case 2:
			gfx = 150;
			break;
		case 3:
			gfx = 178;
			break;
		case 4:
			gfx = 200;
			break;
		case 5:
			gfx = 266;
			break;
		default:
			gfx = 0;
			break;
	}	
	
	dramclock = fsb * dramratio;

	// Print DRAM Freq
	print_fsb_info(dramclock, "RAM : ", "DDR");

	/* Print FSB (only if ECC is not enabled) */
	cprint(LINE_CPU+4, col +1, "- FSB : ");
	col += 9;
	dprint(LINE_CPU+4, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+4, col +1, "MHz");
	col += 4;

	cprint(LINE_CPU+4, col +1, "- GFX : ");
	col += 9;
	dprint(LINE_CPU+4, col, gfx, 3,0);
	col += 3;
	cprint(LINE_CPU+4, col +1, "MHz");
	col += 4;

}

static void poll_fsb_nhm(void) {

	double dramclock, dramratio, fsb;
	unsigned long mc_dimm_clk_ratio, qpi_pll_status;
	float coef = getNHMmultiplier();
	float qpi_speed;

	fsb = ((extclock /1000) / coef);

	/* Print FSB */
	cprint(LINE_CPU+5, col +1, "/ BCLK : ");
	col += 10;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");
	col += 4;
	
	/* Print QPI Speed (if ECC not supported) */
	if(ctrl.mode == ECC_NONE && cpu_id.model == 10) {
		pci_conf_read(nhm_bus, 2, 1, 0x50, 2, &qpi_pll_status);
		qpi_speed = (qpi_pll_status & 0x7F) * ((extclock / 1000) / coef) * 2;
		cprint(LINE_CPU+5, col +1, "/ QPI : ");
		col += 9;
		dprint(LINE_CPU+5, col, qpi_speed/1000, 1,0);
		col += 1;
		cprint(LINE_CPU+5, col, ".");
		col += 1;		
		qpi_speed = ((qpi_speed / 1000) - (int)(qpi_speed / 1000)) * 10;
		dprint(LINE_CPU+5, col, qpi_speed, 1,0);
		col += 1;		
		cprint(LINE_CPU+5, col +1, "GT/s");
		col += 5;	
	}
	
	/* Get the clock ratio */
	
	pci_conf_read(nhm_bus, 3, 4, 0x54, 2, &mc_dimm_clk_ratio);
	dramratio = (mc_dimm_clk_ratio & 0x1F);
	
	// Compute RAM Frequency
	fsb = ((extclock / 1000) / coef);
	dramclock = fsb * dramratio / 2;

	// Print DRAM Freq
	print_fsb_info(dramclock, "RAM : ", "DDR3-");

}

static void poll_fsb_nhm32(void) {

	double dramclock, dramratio, fsb;
	unsigned long mc_dimm_clk_ratio, qpi_pll_status;
	float coef = getNHMmultiplier();
	float qpi_speed;

	fsb = ((extclock /1000) / coef);

	/* Print FSB */
	cprint(LINE_CPU+5, col +1, "/ BCLK : ");
	col += 10;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");
	col += 4;
	
	/* Print QPI Speed (if ECC not supported) */
	if(ctrl.mode == ECC_NONE && cpu_id.model == 12) {
		pci_conf_read(nhm_bus, 2, 1, 0x50, 2, &qpi_pll_status);
		qpi_speed = (qpi_pll_status & 0x7F) * ((extclock / 1000) / coef) * 2;
		cprint(LINE_CPU+5, col +1, "/ QPI : ");
		col += 9;
		dprint(LINE_CPU+5, col, qpi_speed/1000, 1,0);
		col += 1;
		cprint(LINE_CPU+5, col, ".");
		col += 1;		
		qpi_speed = ((qpi_speed / 1000) - (int)(qpi_speed / 1000)) * 10;
		dprint(LINE_CPU+5, col, qpi_speed, 1,0);
		col += 1;		
		cprint(LINE_CPU+5, col +1, "GT/s");
		col += 5;	
	}
	
	/* Get the clock ratio */
	
	pci_conf_read(nhm_bus, 3, 4, 0x50, 2, &mc_dimm_clk_ratio);
	dramratio = (mc_dimm_clk_ratio & 0x1F);
	
	// Compute RAM Frequency
	fsb = ((extclock / 1000) / coef);
	dramclock = fsb * dramratio / 2;

	// Print DRAM Freq
	print_fsb_info(dramclock, "RAM : ", "DDR3-");

}

static void poll_fsb_wmr(void) {

	double dramclock, dramratio, fsb;
	unsigned long dev0, mchcfg;
	float coef = getNHMmultiplier();
	long *ptr;
	
	fsb = ((extclock / 1000) / coef);

	if(ctrl.mode == ECC_NONE)
		{
			col = 0;
			cprint(LINE_CPU+5, col, "IMC : "); col += 6;
			getIntelPNS();	
			//cprint(LINE_CPU+5, col, "(ECC : Disabled)");
			//col += 16;
		}

	/* Print FSB */
	cprint(LINE_CPU+5, col +1, "/ BCLK : ");
	col += 10;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");
	col += 4;

	/* Find dramratio */
	pci_conf_read( 0, 0, 0, 0x48, 4, &dev0);
	dev0 &= 0xFFFFC000;
	ptr=(long*)(dev0+0x2C20);
	mchcfg = *ptr & 0xFFFF;
	dramratio = 1;
	
	/* Get the clock ratio */
	dramratio = 0.25 * (float)(*ptr & 0x1F);
	
	// Compute RAM Frequency
	dramclock = fsb * dramratio;

	// Print DRAM Freq
	print_fsb_info(dramclock, "RAM : ", "DDR3-");

}

static void poll_fsb_snb(void) {

	double dramclock, dramratio, fsb;
	unsigned long dev0, mchcfg;
	float coef = getSNBmultiplier();
	long *ptr;
	
	fsb = ((extclock / 1000) / coef);

	if(ctrl.mode == ECC_NONE)
		{
			col = 0;
			cprint(LINE_CPU+5, col, "IMC : "); col += 6;
			getIntelPNS();	
			//cprint(LINE_CPU+5, col, "(ECC : Disabled)");
			//col += 16;
		}

	/* Print FSB */
	cprint(LINE_CPU+5, col +1, "/ BCLK : ");
	col += 10;
	dprint(LINE_CPU+5, col, fsb, 3,0);
	col += 3;
	cprint(LINE_CPU+5, col +1, "MHz");
	col += 4;

	/* Find dramratio */
	pci_conf_read( 0, 0, 0, 0x48, 4, &dev0);
	dev0 &= 0xFFFFC000;
	ptr=(long*)(dev0+0x5E04);
	mchcfg = *ptr & 0xFFFF;
	dramratio = 1;
	
	/* Get the clock ratio */
	dramratio = (float)(*ptr & 0x1F) * (133.34f / 100.0f);
	
	// Compute RAM Frequency
	dramclock = fsb * dramratio;

	// Print DRAM Freq
	print_fsb_info(dramclock, "RAM : ", "DDR3-");

}

/* ------------------ Here the code for Timings detection ------------------ */
/* ------------------------------------------------------------------------- */

static void poll_timings_nf4ie(void) {


	ulong regd0, reg8c, reg9c, reg80;
	int cas, rcd, rp, ras;

	cprint(LINE_CPU+5, col +1, "- Type : DDR-II");

	//Now, read Registers
	pci_conf_read( 0, 1, 1, 0xD0, 4, &regd0);
	pci_conf_read( 0, 1, 1, 0x80, 1, &reg80);
	pci_conf_read( 0, 1, 0, 0x8C, 4, &reg8c);
	pci_conf_read( 0, 1, 0, 0x9C, 4, &reg9c);

	// Then, detect timings
	cas = (regd0 >> 4) & 0x7;
	rcd = (reg8c >> 24) & 0xF;
	rp = (reg9c >> 8) & 0xF;
	ras = (reg8c >> 16) & 0x3F;
	
	print_timings_info(cas, rcd, rp, ras);
	
	if (reg80 & 0x3) {
		cprint(LINE_CPU+6, col2, "/ Dual Channel (128 bits)");
	} else {
		cprint(LINE_CPU+6, col2, "/ Single Channel (64 bits)");
	}

}

static void poll_timings_i875(void) {

	ulong dev6, dev62;
	ulong temp;
	float cas;
	int rcd, rp, ras;
	long *ptr, *ptr2;

	/* Read the MMR Base Address & Define the pointer */
	pci_conf_read( 0, 6, 0, 0x10, 4, &dev6);

	/* Now, the PAT ritual ! (Kant and Luciano will love this) */
	pci_conf_read( 0, 6, 0, 0x40, 4, &dev62);
	ptr2=(long*)(dev6+0x68);

	if ((dev62&0x3) == 0 && ((*ptr2 >> 14)&1) == 1) {
		cprint(LINE_CPU+5, col +1, "- PAT : Enabled");
	} else {
		cprint(LINE_CPU+5, col +1, "- PAT : Disabled");
	}

	/* Now, we could check some additionnals timings infos) */

	ptr=(long*)(dev6+0x60);
	// CAS Latency (tCAS)
	temp = ((*ptr >> 5)& 0x3);
	if (temp == 0x0) { cas = 2.5; } else if (temp == 0x1) { cas = 2; } else { cas = 3; }

	// RAS-To-CAS (tRCD)
	temp = ((*ptr >> 2)& 0x3);
	if (temp == 0x0) { rcd = 4; } else if (temp == 0x1) { rcd = 3; } else { rcd = 2; }

	// RAS Precharge (tRP)
	temp = (*ptr&0x3);
	if (temp == 0x0) { rp = 4; } else if (temp == 0x1) { rp = 3; } else { rp = 2; }

	// RAS Active to precharge (tRAS)
	temp = ((*ptr >> 7)& 0x7);
	ras = 10 - temp;

	// Print timings
	print_timings_info(cas, rcd, rp, ras);

	// Print 64 or 128 bits mode
	if (((*ptr2 >> 21)&3) > 0) { 
		cprint(LINE_CPU+6, col2, "/ Dual Channel (128 bits)");
	} else {
		cprint(LINE_CPU+6, col2, "/ Single Channel (64 bits)");
	}
}

static void poll_timings_i925(void) {

	// Thanks for CDH optis
	ulong dev0, drt, drc, dcc, idetect, temp;
	long *ptr;

	//Now, read MMR Base Address
	pci_conf_read( 0, 0, 0, 0x44, 4, &dev0);
	pci_conf_read( 0, 0, 0, 0x02, 2, &idetect);
	dev0 &= 0xFFFFC000;

	//Set pointer for DRT
	ptr=(long*)(dev0+0x114);
	drt = *ptr & 0xFFFFFFFF;

	//Set pointer for DRC
	ptr=(long*)(dev0+0x120);
	drc = *ptr & 0xFFFFFFFF;

	//Set pointer for DCC
	ptr=(long*)(dev0+0x200);
	dcc = *ptr & 0xFFFFFFFF;

	//Determine DDR or DDR-II
	if ((drc & 3) == 2) {
		cprint(LINE_CPU+5, col +1, "- Type : DDR2");
	} else {
		cprint(LINE_CPU+5, col +1, "- Type : DDR1");
	}

	// Now, detect timings
	cprint(LINE_CPU+6, col2 +1, "/ CAS : ");
	col2 += 9;

	// CAS Latency (tCAS)
	temp = ((drt >> 8)& 0x3);

	if ((drc & 3) == 2){
		// Timings DDR-II
		if      (temp == 0x0) { cprint(LINE_CPU+6, col2, "5-"); }
		else if (temp == 0x1) { cprint(LINE_CPU+6, col2, "4-"); }
		else if (temp == 0x2) { cprint(LINE_CPU+6, col2, "3-"); }
		else		      { cprint(LINE_CPU+6, col2, "6-"); }
	} else {
		// Timings DDR-I
		if      (temp == 0x0) { cprint(LINE_CPU+6, col2, "3-"); }
		else if (temp == 0x1) { cprint(LINE_CPU+6, col2, "2.5-"); col2 +=2;}
		else		      { cprint(LINE_CPU+6, col2, "2-"); }
	}
	col2 +=2;

	// RAS-To-CAS (tRCD)
	dprint(LINE_CPU+6, col2, ((drt >> 4)& 0x3)+2, 1 ,0);
	cprint(LINE_CPU+6, col2+1, "-");
	col2 +=2;

	// RAS Precharge (tRP)
	dprint(LINE_CPU+6, col2, (drt&0x3)+2, 1 ,0);
	cprint(LINE_CPU+6, col2+1, "-");
	col2 +=2;

	// RAS Active to precharge (tRAS)
	// If Lakeport, than change tRAS computation (Thanks to CDH, again)
	if (idetect > 0x2700)
		temp = ((drt >> 19)& 0x1F);
	else
		temp = ((drt >> 20)& 0x0F);

	dprint(LINE_CPU+6, col2, temp , 1 ,0);
	(temp < 10)?(col2 += 1):(col2 += 2);

	cprint(LINE_CPU+6, col2+1, "/"); col2 +=2;

	temp = (dcc&0x3);
	if      (temp == 1) { cprint(LINE_CPU+6, col2, " Dual Channel (Asymmetric)"); }
	else if (temp == 2) { cprint(LINE_CPU+6, col2, " Dual Channel (Interleaved)"); }
	else		    { cprint(LINE_CPU+6, col2, " Single Channel (64 bits)"); }

}

static void poll_timings_i965(void) {

	// Thanks for CDH optis
	ulong dev0, temp, c0ckectrl, c1ckectrl, offset;
	ulong ODT_Control_Register, Precharge_Register, ACT_Register, Read_Register, Misc_Register;
	long *ptr;

	//Now, read MMR Base Address
	pci_conf_read( 0, 0, 0, 0x48, 4, &dev0);
	dev0 &= 0xFFFFC000;

	ptr = (long*)(dev0+0x260);
	c0ckectrl = *ptr & 0xFFFFFFFF;	

	ptr = (long*)(dev0+0x660);
	c1ckectrl = *ptr & 0xFFFFFFFF;
	
	// If DIMM 0 not populated, check DIMM 1
	((c0ckectrl) >> 20 & 0xF)?(offset = 0):(offset = 0x400);

	ptr = (long*)(dev0+offset+0x29C);
	ODT_Control_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x250);	
	Precharge_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x252);
	ACT_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x258);
	Read_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x244);
	Misc_Register = *ptr & 0xFFFFFFFF;

	//Intel 965 Series only support DDR2
	cprint(LINE_CPU+5, col +1, "- Type : DDR-II");

	// Now, detect timings
	cprint(LINE_CPU+6, col2 +1, "/ CAS : ");
	col2 += 9;

	// CAS Latency (tCAS)
	temp = ((ODT_Control_Register >> 17)& 7) + 3.0f;
	dprint(LINE_CPU+6, col2, temp, 1 ,0);
	cprint(LINE_CPU+6, col2+1, "-");
	(temp < 10)?(col2 += 2):(col2 += 3);

	// RAS-To-CAS (tRCD)
	temp = (Read_Register >> 16) & 0xF;
	dprint(LINE_CPU+6, col2, temp, 1 ,0);
	cprint(LINE_CPU+6, col2+1, "-");
	(temp < 10)?(col2 += 2):(col2 += 3);

	// RAS Precharge (tRP)
	temp = (ACT_Register >> 13) & 0xF;
	dprint(LINE_CPU+6, col2, temp, 1 ,0);
	cprint(LINE_CPU+6, col2+1, "-");
	(temp < 10)?(col2 += 2):(col2 += 3);

	// RAS Active to precharge (tRAS)
	temp = (Precharge_Register >> 11) & 0x1F;
	dprint(LINE_CPU+6, col2, temp, 1 ,0);
	(temp < 10)?(col2 += 1):(col2 += 2);

	cprint(LINE_CPU+6, col2+1, "/"); col2 +=2;

	if ((c0ckectrl >> 20 & 0xF) && (c1ckectrl >> 20 & 0xF)) { 
		cprint(LINE_CPU+6, col2+1, "Dual Channel"); 
	}	else {
		cprint(LINE_CPU+6, col2+1, "Single Channel"); 
	}

}

static void poll_timings_im965(void) {

	// Thanks for CDH optis
	ulong dev0, temp, c0ckectrl, c1ckectrl, offset;
	ulong ODT_Control_Register, Precharge_Register;
	long *ptr;

	//Now, read MMR Base Address
	pci_conf_read( 0, 0, 0, 0x48, 4, &dev0);
	dev0 &= 0xFFFFC000;

	ptr = (long*)(dev0+0x1200);
	c0ckectrl = *ptr & 0xFFFFFFFF;	

	ptr = (long*)(dev0+0x1300);
	c1ckectrl = *ptr & 0xFFFFFFFF;
	
	// If DIMM 0 not populated, check DIMM 1
	((c0ckectrl) >> 20 & 0xF)?(offset = 0):(offset = 0x100);

	ptr = (long*)(dev0+offset+0x121C);
	ODT_Control_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x1214);	
	Precharge_Register = *ptr & 0xFFFFFFFF;

	//Intel 965 Series only support DDR2
	cprint(LINE_CPU+5, col+1, "- Type : DDR-II");

	// Now, detect timings
	cprint(LINE_CPU+6, col2 +1, "/ CAS : ");
	col2 += 9;

	// CAS Latency (tCAS)
	temp = ((ODT_Control_Register >> 23)& 7) + 3.0f;
	dprint(LINE_CPU+6, col2, temp, 1 ,0);
	cprint(LINE_CPU+6, col2+1, "-");
	(temp < 10)?(col2 += 2):(col2 += 3);

	// RAS-To-CAS (tRCD)
	temp = ((Precharge_Register >> 5)& 7) + 2.0f;
	dprint(LINE_CPU+6, col2, temp, 1 ,0);
	cprint(LINE_CPU+6, col2+1, "-");
	(temp < 10)?(col2 += 2):(col2 += 3);

	// RAS Precharge (tRP)
	temp = (Precharge_Register & 7) + 2.0f;
	dprint(LINE_CPU+6, col2, temp, 1 ,0);
	cprint(LINE_CPU+6, col2+1, "-");
	(temp < 10)?(col2 += 2):(col2 += 3);

	// RAS Active to precharge (tRAS)
	temp = (Precharge_Register >> 21) & 0x1F;
	dprint(LINE_CPU+6, col2, temp, 1 ,0);
	(temp < 10)?(col2 += 1):(col2 += 2);

	cprint(LINE_CPU+6, col2+1, "/"); col2 +=2;

	if ((c0ckectrl >> 20 & 0xF) && (c1ckectrl >> 20 & 0xF)) { 
		cprint(LINE_CPU+6, col2+1, "Dual Channel"); 
	}	else {
		cprint(LINE_CPU+6, col2+1, "Single Channel"); 
	}

}

static void poll_timings_p35(void) {

	// Thanks for CDH optis
	float cas;
	int rcd, rp, ras;
	ulong dev0, Device_ID, Memory_Check,	c0ckectrl, c1ckectrl, offset;
	ulong ODT_Control_Register, Precharge_Register, ACT_Register, Read_Register, Misc_Register;
	long *ptr;

	pci_conf_read( 0, 0, 0, 0x02, 2, &Device_ID);
	Device_ID &= 0xFFFF;

	//Now, read MMR Base Address
	pci_conf_read( 0, 0, 0, 0x48, 4, &dev0);
	dev0 &= 0xFFFFC000;

	ptr = (long*)(dev0+0x260);
	c0ckectrl = *ptr & 0xFFFFFFFF;	

	ptr = (long*)(dev0+0x660);
	c1ckectrl = *ptr & 0xFFFFFFFF;
	
	// If DIMM 0 not populated, check DIMM 1
	((c0ckectrl) >> 20 & 0xF)?(offset = 0):(offset = 0x400);

	ptr = (long*)(dev0+offset+0x265);
	ODT_Control_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x25D);	
	Precharge_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x252);
	ACT_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x258);
	Read_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x244);
	Misc_Register = *ptr & 0xFFFFFFFF;

	// On P45, check 1A8
	if(Device_ID > 0x2E00) {
		ptr = (long*)(dev0+offset+0x1A8);
		Memory_Check = *ptr & 0xFFFFFFFF;	
		Memory_Check >>= 2;
		Memory_Check &= 1;
		Memory_Check = !Memory_Check;
	} else {
		ptr = (long*)(dev0+offset+0x1E8);
		Memory_Check = *ptr & 0xFFFFFFFF;		
	}

	//Determine DDR-II or DDR-III
	if (Memory_Check & 1) {
		cprint(LINE_CPU+5, col +1, "- Type : DDR2");
	} else {
		cprint(LINE_CPU+5, col +1, "- Type : DDR3");
	}

	// CAS Latency (tCAS)
	if(Device_ID > 0x2E00) {
		cas = ((ODT_Control_Register >> 8)& 0x3F) - 6.0f;
	} else {
		cas = ((ODT_Control_Register >> 8)& 0x3F) - 9.0f;
	}

	// RAS-To-CAS (tRCD)
	rcd = (Read_Register >> 17) & 0xF;

	// RAS Precharge (tRP)
	rp = (ACT_Register >> 13) & 0xF;

	// RAS Active to precharge (tRAS)
	ras = Precharge_Register & 0x3F;
	
	print_timings_info(cas, rcd, rp, ras);

	cprint(LINE_CPU+6, col2+1, "/"); col2 +=2;

	if ((c0ckectrl >> 20 & 0xF) && (c1ckectrl >> 20 & 0xF)) { 
		cprint(LINE_CPU+6, col2+1, "Dual Channel"); 
	}	else {
		cprint(LINE_CPU+6, col2+1, "Single Channel"); 
	}

}

static void poll_timings_wmr(void) {

	float cas;
	int rcd, rp, ras;
	ulong dev0, c0ckectrl, c1ckectrl, offset;
	ulong ODT_Control_Register, Precharge_Register, ACT_Register, Read_Register, MRC_Register;
	long *ptr;

	//Now, read MMR Base Address
	pci_conf_read( 0, 0, 0, 0x48, 4, &dev0);
	dev0 &= 0xFFFFC000;

	ptr = (long*)(dev0+0x260);
	c0ckectrl = *ptr & 0xFFFFFFFF;	

	ptr = (long*)(dev0+0x660);
	c1ckectrl = *ptr & 0xFFFFFFFF;
	
	// If DIMM 0 not populated, check DIMM 1
	((c0ckectrl) >> 20 & 0xF)?(offset = 0):(offset = 0x400);

	ptr = (long*)(dev0+offset+0x265);
	ODT_Control_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x25D);	
	Precharge_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x252);
	ACT_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x258);
	Read_Register = *ptr & 0xFFFFFFFF;

	ptr = (long*)(dev0+offset+0x240);
	MRC_Register = *ptr & 0xFFFFFFFF;

	// CAS Latency (tCAS)
	if(MRC_Register & 0xF) {
		cas = (MRC_Register & 0xF) + 3.0f;
	} else {
		cas = ((ODT_Control_Register >> 8)& 0x3F) - 5.0f;
	}

	// RAS-To-CAS (tRCD)
	rcd = (Read_Register >> 17) & 0xF;

	// RAS Precharge (tRP)
	rp = (ACT_Register >> 13) & 0xF;

	// RAS Active to precharge (tRAS)
	ras = Precharge_Register & 0x3F;
	
	print_timings_info(cas, rcd, rp, ras);

	cprint(LINE_CPU+6, col2+1, "/"); col2 +=2;

	if ((c0ckectrl >> 20 & 0xF) && (c1ckectrl >> 20 & 0xF)) { 
		cprint(LINE_CPU+6, col2+1, "Dual Channel"); 
	}	else {
		cprint(LINE_CPU+6, col2+1, "Single Channel"); 
	}

}

static void poll_timings_snb(void) {

	float cas;
	int rcd, rp, ras;
	ulong dev0, offset;
	ulong IMC_Register, MCMain0_Register, MCMain1_Register;
	long *ptr;

	//Now, read MMR Base Address
	pci_conf_read( 0, 0, 0, 0x48, 4, &dev0);
	dev0 &= 0xFFFFC000;
	
	offset = 0x0000;

	ptr = (long*)(dev0+offset+0x4000);
	IMC_Register = *ptr & 0xFFFFFFFF;

	// CAS Latency (tCAS)
	cas = (float)((IMC_Register >> 8) & 0x0F);

	// RAS-To-CAS (tRCD)
	rcd = IMC_Register & 0x0F;

	// RAS Precharge (tRP)
	rp = (IMC_Register >> 4) & 0x0F;

	// RAS Active to precharge (tRAS)
	ras = (IMC_Register >> 16) & 0xFF;
	
	print_timings_info(cas, rcd, rp, ras);

	cprint(LINE_CPU+6, col2+1, "/"); col2 +=2;

	// Channels
	ptr = (long*)(dev0+offset+0x5004);
	MCMain0_Register = *ptr & 0xFFFF;
	ptr = (long*)(dev0+offset+0x5008);
	MCMain1_Register = *ptr & 0xFFFF;
	
	if(MCMain0_Register == 0 || MCMain1_Register == 0) {
		cprint(LINE_CPU+6, col2+1, "Single Channel"); 
	} else {
		cprint(LINE_CPU+6, col2+1, "Dual Channel"); 
	}

}

static void poll_timings_5400(void) {

	// Thanks for CDH optis
	ulong ambase, mtr1, mtr2, offset, mca, temp;
	long *ptr;

	//Hard-coded Ambase value (should not be realocated by software when using Memtest86+
	ambase = 0xFE000000;
  offset = mtr1 = mtr2 = 0;

  // Will loop until a valid populated channel is found
  // Bug  : DIMM 0 must be populated or it will fall in an endless loop  
  while(((mtr2 & 0xF) < 3) || ((mtr2 & 0xF) > 6)) {
		ptr = (long*)(ambase+0x378+offset);
		mtr1 = *ptr & 0xFFFFFFFF;
	
		ptr = (long*)(ambase+0x37C+offset);	
		mtr2 = *ptr & 0xFFFFFFFF;
		offset += 0x8000;
	}

	pci_conf_read( 0, 16, 1, 0x58, 4, &mca);

	//This chipset only supports FB-DIMM (Removed => too long)
	//cprint(LINE_CPU+5, col +1, "- Type : FBD");

	// Now, detect timings
	cprint(LINE_CPU+6, col2 +1, "/ CAS : ");
	col2 += 9;

	// CAS Latency (tCAS)
	temp = mtr2 & 0xF;
	dprint(LINE_CPU+6, col2, temp, 1 ,0);
	cprint(LINE_CPU+6, col2+1, "-");
	col2 += 2;

	// RAS-To-CAS (tRCD)
	temp = 6 - ((mtr1 >> 10) & 3);
	dprint(LINE_CPU+6, col2, temp, 1 ,0);
	cprint(LINE_CPU+6, col2+1, "-");
	col2 += 2;

	// RAS Precharge (tRP)
	temp = 6 - ((mtr1 >> 8) & 3);
	dprint(LINE_CPU+6, col2, temp, 1 ,0);
	cprint(LINE_CPU+6, col2+1, "-");
	col2 += 2;

	// RAS Active to precharge (tRAS)
	temp = 16 - (3 * ((mtr1 >> 29) & 3)) + ((mtr1 >> 12) & 3);
  if(((mtr1 >> 12) & 3) == 3 && ((mtr1 >> 29) & 3) == 2) { temp = 9; }

	dprint(LINE_CPU+6, col2, temp, 1 ,0);
	(temp < 10)?(col2 += 1):(col2 += 2);

	cprint(LINE_CPU+6, col2+1, "/"); col2 +=2;

	if ((mca >> 14) & 1) { 
		cprint(LINE_CPU+6, col2+1, "Single Channel"); 
	}	else {
		cprint(LINE_CPU+6, col2+1, "Dual Channel"); 
	}

}

static void poll_timings_E7520(void) {

	ulong drt, ddrcsr;
	float cas;
	int rcd, rp, ras;

	pci_conf_read( 0, 0, 0, 0x78, 4, &drt);
	pci_conf_read( 0, 0, 0, 0x9A, 2, &ddrcsr);

	cas = ((drt >> 2) & 3) + 2;
	rcd = ((drt >> 10) & 1) + 3;
	rp = ((drt >> 9) & 1) + 3;
	ras = ((drt >> 14) & 3) + 11;

	print_timings_info(cas, rcd, rp, ras);
	
	if ((ddrcsr & 0xF) >= 0xC) {
		cprint(LINE_CPU+6, col2, "/ Dual Channel (128 bits)");
	} else {
		cprint(LINE_CPU+6, col2, "/ Single Channel (64 bits)");
	}
}


static void poll_timings_i855(void) {

	ulong drt, temp;

	pci_conf_read( 0, 0, 0, 0x78, 4, &drt);

	/* Now, we could print some additionnals timings infos) */
	cprint(LINE_CPU+6, col2 +1, "/ CAS : ");
	col2 += 9;

	// CAS Latency (tCAS)
	temp = ((drt >> 4)&0x1);
	if (temp == 0x0) { cprint(LINE_CPU+6, col2, "2.5-"); col2 += 4;  }
	else { cprint(LINE_CPU+6, col2, "2-"); col2 +=2; }

	// RAS-To-CAS (tRCD)
	temp = ((drt >> 2)& 0x1);
	if (temp == 0x0) { cprint(LINE_CPU+6, col2, "3-"); }
	else { cprint(LINE_CPU+6, col2, "2-"); }
	col2 +=2;

	// RAS Precharge (tRP)
	temp = (drt&0x1);
	if (temp == 0x0) { cprint(LINE_CPU+6, col2, "3-"); }
	else { cprint(LINE_CPU+6, col2, "2-"); }
	col2 +=2;

	// RAS Active to precharge (tRAS)
	temp = 7-((drt >> 9)& 0x3);
	if (temp == 0x0) { cprint(LINE_CPU+6, col2, "7"); }
	if (temp == 0x1) { cprint(LINE_CPU+6, col2, "6"); }
	if (temp == 0x2) { cprint(LINE_CPU+6, col2, "5"); }
	col2 +=1;

}

static void poll_timings_E750x(void) {

	ulong drt, drc, temp;
	float cas;
	int rcd, rp, ras;

	pci_conf_read( 0, 0, 0, 0x78, 4, &drt);
	pci_conf_read( 0, 0, 0, 0x7C, 4, &drc);

	if ((drt >> 4) & 1) { cas = 2; } else { cas = 2.5; };
	if ((drt >> 1) & 1) { rcd = 2; } else { rcd = 3; };
	if (drt & 1) { rp = 2; } else { rp = 3; };

	temp = ((drt >> 9) & 3);
	if (temp == 2) { ras = 5; } else if (temp == 1) { ras = 6; } else { ras = 7; }

	print_timings_info(cas, rcd, rp, ras);

	if (((drc >> 22)&1) == 1) {
		cprint(LINE_CPU+6, col2, "/ Dual Channel (128 bits)");
	} else {
		cprint(LINE_CPU+6, col2, "/ Single Channel (64 bits)");
	}

}

static void poll_timings_i852(void) {

	ulong drt, temp;

	pci_conf_read( 0, 0, 1, 0x60, 4, &drt);

	/* Now, we could print some additionnals timings infos) */
	cprint(LINE_CPU+6, col2 +1, "/ CAS : ");
	col2 += 9;

	// CAS Latency (tCAS)
	temp = ((drt >> 5)&0x1);
	if (temp == 0x0) { cprint(LINE_CPU+6, col2, "2.5-"); col2 += 4;  }
	else { cprint(LINE_CPU+6, col2, "2-"); col2 +=2; }

	// RAS-To-CAS (tRCD)
	temp = ((drt >> 2)& 0x3);
	if (temp == 0x0) { cprint(LINE_CPU+6, col2, "4-"); }
	if (temp == 0x1) { cprint(LINE_CPU+6, col2, "3-"); }
	else { cprint(LINE_CPU+6, col2, "2-"); }
	col2 +=2;

	// RAS Precharge (tRP)
	temp = (drt&0x3);
	if (temp == 0x0) { cprint(LINE_CPU+6, col2, "4-"); }
	if (temp == 0x1) { cprint(LINE_CPU+6, col2, "3-"); }
	else { cprint(LINE_CPU+6, col2, "2-"); }
	col2 +=2;

	// RAS Active to precharge (tRAS)
	temp = ((drt >> 9)& 0x3);
	if (temp == 0x0) { cprint(LINE_CPU+6, col2, "8"); col2 +=7; }
	if (temp == 0x1) { cprint(LINE_CPU+6, col2, "7"); col2 +=6; }
	if (temp == 0x2) { cprint(LINE_CPU+6, col2, "6"); col2 +=5; }
	if (temp == 0x3) { cprint(LINE_CPU+6, col2, "5"); col2 +=5; }
	col2 +=1;

}

static void poll_timings_amd64(void) {

	ulong dramtlr, dramclr;
	int temp;
	int trcd, trp, tras ;

	cprint(LINE_CPU+6, col2 +1, "/ CAS : ");
	col2 += 9;
	
	pci_conf_read(0, 24, 2, 0x88, 4, &dramtlr);
	pci_conf_read(0, 24, 2, 0x90, 4, &dramclr);

	if (((cpu_id.ext >> 16) & 0xF) >= 4) {
		/* NEW K8 0Fh Family 90 nm (DDR2) */

			// CAS Latency (tCAS)
			temp = (dramtlr & 0x7) + 1;
			dprint(LINE_CPU+6, col2, temp , 1 ,0);
			cprint(LINE_CPU+6, col2 +1, "-"); col2 +=2;
		
			// RAS-To-CAS (tRCD)
			trcd = ((dramtlr >> 4) & 0x3) + 3;
			dprint(LINE_CPU+6, col2, trcd , 1 ,0);
			cprint(LINE_CPU+6, col2 +1, "-"); col2 +=2;
		
			// RAS Precharge (tRP)
			trp = ((dramtlr >> 8) & 0x3) + 3;
			dprint(LINE_CPU+6, col2, trp , 1 ,0);
			cprint(LINE_CPU+6, col2 +1, "-"); col2 +=2;
		
			// RAS Active to precharge (tRAS)
			tras = ((dramtlr >> 12) & 0xF) + 3;
			if (tras < 10){
			dprint(LINE_CPU+6, col2, tras , 1 ,0); col2 += 1;
			} else {
			dprint(LINE_CPU+6, col2, tras , 2 ,0); col2 += 2;
			}
			cprint(LINE_CPU+6, col2+1, "/"); col2 +=2;
		
			// Print 64 or 128 bits mode
		
			if ((dramclr >> 11)&1) {
				cprint(LINE_CPU+6, col2, " DDR2 (128 bits)");
				col2 +=16;
			} else {
				cprint(LINE_CPU+6, col2, " DDR2 (64 bits)");
				col2 +=15;
			}

	} else {
		/* OLD K8 (DDR1) */

			// CAS Latency (tCAS)
			temp = (dramtlr & 0x7);
			if (temp == 0x1) { cprint(LINE_CPU+6, col2, "2-"); col2 +=2; }
			if (temp == 0x2) { cprint(LINE_CPU+6, col2, "3-"); col2 +=2; }
			if (temp == 0x5) { cprint(LINE_CPU+6, col2, "2.5-"); col2 +=4; }
		
			// RAS-To-CAS (tRCD)
			trcd = ((dramtlr >> 12) & 0x7);
			dprint(LINE_CPU+6, col2, trcd , 1 ,0);
			cprint(LINE_CPU+6, col2 +1, "-"); col2 +=2;
		
			// RAS Precharge (tRP)
			trp = ((dramtlr >> 24) & 0x7);
			dprint(LINE_CPU+6, col2, trp , 1 ,0);
			cprint(LINE_CPU+6, col2 +1, "-"); col2 +=2;
		
			// RAS Active to precharge (tRAS)
			tras = ((dramtlr >> 20) & 0xF);
			if (tras < 10){
			dprint(LINE_CPU+6, col2, tras , 1 ,0); col2 += 1;
			} else {
			dprint(LINE_CPU+6, col2, tras , 2 ,0); col2 += 2;
			}
			cprint(LINE_CPU+6, col2+1, "/"); col2 +=2;
		
			// Print 64 or 128 bits mode
		
			if (((dramclr >> 16)&1) == 1) {
				cprint(LINE_CPU+6, col2, " DDR1 (128 bits)");
				col2 +=16;
			} else {
				cprint(LINE_CPU+6, col2, " DDR1 (64 bits)");
				col2 +=15;
			}
	}
}

static void poll_timings_k10(void) {

	ulong dramtlr, dramclr, dramchr;
	ulong offset = 0;
	int cas, rcd, rp, rc, ras;
	
	pci_conf_read(0, 24, 2, 0x94, 4, &dramchr);
	
	// If Channel A not enabled, switch to channel B
	if(((dramchr>>14) & 0x1))
	{
		offset = 0x100;
		pci_conf_read(0, 24, 2, 0x94+offset, 4, &dramchr);	
	}

	pci_conf_read(0, 24, 2, 0x88+offset, 4, &dramtlr);
	pci_conf_read(0, 24, 2, 0x110, 4, &dramclr);
	
	// CAS Latency (tCAS)
	if(((dramchr >> 8)&1) || ((dramchr & 0x7) == 0x4)){
		// DDR3 or DDR2-1066
		cas = (dramtlr & 0xF) + 4;
		rcd = ((dramtlr >> 4) & 0x7) + 5;
		rp = ((dramtlr >> 7) & 0x7) + 5;
	  ras = ((dramtlr >> 12) & 0xF) + 15;
		rc = ((dramtlr >> 16) & 0x1F) + 11;		
	} else {
	// DDR2-800 or less
		cas = (dramtlr & 0xF) + 1;
		rcd = ((dramtlr >> 4) & 0x3) + 3;
		rp = ((dramtlr >> 8) & 0x3) + 3;
	  ras = ((dramtlr >> 12) & 0xF) + 3;
		rc = ((dramtlr >> 16) & 0x1F) + 11;
	}

	print_timings_info(cas, rcd, rp, ras);
	
	cprint(LINE_CPU+6, col2, "/"); col2++;
	
	//Print DDR2 or DDR3
	if ((dramchr >> 8)&1) {
		cprint(LINE_CPU+6, col2+1, "DDR3");
	} else {
		cprint(LINE_CPU+6, col2+1, "DDR2");
	}
	col2 += 5;
	
	// Print 64 or 128 bits mode
		if ((dramclr >> 4)&1) {
			cprint(LINE_CPU+6, col2+1, "(128 bits)");
		} else {
			cprint(LINE_CPU+6, col2+1, "(64 bits)");
		}
	
}

static void poll_timings_k14(void) {

	ulong dramt0, dramlow;
	int cas, rcd, rp, rc, ras;
	
	pci_conf_read(0, 24, 2, 0x88, 4, &dramlow);
	pci_conf_write(0, 24, 2, 0xF0, 4, 0x00000040);
	pci_conf_read(0, 24, 2, 0xF4, 4, &dramt0);

	cas = (dramlow & 0xF) + 4;
	rcd = (dramt0 & 0xF) + 5;
	rp = ((dramt0 >> 8) & 0xF) + 5;
  ras = ((dramt0 >> 16) & 0x1F) + 15;
	rc = ((dramt0 >> 24) & 0x3F) + 16;

	print_timings_info(cas, rcd, rp, ras);

	cprint(LINE_CPU+6, col2, "/ DDR3 (64 bits)");

}

static void poll_timings_EP80579(void) {

	ulong drt1, drt2;
	float cas;
	int rcd, rp, ras;

	pci_conf_read( 0, 0, 0, 0x78, 4, &drt1);
	pci_conf_read( 0, 0, 0, 0x64, 4, &drt2);

	cas = ((drt1 >> 3) & 0x7) + 3;
	rcd = ((drt1 >> 9) & 0x7) + 3;
	rp = ((drt1 >> 6) & 0x7) + 3;
	ras = ((drt2 >> 28) & 0xF) + 8;

	print_timings_info(cas, rcd, rp, ras);
}

static void poll_timings_nf2(void) {

	ulong dramtlr, dramtlr2, dramtlr3, temp;
	ulong dimm1p, dimm2p, dimm3p;

	pci_conf_read(0, 0, 1, 0x90, 4, &dramtlr);
	pci_conf_read(0, 0, 1, 0xA0, 4, &dramtlr2);
	pci_conf_read(0, 0, 1, 0x84, 4, &dramtlr3);
	pci_conf_read(0, 0, 2, 0x40, 4, &dimm1p);
	pci_conf_read(0, 0, 2, 0x44, 4, &dimm2p);
	pci_conf_read(0, 0, 2, 0x48, 4, &dimm3p);

	cprint(LINE_CPU+6, col2 +1, "/ CAS : ");
	col2 += 9;

	// CAS Latency (tCAS)
	temp = ((dramtlr2 >> 4) & 0x7);
	if (temp == 0x2) { cprint(LINE_CPU+6, col2, "2-"); col2 +=2; }
	if (temp == 0x3) { cprint(LINE_CPU+6, col2, "3-"); col2 +=2; }
	if (temp == 0x6) { cprint(LINE_CPU+6, col2, "2.5-"); col2 +=4; }

	// RAS-To-CAS (tRCD)
	temp = ((dramtlr >> 20) & 0xF);
	dprint(LINE_CPU+6, col2, temp , 1 ,0);
	cprint(LINE_CPU+6, col2 +1, "-"); col2 +=2;

	// RAS Precharge (tRP)
	temp = ((dramtlr >> 28) & 0xF);
	dprint(LINE_CPU+6, col2, temp , 1 ,0);
	cprint(LINE_CPU+6, col2 +1, "-"); col2 +=2;

	// RAS Active to precharge (tRAS)
	temp = ((dramtlr >> 15) & 0xF);
	if (temp < 10){
		dprint(LINE_CPU+6, col2, temp , 1 ,0); col2 += 1;
	} else {
		dprint(LINE_CPU+6, col2, temp , 2 ,0); col2 += 2;
	}
		cprint(LINE_CPU+6, col2+1, "/"); col2 +=2;

	// Print 64 or 128 bits mode
	// If DIMM1 & DIMM3 or DIMM1 & DIMM2 populated, than Dual Channel.

	if ((dimm3p&1) + (dimm2p&1) == 2 || (dimm3p&1) + (dimm1p&1) == 2 ) {
		cprint(LINE_CPU+6, col2, " Dual Channel (128 bits)");
		col2 +=24;
	} else {
		cprint(LINE_CPU+6, col2, " Single Channel (64 bits)");
		col2 +=15;
	}

}

static void poll_timings_us15w(void) {

	// Thanks for CDH optis
	ulong dtr, temp;

	/* Find dramratio */
	/* D0 MsgRd, 01 Dunit, 01 DTR */
	pci_conf_write(0, 0, 0, 0xD0, 4, 0xD0010100 );		
	pci_conf_read(0, 0, 0, 0xD4, 4, &dtr );		

	// Now, detect timings
	cprint(LINE_CPU+5, col2 +1, "/ CAS : ");
	col2 += 9;

	// CAS Latency (tCAS)
	temp = ((dtr >> 4) & 0x3) + 3;
	dprint(LINE_CPU+5, col2, temp, 1 ,0);
	cprint(LINE_CPU+5, col2+1, "-");
	col2 += 2;

	// RAS-To-CAS (tRCD)
	temp = ((dtr >> 2) & 0x3) + 3;
	dprint(LINE_CPU+5, col2, temp, 1 ,0);
	cprint(LINE_CPU+5, col2+1, "-");
	col2 += 2;

	// RAS Precharge (tRP)
	temp = ((dtr >> 0) & 0x3) + 3;
	dprint(LINE_CPU+5, col2, temp, 1 ,0);
	col2 += 1;

}

static void poll_timings_nhm(void) {

	ulong mc_channel_bank_timing, mc_control, mc_channel_mrs_value;
	float cas; 
	int rcd, rp, ras;
	int fvc_bn = 4;

	/* Find which channels are populated */
	pci_conf_read(nhm_bus, 3, 0, 0x48, 2, &mc_control);		
	mc_control = (mc_control >> 8) & 0x7;
	
	/* Get the first valid channel */
	if(mc_control & 1) { 
		fvc_bn = 4; 
	} else if(mc_control & 2) { 
		fvc_bn = 5; 
	}	else if(mc_control & 4) { 
		fvc_bn = 6; 
	}

	// Now, detect timings
	// CAS Latency (tCAS) / RAS-To-CAS (tRCD) / RAS Precharge (tRP) / RAS Active to precharge (tRAS)
	pci_conf_read(nhm_bus, fvc_bn, 0, 0x88, 4, &mc_channel_bank_timing);	
	pci_conf_read(nhm_bus, fvc_bn, 0, 0x70, 4, &mc_channel_mrs_value);	
	cas = ((mc_channel_mrs_value >> 4) & 0xF ) + 4.0f;
	rcd = (mc_channel_bank_timing >> 9) & 0xF; 
	ras = (mc_channel_bank_timing >> 4) & 0x1F; 
	rp = mc_channel_bank_timing & 0xF;

	print_timings_info(cas, rcd, rp, ras);

	// Print 1, 2 or 3 Channels
	if (mc_control == 1 || mc_control == 2 || mc_control == 4 ) {
		cprint(LINE_CPU+6, col2, "/ Single Channel");
		col2 += 16;
	} else if (mc_control == 7) {
		cprint(LINE_CPU+6, col2, "/ Triple Channel");
		col2 += 16;
	} else {
		cprint(LINE_CPU+6, col2, "/ Dual Channel");
		col2 += 14;		
	}

}


/* ------------------ Let's continue ------------------ */
/* ---------------------------------------------------- */

static struct pci_memory_controller controllers[] = {
	/* Default unknown chipset */
	{ 0, 0, "",                    0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },

	/* AMD */
	{ 0x1022, 0x7006, "AMD 751",   0, poll_fsb_nothing, poll_timings_nothing, setup_amd751, poll_amd751 },
	{ 0x1022, 0x700c, "AMD 762",   0, poll_fsb_nothing, poll_timings_nothing, setup_amd76x, poll_amd76x },
	{ 0x1022, 0x700e, "AMD 761",   0, poll_fsb_nothing, poll_timings_nothing, setup_amd76x, poll_amd76x },

	/* SiS */
	{ 0x1039, 0x0600, "SiS 600",   0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0620, "SiS 620",   0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x5600, "SiS 5600",  0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0645, "SiS 645",   0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0646, "SiS 645DX", 0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0630, "SiS 630",   0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0650, "SiS 650",   0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0651, "SiS 651",   0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0730, "SiS 730",   0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0735, "SiS 735",   0, poll_fsb_amd32, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0740, "SiS 740",   0, poll_fsb_amd32, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0745, "SiS 745",   0, poll_fsb_amd32, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0748, "SiS 748",   0, poll_fsb_amd32, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0655, "SiS 655",   0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0656, "SiS 656",   0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0648, "SiS 648",   0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0649, "SiS 649",   0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0661, "SiS 661",   0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0671, "SiS 671",   0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1039, 0x0672, "SiS 672",   0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },	

	/* ALi */
	{ 0x10b9, 0x1531, "ALi Aladdin 4", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x10b9, 0x1541, "ALi Aladdin 5", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x10b9, 0x1644, "ALi Aladdin M1644", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },

	/* ATi */
	{ 0x1002, 0x5830, "ATi Radeon 9100 IGP", 0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1002, 0x5831, "ATi Radeon 9100 IGP", 0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1002, 0x5832, "ATi Radeon 9100 IGP", 0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1002, 0x5833, "ATi Radeon 9100 IGP", 0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1002, 0x5954, "ATi Radeon Xpress 200", 0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1002, 0x5A41, "ATi Radeon Xpress 200", 0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },

	/* nVidia */
	{ 0x10de, 0x01A4, "nVidia nForce", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x10de, 0x01E0, "nVidia nForce2 SPP", 0, poll_fsb_nf2, poll_timings_nf2, setup_nothing, poll_nothing },
	{ 0x10de, 0x0071, "nForce4 SLI Intel Edition", 0, poll_fsb_nf4ie, poll_timings_nf4ie, setup_nothing, poll_nothing },

	/* VIA */
	{ 0x1106, 0x0305, "VIA KT133/KT133A",    0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x0391, "VIA KX133",    0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x0501, "VIA MVP4",    0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x0585, "VIA VP/VPX",  0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x0595, "VIA VP2",  0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x0597, "VIA VP3",  0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x0598, "VIA MVP3",  0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x0691, "VIA Apollo Pro/133/133A",  0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x0693, "VIA Apollo Pro+",  0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x0601, "VIA PLE133",  0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x3099, "VIA KT266(A)/KT333", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x3189, "VIA KT400(A)/600", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x0269, "VIA KT880", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x3205, "VIA KM400", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x3116, "VIA KM266", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x3156, "VIA KN266", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x3123, "VIA CLE266", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x0198, "VIA PT800", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x1106, 0x3258, "VIA PT880", 0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },

	/* Serverworks */
	{ 0x1166, 0x0008, "CNB20HE",   0, poll_fsb_nothing, poll_timings_nothing, setup_cnb20, poll_nothing },
	{ 0x1166, 0x0009, "CNB20LE",   0, poll_fsb_nothing, poll_timings_nothing, setup_cnb20, poll_nothing },

	/* Intel */
	{ 0x8086, 0x1130, "Intel i815",      		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x122d, "Intel i430FX",    		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x1235, "Intel i430MX",    		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x1237, "Intel i440FX",    		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x1250, "Intel i430HX",    		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x1A21, "Intel i840",      		0, poll_fsb_nothing, poll_timings_nothing, setup_i840, poll_i840 },
	{ 0x8086, 0x1A30, "Intel i845",      		0, poll_fsb_p4, poll_timings_nothing, setup_i845, poll_i845 },
	{ 0x8086, 0x2560, "Intel i845E/G/PE/GE",0, poll_fsb_p4, poll_timings_nothing, setup_i845, poll_i845 },
	{ 0x8086, 0x2500, "Intel i820",      		0, poll_fsb_nothing, poll_timings_nothing, setup_i820, poll_i820 },
	{ 0x8086, 0x2530, "Intel i850",      		0, poll_fsb_p4, poll_timings_nothing, setup_i850, poll_i850 },
	{ 0x8086, 0x2531, "Intel i860",      		1, poll_fsb_nothing, poll_timings_nothing, setup_i860, poll_i860 },
	{ 0x8086, 0x7030, "Intel i430VX",    		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x7100, "Intel i430TX",    		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x7120, "Intel i810",      		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x7122, "Intel i810",      		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x7124, "Intel i810E",     		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x7180, "Intel i440[LE]X", 		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x7190, "Intel i440BX",    		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x7192, "Intel i440BX",    		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x71A0, "Intel i440GX",    		0, poll_fsb_nothing, poll_timings_nothing, setup_i440gx, poll_i440gx },
	{ 0x8086, 0x71A2, "Intel i440GX",    		0, poll_fsb_nothing, poll_timings_nothing, setup_i440gx, poll_i440gx },
	{ 0x8086, 0x84C5, "Intel i450GX",    		0, poll_fsb_nothing, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x2540, "Intel E7500",     		1, poll_fsb_p4, poll_timings_E750x, setup_iE7xxx, poll_iE7xxx },
	{ 0x8086, 0x254C, "Intel E7501",     		1, poll_fsb_p4, poll_timings_E750x, setup_iE7xxx, poll_iE7xxx },
	{ 0x8086, 0x255d, "Intel E7205",     		0, poll_fsb_p4, poll_timings_nothing, setup_iE7xxx, poll_iE7xxx },
  { 0x8086, 0x3592, "Intel E7320",     		0, poll_fsb_p4, poll_timings_E7520, setup_iE7520, poll_iE7520 },
  { 0x8086, 0x2588, "Intel E7221",     		1, poll_fsb_i925, poll_timings_i925, setup_i925, poll_iE7221 },
  { 0x8086, 0x3590, "Intel E7520",     		0, poll_fsb_p4, poll_timings_E7520, setup_iE7520, poll_nothing },
  { 0x8086, 0x2600, "Intel E8500",     		0, poll_fsb_p4, poll_timings_nothing, setup_nothing, poll_nothing },
	{ 0x8086, 0x2570, "Intel i848/i865", 		0, poll_fsb_i875, poll_timings_i875, setup_i875, poll_nothing },
	{ 0x8086, 0x2578, "Intel i875P",     		0, poll_fsb_i875, poll_timings_i875, setup_i875, poll_i875 },
	{ 0x8086, 0x2550, "Intel E7505",     		0, poll_fsb_p4, poll_timings_nothing, setup_iE7xxx, poll_iE7xxx },
	{ 0x8086, 0x3580, "Intel ",          		0, poll_fsb_i855, poll_timings_i852, setup_nothing, poll_nothing },
	{ 0x8086, 0x3340, "Intel i855PM",    		0, poll_fsb_i855, poll_timings_i855, setup_nothing, poll_nothing },
	{ 0x8086, 0x2580, "Intel i915P/G",   		0, poll_fsb_i925, poll_timings_i925, setup_i925, poll_nothing },
	{ 0x8086, 0x2590, "Intel i915PM/GM", 		0, poll_fsb_i925, poll_timings_i925, setup_i925, poll_nothing },
	{ 0x8086, 0x2584, "Intel i925X/XE",  		0, poll_fsb_i925, poll_timings_i925, setup_i925, poll_iE7221 },
	{ 0x8086, 0x2770, "Intel i945P/G", 	 		0, poll_fsb_i945, poll_timings_i925, setup_i925, poll_nothing },
	{ 0x8086, 0x27A0, "Intel i945GM/PM", 		0, poll_fsb_i945, poll_timings_i925, setup_i925, poll_nothing },
	{ 0x8086, 0x27AC, "Intel i945GME", 	 		0, poll_fsb_i945, poll_timings_i925, setup_i925, poll_nothing },
	{ 0x8086, 0x2774, "Intel i955X", 		 		0, poll_fsb_i945, poll_timings_i925, setup_i925, poll_nothing},
	{ 0x8086, 0x277C, "Intel i975X", 		 		0, poll_fsb_i975, poll_timings_i925, setup_i925, poll_nothing},
	{ 0x8086, 0x2970, "Intel i946PL/GZ", 		0, poll_fsb_i965, poll_timings_i965, setup_p35, poll_nothing},
	{ 0x8086, 0x2990, "Intel Q963/Q965", 		0, poll_fsb_i965, poll_timings_i965, setup_p35, poll_nothing},
	{ 0x8086, 0x29A0, "Intel P965/G965", 		0, poll_fsb_i965, poll_timings_i965, setup_p35, poll_nothing},
	{ 0x8086, 0x2A00, "Intel GM965/GL960", 	0, poll_fsb_im965, poll_timings_im965, setup_p35, poll_nothing},
	{ 0x8086, 0x2A10, "Intel GME965/GLE960",0, poll_fsb_im965, poll_timings_im965, setup_p35, poll_nothing},	
	{ 0x8086, 0x2A40, "Intel PM/GM45/47",		0, poll_fsb_im965, poll_timings_im965, setup_p35, poll_nothing},	
	{ 0x8086, 0x29B0, "Intel Q35", 	 		 		0, poll_fsb_i965, poll_timings_p35, setup_p35, poll_nothing},	
	{ 0x8086, 0x29C0, "Intel P35/G33", 	 		0, poll_fsb_i965, poll_timings_p35, setup_p35, poll_nothing},	
	{ 0x8086, 0x29D0, "Intel Q33",	  	 		0, poll_fsb_i965, poll_timings_p35, setup_p35, poll_nothing},	
	{ 0x8086, 0x29E0, "Intel X38/X48", 	 		0, poll_fsb_i965, poll_timings_p35, setup_p35, poll_nothing},			
	{ 0x8086, 0x29F0, "Intel 3200/3210", 		0, poll_fsb_i965, poll_timings_p35, setup_p35, poll_nothing},	
	{ 0x8086, 0x2E10, "Intel Q45/Q43", 	 		0, poll_fsb_i965, poll_timings_p35, setup_p35, poll_nothing},	
	{ 0x8086, 0x2E20, "Intel P45/G45",	  	0, poll_fsb_i965, poll_timings_p35, setup_p35, poll_nothing},	
	{ 0x8086, 0x2E30, "Intel G41", 	 				0, poll_fsb_i965, poll_timings_p35, setup_p35, poll_nothing},	
	{ 0x8086, 0x4001, "Intel 5400A", 		 		0, poll_fsb_5400, poll_timings_5400, setup_E5400, poll_nothing},		
	{ 0x8086, 0x4003, "Intel 5400B", 		 		0, poll_fsb_5400, poll_timings_5400, setup_E5400, poll_nothing},		
	{ 0x8086, 0x25D8, "Intel 5000P", 		 		0, poll_fsb_5400, poll_timings_5400, setup_E5400, poll_nothing},		
	{ 0x8086, 0x25D4, "Intel 5000V", 		 		0, poll_fsb_5400, poll_timings_5400, setup_E5400, poll_nothing},	
	{ 0x8086, 0x25C0, "Intel 5000X", 		 		0, poll_fsb_5400, poll_timings_5400, setup_E5400, poll_nothing},		
	{ 0x8086, 0x25D0, "Intel 5000Z", 		 		0, poll_fsb_5400, poll_timings_5400, setup_E5400, poll_nothing},	
	{ 0x8086, 0x5020, "Intel EP80579",    	0, poll_fsb_p4, 	poll_timings_EP80579, setup_nothing, poll_nothing },
	{ 0x8086, 0x8100, "Intel US15W",				0, poll_fsb_us15w, poll_timings_us15w, setup_nothing, poll_nothing},
	{ 0x8086, 0x8101, "Intel UL11L/US15L", 	0, poll_fsb_us15w, poll_timings_us15w, setup_nothing, poll_nothing},

	/* Integrated Memory Controllers */
	{ 0xFFFF, 0x0001, "Core IMC", 	 				0, poll_fsb_nhm, 	poll_timings_nhm, setup_nhm, poll_nothing},
	{ 0xFFFF, 0x0002, "Core IMC 32nm", 	 		0, poll_fsb_nhm32, 	poll_timings_nhm, setup_nhm32, poll_nothing},
	{ 0xFFFF, 0x0003, "Core IMC 32nm", 	 		0, poll_fsb_wmr, 	poll_timings_wmr, setup_wmr, poll_nothing},
	{ 0xFFFF, 0x0004, "SNB IMC 32nm", 	 		0, poll_fsb_snb, 	poll_timings_snb, setup_wmr, poll_nothing},
	{ 0xFFFF, 0x0100, "AMD K8 IMC",					0, poll_fsb_amd64, poll_timings_amd64, setup_amd64, poll_amd64 },
	{ 0xFFFF, 0x0101, "AMD K10 IMC",			  0, poll_fsb_k10, poll_timings_k10, setup_k10, poll_nothing },
	{ 0xFFFF, 0x0102, "AMD APU IMC",			  0, poll_fsb_k14, poll_timings_k14, setup_nothing, poll_nothing },

	/* Fail Safe */
	{ 0xFFFF, 0xFFFF, "",			  						0, poll_fsb_failsafe, poll_timings_nothing, setup_nothing, poll_nothing }	
};	

static void print_memory_controller(void)
{
	/* Print memory controller info */

	int d;

	char *name;
	if (ctrl.index == 0) {
		return;
	}

	/* Print the controller name */
	name = controllers[ctrl.index].name;
	col = 10;
	cprint(LINE_CPU+5, col, name);
	/* Now figure out how much I just printed */
	while(name[col - 10] != '\0') {
		col++;
	}
	/* Now print the memory controller capabilities */
	cprint(LINE_CPU+5, col, " "); col++;
	if (ctrl.cap == ECC_UNKNOWN) {
		return;
	}
	if (ctrl.cap & __ECC_DETECT) {
		int on;
		on = ctrl.mode & __ECC_DETECT;
		cprint(LINE_CPU+5, col, "(ECC : ");
		cprint(LINE_CPU+5, col +7, on?"Detect":"Disabled)");
		on?(col += 13):(col += 16);
	}
	if (ctrl.mode & __ECC_CORRECT) {
		int on;
		on = ctrl.mode & __ECC_CORRECT;
		cprint(LINE_CPU+5, col, " / ");
		if (ctrl.cap & __ECC_CHIPKILL) {
		cprint(LINE_CPU+5, col +3, on?"Correct -":"");
		on?(col += 12):(col +=3);
		} else {
			cprint(LINE_CPU+5, col +3, on?"Correct)":"");
			on?(col += 11):(col +=3);
		}
	}
	if (ctrl.mode & __ECC_DETECT) {
	if (ctrl.cap & __ECC_CHIPKILL) {
		int on;
		on = ctrl.mode & __ECC_CHIPKILL;
		cprint(LINE_CPU+5, col, " Chipkill : ");
		cprint(LINE_CPU+5, col +12, on?"On)":"Off)");
		on?(col += 15):(col +=16);
	}}
	if (ctrl.mode & __ECC_SCRUB) {
		int on;
		on = ctrl.mode & __ECC_SCRUB;
		cprint(LINE_CPU+5, col, " Scrub");
		cprint(LINE_CPU+5, col +6, on?"+ ":"- ");
		col += 7;
	}
	if (ctrl.cap & __ECC_UNEXPECTED) {
		int on;
		on = ctrl.mode & __ECC_UNEXPECTED;
		cprint(LINE_CPU+5, col, "Unknown");
		cprint(LINE_CPU+5, col +7, on?"+ ":"- ");
		col += 9;
	}

	/* Print advanced caracteristics  */
	col2 = 0;
	d = get_key();
	/* if F1 is pressed, disable advanced detection */
	if (d != 0x3B) {
	controllers[ctrl.index].poll_fsb();
	controllers[ctrl.index].poll_timings();
	}
}


void find_controller(void)
{
	unsigned long vendor;
	unsigned long device;
	extern struct cpu_ident cpu_id;
	int i;
	int result;
	result = pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, PCI_VENDOR_ID, 2, &vendor);
	result = pci_conf_read(ctrl.bus, ctrl.dev, ctrl.fn, PCI_DEVICE_ID, 2, &device);
	
	// Detect IMC by CPUID
	if(imc_type) { vendor = 0xFFFF; device = imc_type; }
	if(fail_safe) { vendor = 0xFFFF; device = 0xFFFF; }
	
	ctrl.index = 0;	
		if (result == 0) {
			for(i = 1; i < sizeof(controllers)/sizeof(controllers[0]); i++) {
				if ((controllers[i].vendor == vendor) && (controllers[i].device == device)) {
					ctrl.index = i;
					break;
				}
			}
		}
	
	controllers[ctrl.index].setup_ecc();
	/* Don't enable ECC polling by default unless it has
	 * been well tested.
	 */
	set_ecc_polling(-1);
	print_memory_controller();

}

void poll_errors(void)
{
	if (ctrl.poll) {
		controllers[ctrl.index].poll_errors();
	}
}

void set_ecc_polling(int val)
{
	int tested = controllers[ctrl.index].tested;
	if (val == -1) {
		val = tested;
	}
	if (val && (ctrl.mode & __ECC_DETECT)) {
		ctrl.poll = 1;
		cprint(LINE_INFO, COL_ECC, tested? " on": " ON");
	} else {
		ctrl.poll = 0;
		cprint(LINE_INFO, COL_ECC, "off");
	}
}


