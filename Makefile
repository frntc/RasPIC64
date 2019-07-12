#
# Makefile
#

CIRCLEHOME = ../..

OBJS = lowlevel_arm.o gpio_defs.o latch.o oled.o ./oled/ssd1306xled.o ./oled/ssd1306xled8x16.o ./oled/num2str.o 

#OBJS += kernel_ef.o
#OBJS += kernel_cart.o

ifeq ($(kernel), cart)
OBJS += kernel_cart.o
endif

ifeq ($(kernel), ef)
OBJS += kernel_ef.o crt.o
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
 	    $(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	    $(CIRCLEHOME)/lib/fs/libfs.a \
		$(CIRCLEHOME)/addon/fatfs/libfatfs.a \
	    $(CIRCLEHOME)/lib/sched/libsched.a \
        $(CIRCLEHOME)/lib/libcircle.a 

#	    $(CIRCLEHOME)/lib/fs/fat/libfatfs.a \


include ../Rules.mk

