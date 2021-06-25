/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of Miscellaneous, Fake, Hypervisor machines.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/sio.h>
#include <86box/hwm.h>
#include <86box/spd.h>
#include <86box/video.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/sound.h>


#ifdef _WIN32
char *vpc_paths[] = {
    /* Virtual PC 2004/2007 */
    "%ProgramFiles%\\Microsoft Virtual PC\\Virtual PC.exe",
# ifdef _WIN64
    "%ProgramFiles(x86)%"
# else
    "%ProgramW6432%"
# endif
    "\\Microsoft Virtual PC\\Virtual PC.exe",

    /* Windows Virtual PC */
    "%SystemRoot%\\System32\\vpc.exe",
# ifndef _WIN64
    "%SystemRoot%\\Sysnative\\vpc.exe",
# endif

    NULL
};
#endif


int
machine_at_vpc2007_init(const machine_t *model)
{
    int ret, i;

    ret = bios_load_linear("roms/machines/vpc2007/13500.bin",
			   0x000c0000, 262144, 0);

#ifdef _WIN32
    /* Load ROM from an installed copy of Virtual PC if required. */
    uint32_t size, offset;
    for (i = 0; !ret && vpc_paths[i]; i++) {
	size = rom_get_pe_resource(vpc_paths[i], "BIOS", 13500, -1, &offset);
	if (!size)
		continue;
	ret = bios_load_linear(vpc_paths[i], 0x00100000 - size, size, offset);
    }
#endif

    if (bios_only || !ret)
	return ret;

    machine_at_common_init_ex(model, 2);
    is_vpc = 1;

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_slot(0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_slot(0x07, PCI_CARD_SOUTHBRIDGE, 0, 0, 0, 4);
    pci_register_slot(0x08, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x09, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0A, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0B, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0C, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0D, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0E, PCI_CARD_NORMAL, 1, 2, 3, 4);
    pci_register_slot(0x0F, PCI_CARD_NORMAL, 1, 2, 3, 4);
    device_add(&i440bx_device);
    device_add(&piix4e_device);
    device_add(&w83977f_370_device);
    device_add(&keyboard_ps2_ami_pci_device);
    device_add(&intel_flash_bxt_device);
    device_add(&vpc2007_device);
    spd_register(SPD_TYPE_SDRAM, 0xF, 256); /* real VPC provides invalid SPD data */

    return ret;
}
