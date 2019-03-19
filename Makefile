#
# Makefile
#

CIRCLEHOME = ../..

OBJS = lowlevel_arm.o gpio_defs.o latch.o oled.o ./OLED/ssd1306xled.o ./OLED/ssd1306xled8x16.o ./OLED/num2str.o 

ifeq ($(kernel), cart)
OBJS += kernel_cart.o
endif

ifeq ($(kernel), ram)
OBJS += kernel_georam.o
endif

ifeq ($(kernel), sid)
OBJS += kernel_sid.o sound.o ./resid/dac.o ./resid/filter.o ./resid/envelope.o ./resid/extfilt.o ./resid/pot.o ./resid/sid.o ./resid/version.o ./resid/voice.o ./resid/wave.o fmopl.o 
endif

ifeq ($(kernel), sid)
LIBS	= $(CIRCLEHOME)/addon/vc4/sound/libvchiqsound.a \
   	      $(CIRCLEHOME)/addon/vc4/vchiq/libvchiq.a \
	      $(CIRCLEHOME)/addon/linux/liblinuxemu.a

CFLAGS += -DUSE_VCHIQ_SOUND=$(USE_VCHIQ_SOUND) 
endif

CFLAGS += -Wno-comment

LIBS += $(CIRCLEHOME)/lib/usb/libusb.a \
	    $(CIRCLEHOME)/lib/input/libinput.a \
	    $(CIRCLEHOME)/lib/fs/libfs.a \
	    $(CIRCLEHOME)/lib/sched/libsched.a \
        $(CIRCLEHOME)/lib/libcircle.a 

include ../Rules.mk

