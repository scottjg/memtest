/*
 * cpuid.c --
 *
 *      Implements CPUID querying functions
 *
 */
#include "cpuid.h"

cpuid_t cpuid_data0;
cpuid_t cpuid_data80;

unsigned num_logical_cpus = 1;      // number of logical cpus per physical package
unsigned num_cores_per_package = 1;  // number of cores in each physical cpu package
unsigned num_hyper_threads_per_core = 1;      // number of hyper-threads per core

void
cpuid_get(unsigned n, cpuid_t *data)
{
   data->eax = n;
   GET_CPUID(data->eax, data->ebx, data->ecx, data->edx);
}


/*  cpuid_get_vendor_string ---
 *
 *  This function gets the vendor string from the processor's cpuid instruction
 *  and passes it back to the caller in an easy to use structure.
 */
cpuid_vendor_string_t
cpuid_get_vendor_string(void)
{
   static cpuid_vendor_string_t v;

   /* Note: the string gets passed in EBX-EDX-ECX, not the intuitive order. */
   v.uint32_array[0] = cpuid_data0.ebx;
   v.uint32_array[1] = cpuid_data0.edx;
   v.uint32_array[2] = cpuid_data0.ecx;
   v.char_array[CPUID_VENDOR_STR_LENGTH-1] = '\0';
   return v;
}


/*  cpuid_get_version ---
 *
 *  This function reads the processors version information using CPUID and puts
 *  it into a union for easy use by the caller.
 */
cpuid_version_t
cpuid_get_version(void)
{
   cpuid_version_t v;
   uint32_t junkEBX = 0, junkECX = 0, junkEDX = 0;
   v.flat = 0x1;
   GET_CPUID(v.flat, junkEBX, junkECX, junkEDX);
   return v;
}


