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

/* HOST<->GUEST clibpoard integration support */

#include <stdio.h>
#include <string.h>
#include "hostclipboard.h"
#include "mem.h"
#include "rpcemu.h"
#include "network.h"

uint32_t cliptask_pollword_addr = 0;
int alphabet = 100;
char* host_clipboard = NULL;
unsigned int host_clipboard_len = 0;
int host_clipboard_file_type = 0xfff;
unsigned int riscos_ucstable[256];

static void from_ucs(unsigned int *in, unsigned int len, unsigned char **out) {
    unsigned int i, j, result_ptr = 0;
    unsigned char *result = malloc(len + 1);
    if (!result) {
#if defined __linux || defined __linux__
        fprintf(stderr, "from_ucs() malloc failed\n");
#endif
        rpclog("from_ucs() malloc failed\n");
        *out = NULL;
        return;
    }
    for(i = 0; i < len; i++) {
//        fprintf(stderr,"%08x ", in[i]);
        for(j = 0; j < 256; j++) {
            if (in[i] == riscos_ucstable[j]) {
                result[result_ptr] = (unsigned char)j;
//                fprintf(stderr,"%08x -> %c", result[result_ptr], (unsigned char)j);
                result_ptr++;
                break;
            }
        }
//        fprintf(stderr, "\n");
    }
    result[result_ptr] = 0;
    *out = result;
}

/**
 *
 * @param in input string
 * @param in_len input string length (chars)
 * @param out output UCS4 encoded
 * @param out_len
 */
static void to_ucs(unsigned char* in, unsigned int in_len, unsigned int **out, unsigned int *out_len) {
    unsigned int i, j;
    unsigned int *result = malloc(in_len * 4);

    if (!result) {
#if defined __linux || defined __linux__
        fprintf(stderr, "to_ucs() malloc failed\n");
#endif
        rpclog("to_ucs() malloc failed\n");
        *out = NULL;
        return;
    }

    for(i = 0, j = 0; i < in_len; i++) {
        if (riscos_ucstable[in[i]] != 0xffffffff) {
            result[j++] = riscos_ucstable[in[i]];
        }
    }
//    fprintf(stderr, "to_ucs in_len:%d j:%d str:'%s'\n", in_len, j, in);
    *out = result;
    *out_len = j;
}

