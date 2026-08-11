TARGET = BACKROOMS3D
OBJS = src/main.o

CFLAGS = -O2 -G0 -Wall -Wextra -ffast-math
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
LIBS = -lpspaudio -lpspdisplay -lpspctrl -lpspkernel -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Backrooms PSP
PSP_EBOOT_ICON = assets/ICON0.PNG
PSP_EBOOT_PIC1 = assets/PIC1.PNG
PSP_LARGE_MEMORY = 0

PSPSDK := $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

.PHONY: preprocess package verify

preprocess:
	python3 tools/convert_textures.py
	python3 tools/convert_audio.py
	python3 tools/generate_ambient.py

package: EBOOT.PBP
	rm -rf dist/BACKROOMS3D
	mkdir -p dist/BACKROOMS3D/assets
	cp EBOOT.PBP dist/BACKROOMS3D/EBOOT.PBP
	cp assets/chase.raw dist/BACKROOMS3D/assets/chase.raw
	cp assets/ambient_level0.raw dist/BACKROOMS3D/assets/ambient_level0.raw
	cp assets/ambient_poolrooms.raw dist/BACKROOMS3D/assets/ambient_poolrooms.raw

verify: EBOOT.PBP
	python3 tools/verify_project.py
