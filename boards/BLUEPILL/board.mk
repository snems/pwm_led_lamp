# List of all the board related files.
BOARDSRC = $(CHIBIOS)/../boards/BLUEPILL/board.c

# Required include directories
BOARDINC = $(CHIBIOS)/../boards/BLUEPILL

# Shared variables
ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
