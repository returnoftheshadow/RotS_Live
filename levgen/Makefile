# Builds levgen, the maze-level generator that makmaz runs over the *.lev files.
# Add -DWIN32 to CPPFLAGS for a Windows build.

MAKEFLAGS += --no-builtin-rules
.SUFFIXES:
.DELETE_ON_ERROR:

ifeq ($(origin CC),default)
CC := gcc
endif
# -w silences the legacy sources' warnings; drop it to see them.
CFLAGS ?= -g -w -fstrict-aliasing
DEPFLAGS = -MMD -MP

OBJFILES := levgen.o config.o maz.o zon.o wld.o
DEPFILES := $(OBJFILES:.o=.d)

.DEFAULT_GOAL := all
.PHONY: all clean

all: levgen

levgen: $(OBJFILES)
	$(CC) $(LDFLAGS) -o $@ $(OBJFILES) $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(DEPFILES)

clean:
	$(RM) levgen $(OBJFILES) $(DEPFILES)
