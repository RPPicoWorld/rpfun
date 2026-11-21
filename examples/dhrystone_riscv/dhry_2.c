#include "dhry.h"

void proc_6(enumeration enum_val_par, enumeration *enum_ref_par, int int_glob) {
    *enum_ref_par = enum_val_par;
    if (!func_3(enum_val_par)) {
        *enum_ref_par = ident_4;
    }
    switch (enum_val_par) {
    case ident_1:
        *enum_ref_par = ident_1;
        break;
    case ident_2:
        if (int_glob > 100) {
            *enum_ref_par = ident_1;
        } else {
            *enum_ref_par = ident_4;
        }
        break;
    case ident_3:
        *enum_ref_par = ident_2;
        break;
    case ident_4:
        break;
    case ident_5:
        *enum_ref_par = ident_3;
        break;
    }
}

void proc_7(one_fifty int_1_par_val, one_fifty int_2_par_val, one_fifty *int_par_ref) {
    one_fifty int_loc = int_1_par_val + 2;
    *int_par_ref = int_2_par_val + int_loc;
}

void proc_8(arr_1_dim arr_1_par_ref, arr_2_dim arr_2_par_ref, int int_1_par_val, int int_2_par_val, int *int_glob) {
    int int_index;
    int int_loc = int_1_par_val + 5;

    arr_1_par_ref[int_loc] = int_2_par_val;
    arr_1_par_ref[int_loc + 1] = arr_1_par_ref[int_loc];
    arr_1_par_ref[int_loc + 30] = int_loc;

    for (int_index = int_loc; int_index <= int_loc + 1; ++int_index) {
        arr_2_par_ref[int_loc][int_index] = int_loc;
    }
    arr_2_par_ref[int_loc][int_loc - 1] += 1;
    arr_2_par_ref[int_loc + 20][int_loc] = arr_1_par_ref[int_loc];
    *int_glob = 5;
}

enumeration func_1(capital_letter ch_1_par_val, capital_letter ch_2_par_val, char *ch_1_glob) {
    capital_letter ch_1_loc = ch_1_par_val;
    capital_letter ch_2_loc = ch_1_loc;

    if (ch_2_loc != ch_2_par_val) {
        return ident_1;
    } else {
        *ch_1_glob = ch_1_loc;
        return ident_2;
    }
}

boolean func_2(str_30 str_1_par_ref, str_30 str_2_par_ref, char *ch_1_glob, int *int_glob) {
    one_thirty int_loc = 2;
    capital_letter ch_loc = ' ';

    while (int_loc <= 2) {
        if (func_1(str_1_par_ref[int_loc], str_2_par_ref[int_loc + 1], ch_1_glob) == ident_1) {
            ch_loc = 'A';
            int_loc += 1;
        }
    }

    if (ch_loc >= 'W' && ch_loc < 'Z') {
        int_loc = 7;
    }

    if (ch_loc == 'R') {
        return true;
    } else {
        if (strcmp(str_1_par_ref, str_2_par_ref) > 0) {
            int_loc += 7;
            *int_glob = int_loc;
            return true;
        } else {
            return false;
        }
    }
}

boolean func_3(enumeration enum_par_val) {
    enumeration enum_loc = enum_par_val;
    if (enum_loc == ident_3) {
        return true;
    } else {
        return false;
    }
}