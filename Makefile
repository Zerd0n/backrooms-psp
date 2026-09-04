TARGET = BACKROOMS3D
OBJS = src/main.o src/core/world.o src/core/game.o src/psp/assets.o src/psp/renderer.o src/psp/ui.o src/psp/audio.o src/psp/storage.o
CFLAGS = -std=c99 -O2 -G0 -Wall -Wextra -Werror -MMD -MP
ifeq ($(SMOKE_TEST),1)
CFLAGS += -DBR_SMOKE_TEST
OBJS += tests/psp_smoke.o
endif
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
LIBS = -lpspgum -lpspgu -lpspaudio -lpsppower -lpspdisplay -lpspctrl -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Backrooms PSP
PSP_EBOOT_ICON = assets/ICON0.PNG
PSP_EBOOT_PIC1 = assets/PIC1.PNG
PSP_LARGE_MEMORY = 0
PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
-include $(OBJS:.o=.d)
.PHONY: package verify
package: EBOOT.PBP
	python3 tools/project.py package
verify:
	python3 tools/project.py verify
