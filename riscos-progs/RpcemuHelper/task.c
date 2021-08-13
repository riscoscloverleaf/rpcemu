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

/*
* Cliupboard monitoring task
* Much of code taken from vncserver by Jeffrey Lee
*/

#include <oslib/os.h>
#include <oslib/wimp.h>
#include <oslib/osfile.h>
#include <oslib/osmodule.h>
#include <oslib/osfind.h>
#include <oslib/osgbpb.h>
#include <oslib/osargs.h>
#include <oslib/taskmanager.h>
#include <oslib/osbyte.h>
#include <oslib/territory.h>
#include <oslib/serviceinternational.h>
#include <swis.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "module.h"
#include "task.h"
#include "header.h"
#include "ucstables.h"
#include "debug.h"

static os_t clipboard_check_time = 0;
static bool own_clipboard = false;

static const int accepted_messages[] = {
        message_DATA_SAVE,
        message_DATA_SAVE_ACK,
        message_DATA_LOAD,
        message_RAM_FETCH,
        message_RAM_TRANSMIT,
        message_CLAIM_ENTITY,
        message_DATA_REQUEST,
        0
};

/* Check clipboard content 25cs after last key/mouse press */
#define CLIPBOARD_CHECK_DELAY 25

#define POLLWORD_PUT 1
#define POLLWORD_CLIPBOARD_TICK 2
#define POLLWORD_RELEASE 3
#define POLLWORD_TICK 4

static clipboard_content *clip_content = NULL; /* Text we're hosting on the clipboard (from host clipboard) */
static clipboard_content *host_clip_content = NULL; /* Text that was last sent to the host */
static int paste_msg = 0;
static int paste_offset = 0;
static int check_msg = 0;
static int fetch_msg = 0;

static clipboard_content *clipboard_alloc(const char *text, int len, int remain, int file_type) {
    clipboard_content *ret;
    if (xosmodule_alloc(sizeof(clipboard_content) + len + remain + 4, (void **) &ret)) {
        return NULL;
    }
    ret->write_offset = len;
    ret->len = len + remain;
    if (len) {
        memcpy(ret->content, text, len);
    }
    ret->content[len + remain] = 0;
    ret->file_type = file_type; // set default file type "text"
    dprintf("rpcemuhelper task: Allocated buffer addr:%p len:%d\n", ret, sizeof(clipboard_content) + len + remain + 4);
    return ret;
}

static void clipboard_free(clipboard_content **clip) {
    xos_int_off();
    clipboard_content *local = *clip;
    *clip = NULL;
    xos_int_on();
    if (local) {
        dprintf("rpcemuhelper task: Free buffer addr:%p\n", local);
        xosmodule_free(local);
    }
}

void task_clipboard_tick() {
    if (own_clipboard) {
        return;
    }
    /* Check clipboard in the future */
    if (!xos_read_monotonic_time(&clipboard_check_time)) {
        clipboard_check_time += CLIPBOARD_CHECK_DELAY;
        if (!pollword) {/* All we really care about is waking up the task, so don't clobber any existing pollword */
            pollword = POLLWORD_CLIPBOARD_TICK;
        }
    }
}

static void push_to_host()
{
    if (host_clip_content && (host_clip_content->len == clip_content->len) && !memcmp(host_clip_content->content, clip_content->content, clip_content->len))
    {
        /* No change */
        clipboard_free(&clip_content);
        return;
    }
    dprintf("rpcemuhelper task: Pushing new clipboard content to host\n");
    xos_int_off();
    clipboard_content *old = host_clip_content;
    host_clip_content = clip_content;
    clip_content = NULL;
    xos_int_on();
    clipboard_free(&old);

    _swix(ARCEM_SWI_CLIPBOARD, _INR(0, 3), ARCEM_SWI_CLIPBOARD_HOST_SET, host_clip_content->content,
          host_clip_content->len, host_clip_content->file_type);
}

static void atexit_handler() {
    taskhandle = 0;
    dprintf("rpcemuhelper task: Stopping\n");
}

