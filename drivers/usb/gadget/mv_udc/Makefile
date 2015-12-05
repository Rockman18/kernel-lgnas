#
# Makefile for the Marvell USB device controller
#
OBJS := mv_udc_main.o mvUsbDevCh9.o mvUsbDevMain.o	\
				mvUsbDevRecv.o mvUsbDevSend.o mvUsbDevUtl.o	\
				mvUsbHsDevUtl.o mvUsbHsDevMain.o mvUsbHsDevCncl.o

mv_udc-objs := $(OBJS)
obj-$(CONFIG_USB_MRVL) += mv_udc.o
