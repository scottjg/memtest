/*
 * cpuid.h --
 *
 *      contains the data structures required for CPUID 
 *      implementation.
 *
 */

#ifndef _CPUID_H_
#define _CPUID_H_
#include "stdint.h"
#include "defs.h"
#include "smp.h"

#define CPUID_EXTENDED_BASE    0x80000000
#define CPUID_EXTENDED_FEATURE 0x80000001
#define CPUID_EXTENDED_BRAND1  0x80000002
#define CPUID_EXTENDED_BRAND2  0x80000003
#define CPUID_EXTENDED_BRAND3  0x80000004

#define CPUID_VENDOR_LENGTH     3               /* 3 GPRs hold vendor ID */
#define CPUID_VENDOR_STR_LENGTH (CPUID_VENDOR_LENGTH * sizeof(uint32_t) + 1)
#define CPUID_BRAND_LENGTH      12              /* 12 GPRs hold vendor ID */
#define CPUID_BRAND_STR_LENGTH  (CPUID_BRAND_LENGTH * sizeof(uint32_t) + 1)

#define CPUID_FAMILY(_eax)   (((_eax) >> 8) & 0xf)
/* Intel CPU Family */
#define CPUID_FAMILY_486     4
#define CPUID_FAMILY_P5      5
#define CPUID_FAMILY_P6      6
#define CPUID_FAMILY_EXTENDED 15

#define CPUID_EXTENDED_FAMILY(_eax)   (((_eax) >> 20) & 0xff)
#define CPUID_EXTENDED_FAMILY_PENTIUM4  0
#define CPUID_EXTENDED_FAMILY_OPTERON   0


#define CPUID_FAMILY_IS_OPTERON(_eax) \
           (CPUID_FAMILY(_eax) == CPUID_FAMILY_EXTENDED && \
            CPUID_EXTENDED_FAMILY(_eax) == CPUID_EXTENDED_FAMILY_OPTERON)                                                                                              
#define CPUID_FEATURE_COMMON_ID1EDX_HT         0x10000000 /* 28 */


typedef struct {
   uint32_t eax;
   uint32_t ebx; 
   uint32_t ecx; 
   uint32_t edx;
} cpuid_t;

/* cached CPUID data for CPUID(0) and CPUID(0x80000000) */
extern cpuid_t cpuid_data0;
extern cpuid_t cpuid_data80;


static inline unsigned
cpuid_max_func()
{
   return cpuid_data0.eax;
}


static inline unsigned
cpuid_max_ext_func()
{
   return cpuid_data80.eax;
}


/* Typedef for storing the CPUID Vendor String */
typedef union {
   /* Note: the extra byte in the char array is for '\0'. */
   char         char_array[CPUID_VENDOR_STR_LENGTH];
   uint32_t       uint32_array[CPUID_VENDOR_LENGTH];
} cpuid_vendor_string_t;

/* Typedef for storing the CPUID Brand String */
typedef union {
   /* Note: the extra byte in the char array is for '\0'. */
   char         char_array[CPUID_BRAND_STR_LENGTH];
   uint32_t       uint32_array[CPUID_BRAND_LENGTH];
} cpuid_brand_string_t;

/* Typedef for storing CPUID Version */
typedef union {
   uint32_t flat;
   struct {
      uint32_t    stepping:4;      /* Bit 0 */
      uint32_t    model:4;
      uint32_t    family:4;
      uint32_t    processorType:2;
      uint32_t    reserved1514:2;
      uint32_t    extendedModel:4;
      uint32_t    extendedFamily:8;
      uint32_t    reserved3128:4;  /* Bit 31 */
   } bits;      
} cpuid_version_t;

/* Typedef for storing CPUID Processor Information */
typedef union {
   uint32_t flat;
   struct {
      uint32_t    brandIndex:8;    /* Bit 0 */
      uint32_t    cflushLineSize:8;
      uint32_t    logicalProcessorCount:8;
      uint32_t    apicID:8;        /* Bit 31 */
   } bits;      
} cpuid_proc_info_t;

