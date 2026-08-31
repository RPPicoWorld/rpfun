#include "dhry.h"
#include "pico/mutex.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdio.h>

#ifndef REG
boolean reg = false;
#define REG
#else
boolean reg = true;
#endif

void run_dhrystone_benchmark(int number_of_runs, int core_id) {
    one_fifty int_1_loc;
    REG one_fifty int_2_loc;
    one_fifty int_3_loc;
    REG char ch_index;
    enumeration enum_loc;
    str_30 str_1_loc;
    str_30 str_2_loc;
    REG int run_index;

    /* Localize state per execution to allow safe execution on either core */
    rec_pointer ptr_glob;
    rec_pointer next_ptr_glob;
    int int_glob = 0;
    boolean bool_glob = false;
    char ch_1_glob = 0;
    char ch_2_glob = 0;
    arr_1_dim arr_1_glob;
    arr_2_dim arr_2_glob;

    next_ptr_glob = (rec_pointer)malloc(sizeof(rec_type));
    ptr_glob = (rec_pointer)malloc(sizeof(rec_type));

    ptr_glob->ptr_comp = next_ptr_glob;
    ptr_glob->discr = ident_1;
    ptr_glob->variant.var_1.enum_comp = ident_3;
    ptr_glob->variant.var_1.int_comp = 40;
    strcpy(ptr_glob->variant.var_1.str_comp, "DHRYSTONE PROGRAM, SOME STRING");
    strcpy(str_1_loc, "DHRYSTONE PROGRAM, 1'ST STRING");

    arr_2_glob[8][7] = 10;

    mutex_enter_blocking(&printf_mutex);
    printf("--- Core %d: Dhrystone Benchmark, Version 2.1 (Language: C) ---\n", core_id);
    if (reg) {
        printf("Program compiled with 'register' attribute\n");
    } else {
        printf("Program compiled without 'register' attribute\n");
    }
    printf("Execution starts, %d runs through Dhrystone\n", number_of_runs);
    mutex_exit(&printf_mutex);

    absolute_time_t start_time = get_absolute_time();

    for (run_index = 1; run_index <= number_of_runs; ++run_index) {
        proc_5(&ch_1_glob, &bool_glob);
        proc_4(ch_1_glob, &ch_2_glob, &bool_glob);
        int_1_loc = 2;
        int_2_loc = 3;
        strcpy(str_2_loc, "DHRYSTONE PROGRAM, 2'ND STRING");
        enum_loc = ident_2;
        bool_glob = !func_2(str_1_loc, str_2_loc, &ch_1_glob, &int_glob);

        while (int_1_loc < int_2_loc) {
            int_3_loc = 5 * int_1_loc - int_2_loc;
            proc_7(int_1_loc, int_2_loc, &int_3_loc);
            int_1_loc += 1;
        }

        proc_8(arr_1_glob, arr_2_glob, int_1_loc, int_3_loc, &int_glob);
        proc_1(ptr_glob, ptr_glob);

        for (ch_index = 'A'; ch_index <= ch_2_glob; ++ch_index) {
            if (enum_loc == func_1(ch_index, 'C', &ch_1_glob)) {
                proc_6(ident_1, &enum_loc, int_glob);
                strcpy(str_2_loc, "DHRYSTONE PROGRAM, 3'RD STRING");
                int_2_loc = run_index;
                int_glob = run_index;
            }
        }

        int_2_loc = int_2_loc * int_1_loc;
        int_1_loc = int_2_loc / int_3_loc;
        int_2_loc = 7 * (int_2_loc - int_3_loc) - int_1_loc;
        proc_2(&int_1_loc, ch_1_glob, int_glob);
    }

    absolute_time_t stop_time = get_absolute_time();
    int64_t total_us = absolute_time_diff_us(start_time, stop_time);

    mutex_enter_blocking(&printf_mutex);
    printf("--- Core %d Execution Ends ---\n", core_id);
    printf("Final values of the variables used in the benchmark:\n");
    printf("int_glob:            %d (should be: 5)\n", int_glob);
    printf("bool_glob:           %d (should be: 1)\n", bool_glob);
    printf("ch_1_glob:           %c (should be: A)\n", ch_1_glob);
    printf("ch_2_glob:           %c (should be: B)\n", ch_2_glob);
    printf("arr_1_glob[8]:       %d (should be: 7)\n", arr_1_glob[8]);
    printf("arr_2_glob[8][7]:    %d (should be: %d)\n", arr_2_glob[8][7], number_of_runs + 10);
    printf("ptr_glob->discr:     %d (should be: 0)\n", ptr_glob->discr);
    printf("ptr_glob->enum_comp: %d (should be: 2)\n", ptr_glob->variant.var_1.enum_comp);
    printf("ptr_glob->int_comp:  %d (should be: 17)\n", ptr_glob->variant.var_1.int_comp);
    printf("ptr_glob->str_comp:  %s\n", ptr_glob->variant.var_1.str_comp);
    printf("int_1_loc:           %d (should be: 5)\n", int_1_loc);
    printf("int_2_loc:           %d (should be: 13)\n", int_2_loc);
    printf("int_3_loc:           %d (should be: 7)\n", int_3_loc);
    printf("enum_loc:            %d (should be: 1)\n", enum_loc);

    float user_time_sec = (float)total_us / 1000000.0f;

    if (total_us < 2000000) {
        printf("Measured time too small to obtain accurate results (< 2 seconds)\n\n");
    } else {
        float microseconds_per_run = (float)total_us / (float)number_of_runs;
        float dhrystones_per_sec = (float)number_of_runs / user_time_sec;
        float dmips = dhrystones_per_sec / 1757.0f;

        printf("Core %d Microseconds for one run: %12.2f\n", core_id, microseconds_per_run);
        printf("Core %d Dhrystones per second:    %12.2f\n", core_id, dhrystones_per_sec);
        printf("Core %d DMIPS:                    %12.2f\n\n", core_id, dmips);
    }
    mutex_exit(&printf_mutex);

    free(ptr_glob);
    free(next_ptr_glob);
}

