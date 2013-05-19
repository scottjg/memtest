/*
 * smp.c --
 *
 *    Implements support for SMP machines. For reasons of
 *    simplicity, we do not handle all possible cases allowed by the
 *    MP spec. For example, we expect an explicit MP configuration
 *    table and do not handle default configurations. We also expect
 *    an on-chip local apic and do not support an external 82489DX
 *    apic controller.
 * 
 */

#include "stddef.h"
#include "smp.h"
#include "cpuid.h"
#include "test.h"
#define DELAY_FACTOR 1
extern void memcpy(void *dst, void *src , int len);
extern void test_start(void);

typedef struct {
   bool started;
} ap_info_t;

volatile apic_register_t *APIC = NULL;
unsigned number_of_cpus = 1; // There is at least one cpu, the BSP
/* CPU number to APIC ID mapping table. CPU 0 is the BSP. */
static unsigned cpu_num_to_apic_id[MAX_CPUS];
volatile ap_info_t AP[MAX_CPUS];

uint8_t
checksum(uint8_t *data, unsigned len)
{
   uint32_t sum = 0;
   uint8_t *end = data + len;
   while (data < end) {
      sum += *data;
      data++;
   }
   return (uint8_t)(sum % 0x100);
}


bool
read_mp_config_table(uintptr_t addr)
{
   mp_config_table_header_t *mpc = (mp_config_table_header_t*)addr;
   uint8_t *tab_entry_ptr;
   uint8_t *mpc_table_end;
   extern unsigned num_hyper_threads_per_core;

   if (mpc->signature != MPCSignature) {
      return FALSE;
   }
   if (checksum((uint8_t*)mpc, mpc->length) != 0) {
      return FALSE;
   }


   /* FIXME: the uintptr_t cast here works around a compilation problem on
    * AMD64, but it ignores the real problem, which is that lapic_addr
    * is only 32 bits.  Maybe that's OK, but it should be investigated.
    */
   APIC = (volatile apic_register_t*)(uintptr_t)mpc->lapic_addr;

   tab_entry_ptr = ((uint8_t*)mpc) + sizeof(mp_config_table_header_t);
   mpc_table_end = ((uint8_t*)mpc) + mpc->length;
   while (tab_entry_ptr < mpc_table_end) {
      switch (*tab_entry_ptr) {
      case MP_PROCESSOR: {
	 mp_processor_entry_t *pe = (mp_processor_entry_t*)tab_entry_ptr;

	 if (pe->cpu_flag & CPU_BOOTPROCESSOR) {
	    // BSP is CPU 0
	    cpu_num_to_apic_id[0] = pe->apic_id;
	 } else if (number_of_cpus < MAX_CPUS) {
	    cpu_num_to_apic_id[number_of_cpus] = pe->apic_id;
	    number_of_cpus++;
	 }
	 if (num_hyper_threads_per_core > 1 ) {
	    cpu_num_to_apic_id[number_of_cpus] = pe->apic_id | 1;
	    number_of_cpus++;
	 }
	    
	 // we cannot handle non-local 82489DX apics
	 if ((pe->apic_ver & 0xf0) != 0x10) {
	    return 0;
	 }

	 // we don't know what to do with disabled cpus

	 tab_entry_ptr += sizeof(mp_processor_entry_t);
	 break;
      }
      case MP_BUS: {
	 tab_entry_ptr += sizeof(mp_bus_entry_t);
	 break;
      }
      case MP_IOAPIC: {
	 tab_entry_ptr += sizeof(mp_io_apic_entry_t);
	 break;
      }
      case MP_INTSRC:
	 tab_entry_ptr += sizeof(mp_interrupt_entry_t);
      case MP_LINTSRC:
	 tab_entry_ptr += sizeof(mp_local_interrupt_entry_t);
	 break;
      default: 
	 return FALSE;
      }
   }
   return TRUE;
}


floating_pointer_struct_t *
scan_for_floating_ptr_struct(uintptr_t addr, uint32_t length)
{
   floating_pointer_struct_t *fp;
   uintptr_t end = addr + length;


   fp = (floating_pointer_struct_t*)addr;
   while ((uintptr_t)fp < end) {
      if (fp->signature == FPSignature) {
	 if (fp->length == 1 && checksum((uint8_t*)fp, 16) == 0) {
	    return fp;
	 } 
      }
      fp++;
   }
   return NULL;
}

void PUT_MEM16(uintptr_t addr, uint16_t val)
{
   *((volatile uint16_t *)addr) = val;
}

void PUT_MEM32(uintptr_t addr, uint32_t val)
{
   *((volatile uint32_t *)addr) = val;
}

