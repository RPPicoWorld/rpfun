#ifndef DHRY_H
#define DHRY_H

#include "pico/mutex.h" /* Includes mutex_t */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Correct mutex type declaration */
extern mutex_t printf_mutex;

#define structassign(d, s) d = s

typedef enum {
    ident_1,
    ident_2,
    ident_3,
    ident_4,
    ident_5
} enumeration;

#define null 0
#define true 1
#define false 0

typedef int one_thirty;
typedef int one_fifty;
typedef char capital_letter;
typedef int boolean;
typedef char str_30[31];
typedef int arr_1_dim[50];
typedef int arr_2_dim[50][50];

typedef struct record {
    struct record *ptr_comp;
    enumeration discr;
    union {
        struct {
            enumeration enum_comp;
            int int_comp;
            char str_comp[31];
        } var_1;
        struct {
            enumeration e_comp_2;
            char str_2_comp[31];
        } var_2;
        struct {
            char ch_1_comp;
            char ch_2_comp;
        } var_3;
    } variant;
} rec_type, *rec_pointer;

/* Function declarations */
void proc_1(rec_pointer ptr_val_par, rec_pointer ptr_glob);
void proc_2(one_fifty *int_par_ref, char ch_1_glob, int int_glob);
void proc_3(rec_pointer *ptr_ref_par, rec_pointer ptr_glob, int int_glob);
void proc_4(char ch_1_glob, char *ch_2_glob, boolean *bool_glob);
void proc_5(char *ch_1_glob, boolean *bool_glob);
void proc_6(enumeration enum_val_par, enumeration *enum_ref_par, int int_glob);
void proc_7(one_fifty int_1_par_val, one_fifty int_2_par_val, one_fifty *int_par_ref);
void proc_8(arr_1_dim arr_1_par_ref, arr_2_dim arr_2_par_ref, int int_1_par_val, int int_2_par_val, int *int_glob);

enumeration func_1(capital_letter ch_1_par_val, capital_letter ch_2_par_val, char *ch_1_glob);
boolean func_2(str_30 str_1_par_ref, str_30 str_2_par_ref, char *ch_1_glob, int *int_glob);
boolean func_3(enumeration enum_par_val);

void run_dhrystone_benchmark(int number_of_runs, int core_id);

#endif /* DHRY_H */