int main(int argc, char **argv) {
    atexit(atexit_handler);
    //dprintf("rpcemuhelper task: Starting0\n");
    os_error *error;
    int alphabet;
    unsigned int *ucs_ostable;
    osbool unclaimed;

    pollword = 0;
    own_clipboard = false;

    dprintf("rpcemuhelper task: Starting\n");

    wimp_version_no version;
    taskhandle = wimp_initialise(wimp_VERSION_RO3, Module_Title, (wimp_message_list const *) accepted_messages,
                                 &version);

    /* Shrink our wimpslot to minimum */
    wimp_slot_size(1, -1, NULL, NULL, NULL);

    error = xosbyte1(osbyte_ALPHABET_NUMBER, 127, 0, &alphabet);
    if (error)
        alphabet = territory_ALPHABET_LATIN1;

    error = xserviceinternational_get_ucs_conversion_table(alphabet, &unclaimed, (void **)&ucs_ostable);
    if (error != NULL) {
        dprintf("failed reading UCS conversion table: 0x%x: %s", error->errnum, error->errmess);
        /* Try using our own table instead */
        ucs_ostable = ucstable_from_alphabet(alphabet);
    } else if (unclaimed) {
        /* Service wasn't claimed so use our own ucstable */
        dprintf("serviceinternational_get_ucs_conversion_table unclaimed, using internal conversion table");
        ucs_ostable = ucstable_from_alphabet(alphabet);
    }


    // tell RPCEmu host our pollword address and used alphabet
    _swix(ARCEM_SWI_CLIPBOARD, _INR(0, 3), ARCEM_SWI_CLIPBOARD_HOST_SETUP, &pollword, alphabet, ucs_ostable);

    clipboard_check_time = os_read_monotonic_time() + CLIPBOARD_CHECK_DELAY;

    while (1) {
        wimp_block block;
        wimp_event_no event;
        if (own_clipboard || !clipboard_check_time) {
            event = wimp_poll(wimp_MASK_NULL | wimp_GIVEN_POLLWORD | wimp_SAVE_FP, &block, &pollword);
        } else {
            /* Wake up at indicated time to check clipboard contents */
            event = wimp_poll_idle(wimp_GIVEN_POLLWORD | wimp_SAVE_FP, &block, clipboard_check_time, &pollword);
        }
        //dprintf("rpcemuhelper task: Wimp event %d\n", event);
        switch (event) {
            case wimp_NULL_REASON_CODE:
                if (!own_clipboard) {
                    os_t delta = os_read_monotonic_time() - clipboard_check_time;
                    if (delta >= 0) {
                        clipboard_check_time = 0;
                        dprintf("rpcemuhelper task: Checking clipboard content\n");
                        clipboard_free(&clip_content);
                        block.message.size = sizeof(wimp_full_message_data_request) -
                                             sizeof(block.message.data.data_request.file_types) + 8;
                        block.message.your_ref = 0;
                        block.message.action = message_DATA_REQUEST;
                        block.message.data.data_request.w = wimp_NO_ICON;
                        block.message.data.data_request.i = 0;
                        block.message.data.data_request.pos.x = 0;
                        block.message.data.data_request.pos.y = 0;
                        block.message.data.data_request.flags = wimp_DATA_REQUEST_CLIPBOARD;
                        block.message.data.data_request.file_types[0] = 0xfff;
                        block.message.data.data_request.file_types[1] = 0xc85;
                        block.message.data.data_request.file_types[2] = 0xb60;
                        block.message.data.data_request.file_types[3] = -1;
                        if (xwimp_send_message(wimp_USER_MESSAGE_RECORDED, &block.message, wimp_BROADCAST)) {
                            dprintf("rpcemuhelper task: Data request failed\n");
                        } else {
                            check_msg = block.message.my_ref;
                        }
                    }
                }
                break;
            case wimp_POLLWORD_NON_ZERO: {
                xos_int_off();
                int poll = pollword;
                pollword = 0;
                dprintf("rpcemuhelper task: Pollword: %d\n", poll);
                xos_int_on();
                switch (poll) {
                    case POLLWORD_HOST_CLIPBORARD_CHANGED: {
                        int host_clip_len, host_file_type;

                        // get host's clipboard length
                        _swix(ARCEM_SWI_CLIPBOARD, _IN(0) | _OUT(0) | _OUT(1), ARCEM_SWI_CLIPBOARD_HOST_CHECK, &host_clip_len, &host_file_type);
//                        dprintf("rpcemuhelper task: Host clipboard content received, len: %d\n", host_clip_len);
                        clipboard_free(&clip_content);
                        if (host_clip_len) {
                            paste_msg = 0; /* Abort any current paste op */
                            check_msg = 0;
                            fetch_msg = 0;
                            // allocate memory for host clipboard content
                            dprintf("rpcemuhelper task: Allocating buffer for host clipboard content, len:%d\n", host_clip_len);
                            clip_content = clipboard_alloc(NULL, 0, host_clip_len, host_file_type);
//                            dprintf("rpcemuhelper task: Host clipboard content received, len2: %d\n", clip_content->len);
                            // transfer clipboard content from host to clip_content
                            _swix(ARCEM_SWI_CLIPBOARD, _INR(0, 2), ARCEM_SWI_CLIPBOARD_HOST_GET, clip_content->content,
                                  clip_content->len);
                            if (clip_content->file_type == 0xfff) {
                                dprintf("rpcemuhelper task: Host clipboard content received, len:%d type:%x '%s'\n", clip_content->len, clip_content->file_type, clip_content->content);
                            } else {
                                dprintf("rpcemuhelper task: Host clipboard content received, len:%d type:%x\n", clip_content->len, clip_content->file_type);
                            }
                            if (!own_clipboard) {
                                /* Claim the clipboard */
                                dprintf("rpcemuhelper task: Host clipboard content received, claiming clipboard...\n");
                                block.message.size = sizeof(wimp_full_message_claim_entity);
                                block.message.your_ref = 0;
                                block.message.action = message_CLAIM_ENTITY;
                                block.message.data.claim_entity.flags = wimp_CLAIM_CLIPBOARD;
                                own_clipboard = true;
                                if (xwimp_send_message(wimp_USER_MESSAGE, &block.message, wimp_BROADCAST)) {
                                    dprintf("rpcemuhelper task: Claim failed!\n");
                                    own_clipboard = false;
                                }
                            } else {
                                dprintf("rpcemuhelper task: Client content received, clipboard already owned\n");
                            }
                        } else {
                            dprintf("rpcemuhelper task: Pollword non-zero but no host clipboard content to receive\n");
                        }
                        break;
                    }
                    case POLLWORD_CLIPBOARD_TICK:
                    default:
                        break;
                }
                break;
            }
            case wimp_USER_MESSAGE:
            case wimp_USER_MESSAGE_RECORDED:
            case wimp_USER_MESSAGE_ACKNOWLEDGE: {
                switch (block.message.action) {
                    case message_QUIT: {
                        _swix(ARCEM_SWI_CLIPBOARD, _INR(0, 1), ARCEM_SWI_CLIPBOARD_HOST_SETUP, NULL, 0);
                        return 0;
                    }
                    case message_CLAIM_ENTITY: {
                        if ((block.message.sender != taskhandle) &&
                            (block.message.data.claim_entity.flags & wimp_CLAIM_CLIPBOARD)) {
                            dprintf("rpcemuhelper task: Other task %08x now owns clipboard\n", block.message.sender);
                            if (own_clipboard) {
                                clipboard_free(&clip_content);
                                own_clipboard = false;
                            }
                            task_clipboard_tick();
                        } else {
                            //              dprintf("rpcemuhelper task: Ignoring message_CLAIM_ENTITY %08x %08x\n",block.message.sender, block.message.data.claim_entity.flags);
                        }
                        break;
                    }

                    /* Messages for when we own the clipboard */
                    case message_DATA_REQUEST: {
                        if (!(block.message.data.data_request.flags & wimp_DATA_REQUEST_CLIPBOARD)) {
                            dprintf("rpcemuhelper task: Ignoring unknown message_DATA_REQUEST flags %08x\n",
                                    block.message.data.data_request.flags);
                            break;
                        } else if (!own_clipboard) {
                            dprintf("rpcemuhelper task: Received data request but don't own clipboard (sender %08x)\n", block.message.sender);
                        } else if (!clip_content) {
                            dprintf("rpcemuhelper task: Received data request but don't have any clip content!\n");
                        } else {
                            wimp_t target = block.message.sender;
                            dprintf("rpcemuhelper task: Received data request from %08x, offering data save\n", target);
                            sprintf(block.message.data.data_xfer.file_name, "RPCEmu-clipboard");
                            block.message.size =
                                    sizeof(wimp_full_message_data_xfer) -
                                    sizeof(block.message.data.data_xfer.file_name) +
                                    ((strlen(block.message.data.data_xfer.file_name) + 4) & ~3);
                            block.message.your_ref = block.message.my_ref;
                            block.message.action = message_DATA_SAVE;
                            /* window, icon, pos preserved */
                            block.message.data.data_xfer.est_size = clip_content->len;
                            block.message.data.data_xfer.file_type = clip_content->file_type;
                            if (xwimp_send_message(wimp_USER_MESSAGE, &block.message, target)) {
                                dprintf("rpcemuhelper task: message_DATA_SAVE failed!\n");
                            } else {
                                dprintf("rpcemuhelper task: message_DATA_SAVE my_ref = %08x\n", block.message.my_ref);
                                paste_msg = block.message.my_ref;
                                paste_offset = 0;
                            }
                        }
                        break;
                    }
                    case message_DATA_SAVE_ACK: {
                        dprintf("rpcemuhelper task: message_DATA_SAVE_ACK, your_ref = %08x, paste_msg=%08x filename = \"%s\"\n",
                                block.message.your_ref,
                                paste_msg,
                                block.message.data.data_xfer.file_name);
                        if (block.message.your_ref == paste_msg) {
                            paste_msg = 0;
                            if (xosfile_save_stamped(block.message.data.data_xfer.file_name,
                                                     block.message.data.data_xfer.file_type, clip_content->content,
                                                     clip_content->content + clip_content->len)) {
                                dprintf("rpcemuhelper task: Save failed!\n");
                            } else {
                                dprintf("rpcemuhelper task: Save OK, sending load message\n");
                                block.message.your_ref = block.message.my_ref;
                                block.message.action = message_DATA_LOAD;
                                xwimp_send_message(wimp_USER_MESSAGE_RECORDED, &block.message, block.message.sender);
                            }
                        }
                        break;
                    }
                    case message_RAM_FETCH: {
                        dprintf("rpcemuhelper task: message_DATA_RAM_FETCH, your_ref = %08x fetch_msg=%08x paste_msg=%08x \n", block.message.your_ref, fetch_msg, paste_msg);
                        if (block.message.your_ref == paste_msg) {
                            /* We're attempting to paste content */
                            int len = block.message.data.ram_xfer.size;
                            if (len >= clip_content->len - paste_offset) {
                                len = clip_content->len - paste_offset;
                            }
                            if (len &&
                                xwimp_transfer_block(taskhandle, clip_content->content + paste_offset,
                                                     block.message.sender,
                                                     block.message.data.ram_xfer.addr, len)) {
                                dprintf("rpcemuhelper task: RAM transfer failed!\n");
                                paste_msg = 0;
                            } else {
                                dprintf("rpcemuhelper task: RAM transfer OK, sending ACK message\n");
                                block.message.your_ref = block.message.my_ref;
                                block.message.action = message_RAM_TRANSMIT;
                                wimp_event_no evt = (block.message.data.ram_xfer.size == len
                                                     ? wimp_USER_MESSAGE_RECORDED
                                                     : wimp_USER_MESSAGE); /* Send recorded messages until we transfer less data than was requested */
                                block.message.data.ram_xfer.size = len;
                                xwimp_send_message(evt, &block.message, block.message.sender);
                                paste_offset += len;
                                paste_msg = (evt == wimp_USER_MESSAGE ? 0 : block.message.my_ref);
                            }
                        }
//                        } else if (block.message.your_ref == fetch_msg) {
//                            /* We're attempting to get content */
//                            dprintf("rpcemuhelper task: RAM fetch bounced, trying data save instead\n");
//                            fetch_msg = 0;
//
//                            sprintf(block.message.data.data_xfer.file_name, "<Wimp$Scrap>");
//                            block.message.size =
//                                    sizeof(wimp_full_message_data_xfer) -
//                                    sizeof(block.message.data.data_xfer.file_name) +
//                                    ((strlen(block.message.data.data_xfer.file_name) + 4) & ~3);
//                            block.message.your_ref = check_msg;
//                            block.message.action = message_DATA_SAVE_ACK;
//                            block.message.data.data_xfer.w = wimp_NO_ICON;
//                            block.message.data.data_xfer.i = 0;
//                            block.message.data.data_xfer.pos.x = 0;
//                            block.message.data.data_xfer.pos.y = 0;
//                            block.message.data.data_xfer.est_size = clip_content->len;
//                            block.message.data.data_xfer.file_type = clip_content->file_type;
//                            if (xwimp_send_message(wimp_USER_MESSAGE_RECORDED, &block.message, block.message.sender)) {
//                                dprintf("rpcemuhelper task: message_DATA_SAVE_ACK failed!\n");
//                                check_msg = 0;
//                            } else {
//                                /* Wait for the data load */
//                                check_msg = block.message.my_ref;
//                            }
//                        }
                    }
                        break;

                    /* Messages for when we're polling the clipboard */
                    case message_DATA_SAVE: {
                        if (block.message.your_ref == check_msg) {
                            dprintf("rpcemuhelper task: Task %08x owns clipboard data of type %03x size %d\n", block.message.sender,
                                    block.message.data.data_xfer.file_type, block.message.data.data_xfer.est_size);
                            check_msg = 0;
                            if ((block.message.data.data_xfer.file_type == 0xfff
                                || block.message.data.data_xfer.file_type == 0xc85
                                || block.message.data.data_xfer.file_type == 0xb60) &&
                                (block.message.data.data_xfer.est_size > 0)) {
                                if (clip_content) {
                                    dprintf("rpcemuhelper task: Free old clipboard buffer, addr: %p\n", clip_content);
                                    clipboard_free(&clip_content);
                                }
                                dprintf("rpcemuhelper task: Allocating new clipboard buffer, est_size: %d\n", block.message.data.data_xfer.est_size);
                                clip_content = clipboard_alloc(NULL, 0, block.message.data.data_xfer.est_size, block.message.data.data_xfer.file_type);
                                if (clip_content) {
                                    check_msg = block.message.my_ref;
                                    if (clip_content->file_type == 0xfff) {
                                        dprintf("rpcemuhelper task: Trying RAM fetch myref=%08x\n", block.message.my_ref);
                                        block.message.size = sizeof(wimp_full_message_ram_xfer);
                                        block.message.your_ref = block.message.my_ref;
                                        block.message.action = message_RAM_FETCH;
                                        block.message.data.ram_xfer.addr = clip_content->content;
                                        block.message.data.ram_xfer.size = clip_content->len +
                                                                           1; /* RAM transmit terminates when we receive less than we requested, so always request one more byte than necessary (which, if received, will overwrite our null terminator) */
                                        if (xwimp_send_message(wimp_USER_MESSAGE_RECORDED, &block.message,
                                                               block.message.sender)) {
                                            dprintf("rpcemuhelper task: message_RAM_FETCH failed!\n");
                                            check_msg = 0;
                                        } else {
                                            dprintf("rpcemuhelper task: message_DATA_SAVE fetch_msg: %08x\n", block.message.my_ref);
                                            fetch_msg = block.message.my_ref;
                                        }
                                    } else {
                                        dprintf("rpcemuhelper task: file type is not text, use data save instead RAM fetch\n");
                                        fetch_msg = 0;

                                        sprintf(block.message.data.data_xfer.file_name, "<Wimp$Scrap>");
                                        block.message.size =
                                                sizeof(wimp_full_message_data_xfer) -
                                                sizeof(block.message.data.data_xfer.file_name) +
                                                ((strlen(block.message.data.data_xfer.file_name) + 4) & ~3);
                                        block.message.your_ref = check_msg;
                                        block.message.action = message_DATA_SAVE_ACK;
                                        block.message.data.data_xfer.w = wimp_NO_ICON;
                                        block.message.data.data_xfer.i = 0;
                                        block.message.data.data_xfer.pos.x = 0;
                                        block.message.data.data_xfer.pos.y = 0;
                                        block.message.data.data_xfer.est_size = clip_content->len;
                                        block.message.data.data_xfer.file_type = clip_content->file_type;
                                        if (xwimp_send_message(wimp_USER_MESSAGE_RECORDED, &block.message, block.message.sender)) {
                                            dprintf("rpcemuhelper task: message_DATA_SAVE_ACK failed!\n");
                                            check_msg = 0;
                                        } else {
                                            /* Wait for the data load */
                                            check_msg = block.message.my_ref;
                                            dprintf("rpcemuhelper task: message_DATA_SAVE_ACK sent, check_msg: %08x\n", check_msg);
                                        }
                                    }
                                }
                            }
                        }
                    }
                        break;
                    case message_RAM_TRANSMIT: {
                        dprintf("rpcemuhelper task: message_RAM_TRANSMIT, your_ref = %08x fetch_msg=%08x\n", block.message.your_ref, fetch_msg);
                        if (block.message.your_ref == fetch_msg) {
                            dprintf("rpcemuhelper task: Received %d/%d bytes via RAM transfer\n", block.message.data.ram_xfer.size,
                                    clip_content->len - clip_content->write_offset);
                            check_msg = 0;
                            clip_content->write_offset += block.message.data.ram_xfer.size;
                            if (clip_content->write_offset > clip_content->len) {
                                /* Oh dear, the estimated length was bogus
                                   Just reset our position and go round again
                                   (will result in us only getting the end of the clipboard,
                                   but it will avoid the RAM transfer protocol stalling) */
                                clip_content->write_offset = 0;
                                block.message.size = sizeof(wimp_full_message_ram_xfer);
                                block.message.your_ref = block.message.my_ref;
                                block.message.action = message_RAM_FETCH;
                                block.message.data.ram_xfer.addr = clip_content->content;
                                block.message.data.ram_xfer.size = clip_content->len + 1;
                                if (xwimp_send_message(wimp_USER_MESSAGE_RECORDED, &block.message,
                                                       block.message.sender)) {
                                    dprintf("rpcemuhelper task: message_RAM_FETCH failed!\n");
                                    fetch_msg = 0;
                                } else {
                                    fetch_msg = block.message.my_ref;
                                }
                            } else {
                                /* Transaction complete, push to server */
                                fetch_msg = 0;
                                clip_content->len = clip_content->write_offset; /* Just in case it was less than we were originall told */
                                clip_content->content[clip_content->len] = 0; /* And make sure we put the terminator back, in case it was overwritten */
                                push_to_host();
                            }
                        } else if (block.message.your_ref == paste_msg) {
                            dprintf("rpcemuhelper task: message_RAM_TRANSMIT failed!\n");
                            /* Just give up then */
                            paste_msg = 0;
                        }
                        break;
                    }
                    case message_DATA_LOAD: {
                        if (block.message.your_ref == check_msg) {
                            dprintf("rpcemuhelper task: Data save worked OK, file '%s'\n", block.message.data.data_xfer.file_name);
                            check_msg = 0;
                            os_fw f = 0;
                            xosfind_openinw(osfind_NO_PATH, block.message.data.data_xfer.file_name, NULL, &f);
                            bool ok = (f != 0);
                            int unread;
                            if (ok && xosgbpb_readw(f, clip_content->content, clip_content->len, &unread)) ok = false;
                            if (ok && unread) ok = false;
                            osbool eof;
                            if (ok && xosargs_read_eof_statusw(f, &eof)) ok = false;
                            if (ok && !eof) ok = false;
                            if (f) xosfind_closew(f);
                            if (ok) {
                                dprintf("rpcemuhelper task: Data read OK\n");
                                /* Ack the message */
                                block.message.your_ref = block.message.my_ref;
                                block.message.action = message_DATA_LOAD_ACK;
                                xwimp_send_message(wimp_USER_MESSAGE, &block.message, block.message.sender);

                                push_to_host();
                            } else {
                                dprintf("rpcemuhelper task: Data load failed!\n");
                            }
                        }
                    }
                        break;
                }
                break;
            }
            default:
                dprintf("rpcemuhelper task: Unhandled wimp message %d %08x\n", event, block.message.action);
                break;
        }
    }
    return 0;
}

os_error *task_shutdown() {
    if ((taskhandle != NULL) && (taskhandle != -1)) {
        os_error *err = xwimp_close_down((wimp_t) taskhandle);
        if (err) {
            return err;
        }
        dprintf("rpcemuhelper task: Killed\n");
        taskhandle = 0;
    }
    /* Release any RMA blocks */
    clipboard_free(&clip_content);
    clipboard_free(&host_clip_content);
    return NULL;
}
