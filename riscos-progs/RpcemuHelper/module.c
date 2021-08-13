/*
* Copyright (c) 2021, RiscOS Cloverleaf
* https://riscoscloverleaf.com/
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <swis.h>
#include <oslib/os.h>
#include <oslib/osmodule.h>
#include <oslib/taskmanager.h>

#include "debug.h"
#include "header.h"
#include "task.h"

#define EnableEvent 14
#define DisableEvent 13
#define KeypressEvent 11
#define MouseEvent 10

#define State_Alt_Key_Down 1
#define State_PG_Key_Down 2
#define State_PG_Key_Up 3

extern void __module_header(void); /* Not an actual function */

wimp_t taskhandle = 0;
int pollword = 0;
void *private_word = NULL;

static int alt_key_down = 0;
static int ctrl_key_down = 0;
static int mouse_buttons = 0;

static _kernel_oserror *module_initialise(void *pw) {
    private_word = pw;
    _kernel_oserror *e = _swix(OS_Claim, _INR (0, 2), EventV, EventV_event, pw);
    if (!e) e = _swix(OS_Byte, _INR (0, 1), EnableEvent, KeypressEvent);
    if (!e) e = _swix(OS_Byte, _INR (0, 1), EnableEvent, MouseEvent);
    return e;
}

static _kernel_oserror *module_finalise(void *pw) {
    _kernel_oserror *e;
    e = task_shutdown();
    if (!e) e = _swix(OS_Byte, _INR (0, 1), DisableEvent, MouseEvent);
    if (!e) e = _swix(OS_Byte, _INR (0, 1), DisableEvent, KeypressEvent);
    if (!e) e = _swix(OS_Release, _INR (0, 2), EventV, EventV_event, pw);
    return e;
}

_kernel_oserror *mod_init(const char *tail __attribute__ ((unused)),
                          int podule_base __attribute__ ((unused)),
                          void *pw) {
    _kernel_oserror *e;
    if ((e = module_initialise(pw)) != NULL)
        module_finalise(pw);
    return e;
}


_kernel_oserror *
mod_final(int fatal __attribute__ ((unused)),
          int podule_base __attribute__ ((unused)),
          void *pw) {
    return module_finalise(pw);
}

void mod_service(int service, _kernel_swi_regs *r, void *pw) {
    if (service == Service_Memory && (r->r[2] == (void*)__module_header)) {
        r->r[1] = 0;
    }
}


_kernel_oserror *mod_command_handler(const char *args, int argc, int cmdno, void *pw) {
    os_error *err;

    switch (cmdno) {
        case CMD_rpcemuhelper_debug:
            debug_report = atoi(args);
            break;

        case CMD_rpcemuhelper_start_task:
            if (taskhandle == NULL)
            {
                taskhandle = -1;
            }
            else if (taskhandle != -1)
            {
                return NULL;
            }
            //dprintf("xosmodule_enter");
            err = xosmodule_enter(Module_Title, "");
            if (err)
            {
                //dprintf("xosmodule_enter error");
                taskhandle = 0;
                return (_kernel_oserror *) err;
            }
            break;
        case CMD_rpcemuhelper_start:
            if (taskhandle == 0)
            {
                taskhandle = -1;
                //dprintf("xtaskmanager_start_task rpcemuhelper_start_task");
                if (xtaskmanager_start_task("rpcemuhelper_start_task"))
                {
                    taskhandle = 0;
                }
            }
            break;
    }
    return NULL;      // or pointer to error-block
}

void do_scroll(int wheel_direction) {
    int b[10];
    int relz = wheel_direction;
    _swix (Wimp_GetPointerInfo, _IN(1), b);
    b[0] = b[3];
    if((wheel_direction) && (b[0] != -1) && !_swix (Wimp_GetWindowState, _IN(1), b)) // valid window handle
    {
        /* does it do scroll requests? */
        if (b[8] & ((1 << 8) | (1 << 9))) {
            /* Only scroll if scrollbars are present */
            if (b[8] & (1 << 31)) {
                /* New format window flags word */
                if (!(b[8] & (1 << 28)))
                    relz = 0;
            } else {
                /* Old format window flags word */
                if (!(b[8] & (1 << 2)))
                    relz = 0;
            }
            relz *= 5; // a bit more movement...
            while (relz) {
                b[9] = (relz ? (relz > 0 ? -1 : 1) : 0);
                relz += b[9];
                _swix(Wimp_SendMessage, _INR(0, 2), 10, b, b[0]);
            }
        }
    }
}

/* the handler itself... */
extern int EventV_event_handler(_kernel_swi_regs *r, void *pw __attribute__ ((unused))) {
    if (r->r[0] == 11) { // key event
        /* switch on the event code */
        switch(r->r[2]) {
            case os_TRANSITION_KEY_LEFT_CONTROL:
            case os_TRANSITION_KEY_RIGHT_CONTROL:
                if (r->r[1] == os_TRANSITION_DOWN) {
                    ctrl_key_down = 1;
                } else {
                    ctrl_key_down = 0;
                }
                break;
            case os_TRANSITION_KEY_LEFT_ALT:
                if (r->r[1] == os_TRANSITION_DOWN) {
                    alt_key_down = 1;
                } else {
                    alt_key_down = 0;
                }
                break;
            case os_TRANSITION_KEY_PAGE_DOWN:
                if (r->r[1] == os_TRANSITION_DOWN) {
                    if (alt_key_down) {
                        do_scroll(1);
                        return 0;
                    }
                } else {
                    if (alt_key_down) {
                        return 0;
                    }
                }
                break;
            case os_TRANSITION_KEY_PAGE_UP:
                if (r->r[1] == os_TRANSITION_DOWN) {
                    if (alt_key_down) {
                        do_scroll(-1);
                        return 0;
                    }
                } else {
                    if (alt_key_down) {
                        return 0;
                    }
                }
                break;
            default:
                if (ctrl_key_down) {
                    task_clipboard_tick();
                }
                break;
        }
    } else { // mouse event
        int delta = mouse_buttons ^ r->r[3];
        mouse_buttons = r->r[3];
        if (delta & 7) {
            task_clipboard_tick();
        }
    }
    return 1;
}
