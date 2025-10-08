/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 Dang Huynh
 */

#ifndef __AWARDMODULAR_H
#define __AWARDMODULAR_H

/**
 * epa_logo_fade() - fade the epa logo
 */
void epa_logo_fade(void);

/**
 * print_modular_bios() - display the modular bios
 */
int print_modular_bios(void);

/**
 * print_modular_bios_second() - display the second stage of the modular bios
 */
int print_modular_bios_second(void);

/**
 * init_modular_bios() - initialize modular bios structs
 */
void init_modular_bios(void);

#endif
