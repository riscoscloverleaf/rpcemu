# rpcemu
RPCEmu is an emulator of Acorn's Risc PC and A7000 machines. With Cloverleaf patches.

This is the modified version of RPCEmu 0.9.3
* Mouse wheel scrolling
* "Startup and Shutdown" options
* Clipboard integration between Host system and RISCOS (with images and text support)
* MacOS support

## Changelog
### 0.4 (2012-08-25)
Applied patch for MacOS from https://github.com/Septercius/rpcemu-dev and build RPCEmu-Interpreter and RPCEmu-Recompiler for
MacOS

### 0.3 (2021-08-12)
1. Implemented images support for host<->guest integrated clipboard. Tested on !Paint and !PrivateEye apps.
* From HOST (RPCEmu side) to GUEST (RISCOS side) image transferred in JPG format. 
RISCOS apps must be able to load JPG to paste from clipboard. !Paint can do this.
* From GUEST to HOST then clipboard images can be transferred in JPG and PNG formats. 
Sprites doest not supported right now, it will be implemented later. 
Copy from !Paint will not work as it copies the sprite. Copy from !PrivateEye a JPG and PNG files is work.
* As RISCOS does not have events to monitor the clipboard contents changes then it must be polled and compared with old content and if a big image copied to clipboard then a system can run a little slowly.
2. To convert the clipboard text to/from RISCOS encoding I used RISCOS UCS conversion table (serviceinternational_get_ucs_conversion_table() function) instead of iconv. 
Windows version become smaller again as linking with iconv is not required. 
Note: right now the conversion table is retrieved at the start of the module so if you change the alphabet later then it will not updated. Will be fixed later.

### 0.2 (2021-07-22)
Implemented "Startup and Shutdown" options: "Start in full screen" and "Exit RPCEmu on RISCOS Shutdown".

### 0.1 (2021-06-28)
Implemented mouse wheel scrolling and text clipboard integration between Host system and RISCOS.