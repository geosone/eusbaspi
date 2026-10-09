# EUSBASPI - Extended USB ASPI manager for DOS
NASM    = nasm
NFLAGS  = -f bin -w-reloc-abs-word

# EUSBVXD.386 (Windows 3.x enhanced mode helper) needs the OpenWatcom linker
WATCOM ?= /opt/watcom
WLINK   = $(WATCOM)/binl64/wlink

all: build/EUSBASPI.SYS build/ASPITEST.COM build/EUSBVXD.386

build/EUSBASPI.SYS: src/eusbaspi.asm src/usb.inc src/usbinit.inc src/hotplug.inc src/aspi.inc src/defs.inc | build
	$(NASM) $(NFLAGS) -Isrc/ -l build/eusbaspi.lst -o $@ $<

build/ASPITEST.COM: tools/aspitest.asm src/aspi.inc | build
	$(NASM) $(NFLAGS) -Itools/ -Isrc/ -o $@ $<

build/eusbvxd.obj: src/eusbvxd.asm | build
	$(NASM) -f obj -o $@ $<

build/EUSBVXD.386: build/eusbvxd.obj
	WATCOM=$(WATCOM) $(WLINK) format windows vxd name $@ file $< \
	    export EUSBVXD_DDB.1 option quiet,map=build/eusbvxd.map

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: all clean

# distribution: INSTALL.EXE comes from the GeosOne DOS Installer
# (https://github.com/geosone/gdi)
GDI ?= ../gdi/build/INSTALL.EXE
PKG = build/pkg
package: all
	rm -rf $(PKG) && mkdir -p $(PKG)
	cp build/EUSBASPI.SYS build/EUSBVXD.386 build/ASPITEST.COM $(PKG)/
	cp install/INSTALL.INI install/README.TXT $(PKG)/
	sed 's/$$/\r/' LICENSE > $(PKG)/LICENSE.TXT
	cp $(GDI) $(PKG)/INSTALL.EXE
	cd $(PKG) && rm -f ../eusbaspi.zip && zip -q -9 ../eusbaspi.zip *

.PHONY: package