void clipboard_swi(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, uint32_t r4, uint32_t r5, uint32_t *retr0, uint32_t *retr1) {
//    fprintf(stderr, "clipboard_swi: r0:%d r1:%x r2:%d r3:%p\n",
//           r0, r1, r2, r3);
    switch (r0) {
        case ARCEM_SWI_CLIPBOARD_SETUP:
            cliptask_pollword_addr = r1;
            alphabet = r2;
            memcpytohost(riscos_ucstable, r3, 256*4);
//            for(int i = 0; i < 16; i++) {
//                for(int j = 0; j < 16; j++) {
//                    fprintf(stderr, "%04x ", riscos_ucstable[j+i*16]);
//                }
//                fprintf(stderr,"\n");
//            }
            break;
        case ARCEM_SWI_CLIPBOARD_HOST_CHECK: {
            if (host_clipboard != NULL) {
                if (host_clipboard_file_type == 0xfff) {
                    *retr0 = (host_clipboard_len / 4) + 1; // host_clipboard hold UCS32 chars +1 zero byte
                } else {
                    *retr0 = host_clipboard_len;
                }
                *retr1 = host_clipboard_file_type;
            } else {
                *retr0 = 0;
                *retr1 = 0;
            }
//            fprintf(stderr, "ARCEM_SWI_CLIPBOARD_HOST_LEN %d\n", *retr0);
            break;
        }
        case ARCEM_SWI_CLIPBOARD_HOST_GET: {
            // r1 dest addr
            // r2 dest len
            unsigned int cliplen = host_clipboard_len;
            if (r2 < host_clipboard_len) {
                cliplen = r2;
            }
            if (host_clipboard_file_type == 0xfff) {
                unsigned char *clip_text;
                from_ucs((unsigned int *)host_clipboard, host_clipboard_len / 4, &clip_text);
                if (clip_text != NULL) {
//                    fprintf(stderr, "ARCEM_SWI_CLIPBOARD_HOST_GET type: %x len:%d '%s'\n", host_clipboard_file_type, cliplen, clip_text);
                    memcpyfromhost(r1, clip_text, cliplen);
                    free(clip_text);
                }
            } else {
//                fprintf(stderr, "ARCEM_SWI_CLIPBOARD_HOST_GET type: %x len:%d\n", host_clipboard_file_type, cliplen);
                memcpyfromhost(r1, host_clipboard, cliplen);
            }
//            fprintf(stderr, "ARCEM_SWI_CLIPBOARD_HOST_GET %s\n", host_clipboard);
            break;
        }
        case ARCEM_SWI_CLIPBOARD_HOST_SET: {
            // r1 src addr
            // r2 src len
            // r3 file type
            if (host_clipboard) {
                free(host_clipboard);
            }
            host_clipboard_file_type = r3;
            if (host_clipboard_file_type == 0xfff) {
                unsigned char *clip_text = malloc(r2 + 1);
                unsigned int *clip_text_ucs_out = NULL;
                unsigned int clip_text_ucs_len;
                if (!clip_text) {
#if defined __linux || defined __linux__
                    fprintf(stderr, "ARCEM_SWI_CLIPBOARD_HOST_SET malloc failed\n");
#endif
                    rpclog("ARCEM_SWI_CLIPBOARD_HOST_SET malloc failed\n");
                    return;
                }
                memcpytohost(clip_text, r1, r2);
                clip_text[r2] = 0;
                to_ucs(clip_text, r2, &clip_text_ucs_out, &clip_text_ucs_len);
                if (!clip_text_ucs_out) {
                    free(clip_text);
                    return;
                }
                host_clipboard = (char *)clip_text_ucs_out;
                host_clipboard_len = clip_text_ucs_len * 4;
//                fprintf(stderr, "ARCEM_SWI_CLIPBOARD_HOST_SET addr:%x in_len:%d clip_len:%d type:%x '%s'\n", r1, r2, host_clipboard_len, host_clipboard_file_type, clip_text);
                free(clip_text);
            } else {
                host_clipboard = malloc(r2);
                host_clipboard_len = r2;
                memcpytohost(host_clipboard, r1, host_clipboard_len);
//                fprintf(stderr, "ARCEM_SWI_CLIPBOARD_HOST_SET addr:%x len:%d type:%x\n", r1, host_clipboard_len, host_clipboard_file_type);
            }
            rpcemu_set_host_clipboard(host_clipboard_file_type, host_clipboard, host_clipboard_len);
            break;
        }
        default:
            break;
    }
}

// for the text data is in the UCS4
void clipboard_changed_on_host_notify(int file_type, const char *data, unsigned int data_len) {
//    fprintf(stderr, "clipboard_changed_on_host pollword:%x file_type:%x len:%d\n", cliptask_pollword_addr, file_type, data_len);
    char *new_host_clipboard;

    if (!cliptask_pollword_addr) {
        return;
    }
    if (host_clipboard != NULL) {
        if (memcmp(host_clipboard, data, data_len) == 0) {
//            fprintf(stderr, "clipboard_changed_on_host clipboard the same\n");
            return;
        }
    }

    new_host_clipboard = malloc(data_len);
    if (!new_host_clipboard) {
#if defined __linux || defined __linux__
        fprintf(stderr, "can't allocate memory for clipboard! size:%d\n", data_len);
#endif
        rpclog("can't allocate memory for clipboard! size:%d\n", data_len);
        return;
    }
    memcpy(new_host_clipboard, data, data_len);

    if (host_clipboard) {
        free(host_clipboard);
    }
    host_clipboard = new_host_clipboard;
    host_clipboard_file_type = file_type;
    host_clipboard_len = data_len;
    mem_write32(cliptask_pollword_addr, ARCEM_POLLWORD_HOST_CLIPBORARD_CHANGED);
}