void proc_1(rec_pointer ptr_val_par, rec_pointer ptr_glob) {
    rec_pointer next_record = ptr_val_par->ptr_comp;

    structassign(*ptr_val_par->ptr_comp, *ptr_glob);
    ptr_val_par->variant.var_1.int_comp = 5;
    next_record->variant.var_1.int_comp = ptr_val_par->variant.var_1.int_comp;
    next_record->ptr_comp = ptr_val_par->ptr_comp;
    proc_3(&next_record->ptr_comp, ptr_glob, 5);

    if (next_record->discr == ident_1) {
        next_record->variant.var_1.int_comp = 6;
        proc_6(ptr_val_par->variant.var_1.enum_comp, &next_record->variant.var_1.enum_comp, 5);
        next_record->ptr_comp = ptr_glob->ptr_comp;
        proc_7(next_record->variant.var_1.int_comp, 10, &next_record->variant.var_1.int_comp);
    } else {
        structassign(*ptr_val_par, *ptr_val_par->ptr_comp);
    }
}

void proc_2(one_fifty *int_par_ref, char ch_1_glob, int int_glob) {
    one_fifty int_loc;
    enumeration enum_loc;

    int_loc = *int_par_ref + 10;
    do {
        if (ch_1_glob == 'A') {
            int_loc -= 1;
            *int_par_ref = int_loc - int_glob;
            enum_loc = ident_1;
        }
    } while (enum_loc != ident_1);
}

void proc_3(rec_pointer *ptr_ref_par, rec_pointer ptr_glob, int int_glob) {
    if (ptr_glob != null) {
        *ptr_ref_par = ptr_glob->ptr_comp;
    }
    proc_7(10, int_glob, &ptr_glob->variant.var_1.int_comp);
}

void proc_4(char ch_1_glob, char *ch_2_glob, boolean *bool_glob) {
    boolean bool_loc = (ch_1_glob == 'A');
    *bool_glob = bool_loc | *bool_glob;
    *ch_2_glob = 'B';
}

void proc_5(char *ch_1_glob, boolean *bool_glob) {
    *ch_1_glob = 'A';
    *bool_glob = false;
}