static void inline 
APIC_WRITE(unsigned reg, uint32_t val)
{
   APIC[reg][0] = val;
}

static inline uint32_t 
APIC_READ(unsigned reg)
{
   return APIC[reg][0];
}


static void 
SEND_IPI(unsigned apic_id, unsigned trigger, unsigned level, unsigned mode,
	    uint8_t vector)
{
   uint32_t v;

   v = APIC_READ(APICR_ICRHI) & 0x00ffffff;
   APIC_WRITE(APICR_ICRHI, v | (apic_id << 24));

   v = APIC_READ(APICR_ICRLO) & ~0xcdfff;
   v |= (APIC_DEST_DEST << APIC_ICRLO_DEST_OFFSET) 
      | (trigger << APIC_ICRLO_TRIGGER_OFFSET)
      | (level << APIC_ICRLO_LEVEL_OFFSET)
      | (mode << APIC_ICRLO_DELMODE_OFFSET)
      | (vector);
   APIC_WRITE(APICR_ICRLO, v);
}


// Silly way of busywaiting, but we don't have a timer
void delay(unsigned us) 
{
   unsigned freq = 1000; // in MHz, assume 1GHz CPU speed
   uint64_t cycles = us * freq;
   uint64_t t0 = RDTSC();
   uint64_t t1;
   volatile unsigned k;

   do {
      for (k = 0; k < 1000; k++) continue;
      t1 = RDTSC();
   } while (t1 - t0 < cycles);
}

static inline void
memset (void *dst,
        char  value,
        int   len)
{
   int i;
   for (i = 0 ; i < len ; i++ ) { 
      *((char *) dst + i) = value;
   }
}

void kick_cpu(unsigned cpu_num)
{
   unsigned num_sipi, apic_id;
   apic_id = cpu_num_to_apic_id[cpu_num];

   // clear the APIC ESR register
   APIC_WRITE(APICR_ESR, 0);
   APIC_READ(APICR_ESR);

   // asserting the INIT IPI
   SEND_IPI(apic_id, APIC_TRIGGER_LEVEL, 1, APIC_DELMODE_INIT, 0);
   delay(100000 / DELAY_FACTOR);

   // de-assert the INIT IPI
   SEND_IPI(apic_id, APIC_TRIGGER_LEVEL, 0, APIC_DELMODE_INIT, 0);

   for (num_sipi = 0; num_sipi < 2; num_sipi++) {
      unsigned timeout;
      bool send_pending;
      unsigned err;

      APIC_WRITE(APICR_ESR, 0);

      SEND_IPI(apic_id, 0, 0, APIC_DELMODE_STARTUP, (uint32_t)startup_32 >> 12);

      timeout = 0;
      do {
	 delay(10);
	 timeout++;
	 send_pending = (APIC_READ(APICR_ICRLO) & APIC_ICRLO_STATUS_MASK) != 0;
      } while (send_pending && timeout < 1000);

      if (send_pending) {
	 //cprint(LINE_STATUS+1, 0, "SMP: STARTUP IPI was never sent");
      }
      
      delay(100000 / DELAY_FACTOR);

      err = APIC_READ(APICR_ESR) & 0xef;
      if (err) {
	 //cprint(LINE_STATUS+1, 0, "SMP: After STARTUP IPI: err = 0x");
         //hprint(LINE_STATUS+1, COL_MID, err);
      }
   }
}

// These memory locations are used for the trampoline code and data.

#define BOOTCODESTART 0x9000
#define GDTPOINTERADDR 0x9100
#define GDTADDR 0x9110

