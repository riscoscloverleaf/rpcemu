# this makefile is for GCCSDK cross-compiler
# to build the module run <your-gccsdk>/env/ro-make

CFLAGS = -Wall -mpoke-function-name -O2 -mlibscl -mthrowback -mmodule -mhard-float -std=gnu99 -DCONFIG_DEBUG -DCONFIG_DEBUG_REPORTER
CMHGFLAGS = -32bit -tgcc 

CC = gcc -c $(CFLAGS) -isystem OSLib:
# needs to link with this OSLib variant: GCCSDK GCC 4.x using SharedCLibrary module (hard-float)
# http://downloads.sourceforge.net/ro-oslib/OSLib-elf-scl-mod-hardfloat-7.00.zip
# I'm just made copy libOSLib32.a file to libOSLib32-mod-hard.a in the GCCSDK env/lib directory to have all variants
LINK = gcc $(CFLAGS) -lOSLib32-mod-hard

OBJS = \
 header \
 debug \
 module \
 ucstables \
 task \
#	Output file

rpcemuhelper,ffa:	$(addsuffix .o, $(OBJS))
	$(LINK) -o $@ $^

#	Object files
%.o: %.c
	$(CC) -o $@ $<

#	cmunge stuff
header.o header.h:	module.cmhg
	cmunge $(CMHGFLAGS) -o header.o -d header.h module.cmhg
