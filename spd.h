/*
 * MemTest86+ V3.00 Specific code (GPL V2.0)
 * By Samuel DEMEULEMEESTER, sdemeule@memtest.org
 * http://www.canardpc.com - http://www.memtest.org
 */
 
void get_spd_spec(void);
int get_ddr2_module_size(int rank_density_byte, int rank_num_byte);
int get_ddr3_module_size(int sdram_capacity, int prim_bus_width, int sdram_width, int ranks);
char* convert_hex_to_char(unsigned hex_org);
#define LINE_SPD 16