/* Typedef for storing CPUID Feature flags */
typedef union {
   uint64_t       flat;
   uint32_t       uint32_array[2];
   struct {
      uint32_t    fpu:1;           /* Bit 0 */
      uint32_t    vme:1;
      uint32_t    de:1;
      uint32_t    pse:1;
      uint32_t    tsc:1;
      uint32_t    msr:1;
      uint32_t    pae:1;
      uint32_t    mce:1;
      uint32_t    cx8:1;
      uint32_t    apic:1;
      uint32_t    reserved10:1;
      uint32_t    sep:1;
      uint32_t    mtrr:1;
      uint32_t    pge:1;
      uint32_t    mca:1;
      uint32_t    cmov:1;
      uint32_t    pat:1;
      uint32_t    pse36:1;
      uint32_t    psn:1;
      uint32_t    cflush:1;
      uint32_t    reserved20:1;
      uint32_t    ds:1;
      uint32_t    acpi:1;
      uint32_t    mmx:1;
      uint32_t    fxsr:1;
      uint32_t    sse:1;
      uint32_t    sse2:1;
      uint32_t    ss:1;
      uint32_t    htt:1;
      uint32_t    tm:1;
      uint32_t    reserved30:1;
      uint32_t    pbe:1;           /* Bit 31 */
      uint32_t    sse3:1;          /* Bit 32 */
      uint32_t    reserved3433:2;
      uint32_t    monitor:1;
      uint32_t    dscpl:1;
      uint32_t    reserved3937:3;
      uint32_t    tm2:1;
      uint32_t    reserved41:1;
      uint32_t    cnxtid:1;
      uint32_t    reserved4443:2;
      uint32_t    cmpxchg16b:1;
      uint32_t    reserved6346:18; /* Bit 63 */
   } bits;
} cpuid_feature_flags_t;

/* Feature flags returned by extended CPUID node function 8000_0001. */
typedef union {
   uint64_t       flat;
   uint32_t       uint32_array[2];
   struct {
      uint32_t    fpu:1;           /* Bit 0 */
      uint32_t    vme:1;
      uint32_t    de:1;
      uint32_t    pse:1;
      uint32_t    tsc:1;
      uint32_t    msr:1;
      uint32_t    pae:1;
      uint32_t    mce:1;
      uint32_t    cx8:1;
      uint32_t    apic:1;
      uint32_t    reserved10:1;
      uint32_t    sep:1;
      uint32_t    mtrr:1;
      uint32_t    pge:1;
      uint32_t    mca:1;
      uint32_t    cmov:1;
      uint32_t    pat:1;
      uint32_t    pse36:1;
      uint32_t    reserved1918:2;
      uint32_t    nx:1;
      uint32_t    reserved21:1;
      uint32_t    mmxamd:1;
      uint32_t    mmx:1;
      uint32_t    fxsr:1;
      uint32_t    ffxsr:1;
      uint32_t    reserved26:1;
      uint32_t    rdtscp:1;
      uint32_t    reserved28:1;
      uint32_t    lm:1;
      uint32_t    threedeenowext:1;
      uint32_t    threedeenow:1;   /* Bit 31 */
      uint32_t    lahf:1;          /* Bit 32 */
      uint32_t    cmplegacy:1;
      uint32_t    reserved3534:2;
      uint32_t    cr8avail:1;
      uint32_t    reserved6337:27; /* Bit 63 */
   } bits;
} cpuid_ext_feature_flags_t;

void cpuid_get(unsigned n, cpuid_t *data);
cpuid_vendor_string_t cpuid_get_vendor_string(void);
cpuid_version_t      cpuid_get_version(void);
cpuid_feature_flags_t cpuid_get_feature_flags(void);
bool              cpuid_get_ext_feature_flags(cpuid_ext_feature_flags_t *f);
bool              cpuid_is_vendor_amd(void);
bool              cpuid_is_vendor_intel(void);
bool              cpuid_is_family_p6(void);
bool              cpuid_is_family_p4(void);
bool              cpuid_is_family_opteron(void);
void              cpuid_init(void);

#endif