void boot_ap(unsigned cpu_num)
{
   unsigned num_sipi, apic_id;
   extern uint8_t gdt; 
   extern uint8_t _ap_trampoline_start;
   extern uint8_t _ap_trampoline_protmode;
   unsigned len = &_ap_trampoline_protmode - &_ap_trampoline_start;
   apic_id = cpu_num_to_apic_id[cpu_num];


   memcpy((uint8_t*)BOOTCODESTART, &_ap_trampoline_start, len);

   // Fixup the LGDT instruction to point to GDT pointer.
   PUT_MEM16(BOOTCODESTART + 3, GDTPOINTERADDR);

   // Copy a pointer to the temporary GDT to addr GDTPOINTERADDR.
   // The temporary gdt is at addr GDTADDR
   PUT_MEM16(GDTPOINTERADDR, 4 * 8);
   PUT_MEM32(GDTPOINTERADDR + 2, GDTADDR);

   // Copy the first 4 gdt entries from the currently used GDT to the
   // temporary GDT.
   memcpy((uint8_t *)GDTADDR, &gdt, 32);

   // clear the APIC ESR register
   APIC_WRITE(APICR_ESR, 0);
   APIC_READ(APICR_ESR);

   // asserting the INIT IPI
   SEND_IPI(apic_id, APIC_TRIGGER_LEVEL, 1, APIC_DELMODE_INIT, 0);
   delay(100000 / DELAY_FACTOR);

   // de-assert the INIT IPI
   SEND_IPI(apic_id, APIC_TRIGGER_LEVEL, 0, APIC_DELMODE_INIT, 0);

   for (num_sipi = 0; num_sipi < 2; num_sipi++) {
      unsigned timeout;
      bool send_pending;
      unsigned err;

      APIC_WRITE(APICR_ESR, 0);

      SEND_IPI(apic_id, 0, 0, APIC_DELMODE_STARTUP, BOOTCODESTART >> 12);

      timeout = 0;
      do {
	 delay(10);
	 timeout++;
	 send_pending = (APIC_READ(APICR_ICRLO) & APIC_ICRLO_STATUS_MASK) != 0;
      } while (send_pending && timeout < 1000);

      if (send_pending) {
	 //cprint(LINE_STATUS+1, 0, "SMP: STARTUP IPI was never sent");
      }
      
      delay(100000 / DELAY_FACTOR);

      err = APIC_READ(APICR_ESR) & 0xef;
      if (err) {
	 //cprint(LINE_STATUS+1, 0, "SMP: After STARTUP IPI: err = 0x");
        // hprint(LINE_STATUS+1, COL_MID, err);
      }
   }
}

void
smp_init_bsp()
{
   floating_pointer_struct_t *fp;
   /* gets the details about the cpu, the type, the brand
    * whether it is a multi-core package etc.
    */
   cpuid_init(); 

   memset(&AP, 0, sizeof AP);

   fp = scan_for_floating_ptr_struct(0x0, 0x400);
   if (fp == NULL) {
      fp = scan_for_floating_ptr_struct(639*0x400, 0x400);
   }
   if (fp == NULL) {
         fp = scan_for_floating_ptr_struct(0xf0000, 0x10000);
   }
   if (fp == NULL) {
        /*
         * If it is an SMP machine we should know now, unless the
         * configuration is in an EISA/MCA bus machine with an
         * extended bios data area.
         *
         * there is a real-mode segmented pointer pointing to the
         * 4K EBDA area at 0x40E, calculate and scan it here.
        */
        unsigned int address = *(unsigned short *)0x40E;
        address <<= 4;
	if (address) {
       		fp = scan_for_floating_ptr_struct(address, 0x400);
        }
   }

   if (fp != NULL && fp->phys_addr != 0) {
      if (!read_mp_config_table(fp->phys_addr)) {
	 //cprint(LINE_STATUS+1,0, "SMP: Error while parsing MP config table");
      }
   }
/*
   if (fp == NULL) {
      cprint(LINE_STATUS+1,0,"SMP: No floating pointer structure found");
   }
*/
}

void
smp_init_aps()
{
   int cpuNum;
   for(cpuNum = 0 ; cpuNum < MAX_CPUS ; cpuNum++) {
      AP[cpuNum].started = FALSE;
   }
}

unsigned
my_apic_id()
{
   return (APIC[APICR_ID][0]) >> 24;
}

void 
smp_ap_booted(unsigned cpu_num) 
{
   AP[cpu_num].started = TRUE;
}

void
smp_boot_ap(unsigned cpu_num)
{
   unsigned timeout;
   extern bool smp_mode;
   boot_ap(cpu_num);
   timeout = 0;
   do {
      delay(1000 / DELAY_FACTOR);
      timeout++;
   } while (!AP[cpu_num].started && timeout < 100000 / DELAY_FACTOR);

   if (!AP[cpu_num].started) {
      //cprint(LINE_STATUS+1, 0, "SMP: Boot timeout for");
      //dprint(LINE_STATUS+1, COL_MID, cpu_num,2,1);
      //cprint(LINE_STATUS+1, 26, "Turning off SMP");
      smp_mode = FALSE;
   }
}

unsigned
smp_num_cpus()
{
   return number_of_cpus;
}

unsigned
smp_my_cpu_num()
{
   unsigned apicid = my_apic_id();
   unsigned i;

   for (i = 0; i < MAX_CPUS; i++) {
      if (apicid == cpu_num_to_apic_id[i]) {
	 break;
      }
   }
   if (i == MAX_CPUS) {
      i = 0;
   }
   return i;
}

volatile spinlock_t barr_lk={1};
void barrier(volatile int *barr, int n)
{
        spin_lock(&barr_lk);
        barr++;
        spin_unlock(&barr_lk);
        while((uint32_t)barr<n);
	barr = 0;
        return;
}

