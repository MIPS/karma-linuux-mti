#
# Makefile for the linux kernel.
#

obj-y			:= irq.o clk.o prom.o setup.o l4_dma.o memory.o

obj-$(CONFIG_OF)	+= virt.dtb.o

$(obj)/%.dtb: $(obj)/%.dts
	$(call if_changed,dtc)