cpuid_feature_flags_t 
cpuid_get_feature_flags(void)
{
   cpuid_feature_flags_t f;
   uint32_t junkEAX = 0x1, junkEBX = 0;
   GET_CPUID(junkEAX, junkEBX, f.uint32_array[1], f.uint32_array[0]);
   return f;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cpuid_get_ext_feature_flags --
 *
 *    Passes back the caller the extended feature flags supported by
 *    this CPU.  This can be used, among other things, to determine if the
 *    processor supports long mode.
 *
 * Results:
 *    Returns TRUE if the processor supports the extended feature flags
 *    CPUID node, and FALSE otherwise.
 *
 * Side effects:
 *    Calls CPUID a couple of times.
 *
 *-----------------------------------------------------------------------------
 */

bool
cpuid_get_ext_feature_flags(cpuid_ext_feature_flags_t *f) // OUT: Flags for this CPU
{
   uint32_t eax, ebx, ecx;

   if (cpuid_data80.eax < 0x80000001) {
      // Extended feature flags not supported on this CPU
      return FALSE;
   }
   eax = CPUID_EXTENDED_FEATURE;
   GET_CPUID(eax, ebx, ecx, f->flat);
   return TRUE;
}

#define CHAR_TO_INT(a,b,c,d) ((a) + (b) * 0x100 + (c) * 0x10000 + (d) * 0x1000000)

bool
cpuid_is_vendor_amd(void)
{
   return cpuid_data0.ebx == CHAR_TO_INT('A', 'u', 't', 'h')
      && cpuid_data0.edx == CHAR_TO_INT('e', 'n', 't', 'i')
      && cpuid_data0.ecx == CHAR_TO_INT('c', 'A', 'M', 'D');
}


bool
cpuid_is_vendor_intel(void)
{
   return cpuid_data0.ebx == CHAR_TO_INT('G', 'e', 'n', 'u')
      && cpuid_data0.edx == CHAR_TO_INT('i', 'n', 'e', 'I')
      && cpuid_data0.ecx == CHAR_TO_INT('n', 't', 'e', 'l');
}


/*
 *-----------------------------------------------------------------------------
 *
 * cpuid_is_family_p4 --
 *
 *    Returns TRUE if the processor we're running on is an Intel processor 
 *    of the P4 family.
 *
 * Results:
 *    The obvious.
 *
 *-----------------------------------------------------------------------------
 */

bool
cpuid_is_family_p4(void)
{
   cpuid_version_t v = cpuid_get_version();

   return cpuid_is_vendor_intel() && v.bits.family == CPUID_FAMILY_EXTENDED && 
      v.bits.extendedFamily == CPUID_EXTENDED_FAMILY_PENTIUM4;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cpuid_is_family_p6 --
 *
 *    Returns TRUE if the processor we're running on belongs to the P6 family.
 *
 * Results:
 *    The obvious.
 *
 *-----------------------------------------------------------------------------
 */

bool
cpuid_is_family_p6(void)
{
   cpuid_version_t v = cpuid_get_version();

   return cpuid_is_vendor_intel() && v.bits.family == CPUID_FAMILY_P6;
}


/*
 *-----------------------------------------------------------------------------
 *
 * cpuid_is_family_opteron --
 *
 *    Returns TRUE if the processor we're running on belongs to the
 *    Opteron family.
 *
 *-----------------------------------------------------------------------------
 */

bool
cpuid_is_family_opteron(void)
{
   cpuid_version_t v = cpuid_get_version();
   return cpuid_is_vendor_amd() && CPUID_FAMILY_IS_OPTERON(v.flat);
}


/*
 *-----------------------------------------------------------------------------
 *
 * cpuid_init --
 *
 *    Executes CPUID and caches values in cpuid_data0 abd cpuid_data80.
 *
 *-----------------------------------------------------------------------------
 */
void
cpuid_init(void)
{
   //bool htt = FALSE;
   cpuid_t id1;
   
   /* First get the basic cpuid information on what the 
    * type of the processor is , i.e intel or amd etc
    * and how much of extra cpuid information is available
    * with the processor 
    */
   cpuid_data0.eax = 0;
   GET_CPUID(cpuid_data0.eax, cpuid_data0.ebx,
	     cpuid_data0.ecx, cpuid_data0.edx);


   /* Find out if hyper-threading is available and there is more than one
    * logical processor.  See section 7.6.3 in Intel IA-32 volume III.
    */
   cpuid_get(1, &id1);

   if (cpuid_is_vendor_intel()) {
      if (cpuid_is_family_p6()) {
         // Extended CPUID features not supported on PIII
         return;
      }
      if (cpuid_is_family_p4()) {
         /*
          * Multi-core processors have the HT feature bit set (even if they
          * don't support HT).
          * The number of HT is the total number, not per-core number.
          * The number of cores is off by 1, i.e. single-core reports 0.
          */
         //htt = id1.edx & CPUID_FEATURE_COMMON_ID1EDX_HT;
         if (id1.edx & CPUID_FEATURE_COMMON_ID1EDX_HT) {
            num_hyper_threads_per_core = (id1.ebx >> 16) & 0xff;
            if (cpuid_max_func() >= 4) {
	       cpuid_t id4;
	       cpuid_get(4, &id4);
	       num_cores_per_package = ((id4.eax >> 26) & 0x3f) + 1;
               num_hyper_threads_per_core /=  num_cores_per_package;
            }
         }
      }
   } else if (cpuid_is_vendor_amd()) {
      cpuid_data80.eax = 0x80000000;
      GET_CPUID(cpuid_data80.eax, cpuid_data80.ebx,
	     cpuid_data80.ecx, cpuid_data80.edx);
      if (cpuid_max_ext_func() >= 0x80000008) {
	 /* Number of cores is reported in extended function 0x80000008
          * For legacy multi-core support, AMD CPUs report the number of
          * cores as hyper-threads. Adjust the numbers to reflect that there
          * are no threads.
          */
	 cpuid_t id88;
	 cpuid_get(0x80000008, &id88);
	 num_cores_per_package = id88.ecx & 0xff;
	 num_hyper_threads_per_core = 1;
      }
   } else {
      /* Unknown cpu type. we use the defaults */
   }
}
