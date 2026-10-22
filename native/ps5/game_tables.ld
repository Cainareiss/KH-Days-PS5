/* Include alongside the title's main linker script. KEEP is required even
 * with --gc-sections: these records are reached through the table boundaries. */
SECTIONS
{
    khdays_data : ALIGN(8)
    {
        __start_khdays_data = .;
        KEEP(*(khdays_data))
        __stop_khdays_data = .;
    }
    khdays_functions : ALIGN(8)
    {
        __start_khdays_functions = .;
        KEEP(*(khdays_functions))
        __stop_khdays_functions = .;
    }
}
INSERT AFTER .data;
ASSERT((__stop_khdays_data - __start_khdays_data) % 24 == 0,
       "invalid native data table layout")
ASSERT((__stop_khdays_functions - __start_khdays_functions) % 16 == 0,
       "invalid native function table layout")
