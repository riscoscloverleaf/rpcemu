/*
  RPCEmu - An Acorn system emulator

  Copyright (C) 2021 RiscOS Cloverleaf

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "hostfs.h"

#ifndef SRC_HOSTCLIPBOARD_H
#define SRC_HOSTCLIPBOARD_H

#define ARCEM_SWI_CLIPBOARD_SETUP        1
#define ARCEM_SWI_CLIPBOARD_HOST_SET     2
#define ARCEM_SWI_CLIPBOARD_HOST_GET     3
#define ARCEM_SWI_CLIPBOARD_HOST_CHECK   4

#define ARCEM_SWI_CLIPBOARD     (ARCEM_SWI_CHUNK + 0x10)

#define ARCEM_POLLWORD_HOST_CLIPBORARD_CHANGED         1

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern uint32_t cliptask_pollword_addr;

extern void clipboard_swi(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t r4, uint32_t r5, uint32_t *retr0, uint32_t *retr1);
extern void clipboard_changed_on_host_notify(int file_type, const char *data, unsigned int data_len);
extern void rpcemu_set_host_clipboard(int file_type, const char *data, unsigned int data_len);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif //SRC_HOSTCLIPBOARD_H
