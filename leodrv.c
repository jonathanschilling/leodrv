#ifndef LEODRV_C
#define LEODRV_C

/*******************************************************************************

  Hermstedt(tm) Leonardo(tm) ISDN PCI Linux driver
  Copyright(c) 2012 - 2022 Jonathan Schilling.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Jonathan Schilling <jonathan.schilling@mail.de>

*******************************************************************************/

/*
 * This is intended to become a driver for the famous Leonardo PCI
 * ISDN Cards made by Hermstedt GmbH fot the Macintosh (tm).
 * The first function to be implemented will be the reset/start of
 * the on-Board 68HC001.
 *
 * For testing purposes, the card will be used in a standard PC.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/string.h>

#define DRV_MODULE_NAME	 "leodrv"
#define DRV_EXT	         ""
#define DRV_VERSION      "1.0.0"
#define DRV_DESCRIPTION	 "Leonardo ISDN PCI driver"
#define DRV_COPYRIGHT    "Copyright(c) 2012 J. Schilling"
#define PFX              DRV_MODULE_NAME ": "

#define PCI_VENDOR_ID_HSTEDT 0x118e
#define LEO_XL               0x0042 // Leonardo XL
#define LEO_SL               0x00A2 // Leonardo SL (not sure!)
#define LEO_SP               0x00D2 // Leonardo SP (not sure!)

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

static DEFINE_PCI_DEVICE_TABLE(leo_id_table) = {
	{PCI_DEVICE(PCI_VENDOR_ID_HSTEDT, LEO_XL)},
	{PCI_DEVICE(PCI_VENDOR_ID_HSTEDT, LEO_SL)},
	{PCI_DEVICE(PCI_VENDOR_ID_HSTEDT, LEO_SP)},
	{}
};
MODULE_DEVICE_TABLE(pci, leo_id_table);

#define write_register(value, iomem, port) \
	iowrite32(cpu_to_le32(value), \
		(iomem) + (port))

#define read_register(iomem, port) \
		le32_to_cpu(ioread32((iomem) + (port)))


/* structs, enums for communication with card */

/* Control/Status Registers */
//struct csr {
//	u8 addr_space[1024*1024];
//	u32 start;
//	u32 size;
//};

/* Leonardo Card */
struct leo {
	struct pci_dev *pdev;

	void __iomem *card_space;
	u32 csr_begin;
	u32 csr_end;

	u32 leo_base_phys;
	u8 irq_line;
};

enum {
	LEO_RESET	= 0x400,
	LEO_HALT	= 0x400,
	LEO_RUN		= 0x400,
	LEO_SEL_MEM	= 0
};

enum {
	LEO_MAIN_CTRL	= 0x3c,
	LEO_AUX_CTRL	= 0x38,
	LEO_RAM_CTRL	= 52
};

/**
 *  reset the Leonardo card identified by leo
 */
static int leo_hw_reset(struct leo *leo) {

	u32 *port = leo->card_space + 0x80000 + LEO_MAIN_CTRL;
	//u8 i = 0;
	printk(KERN_INFO "leodrv: leo_hw_reset at %x\n", (u32)port);

	//u32 addr = (leo->csr_begin + LEO_MAIN_CTRL);

	// write (leo->leo_base_phys + LEO_MAIN_CTRL, LEO_RESET);
	iowrite32(LEO_RESET, port);
	//printk(KERN_INFO "first 256 bytes: \n");
	//for (i = 0; i < 256; i++) {
	//	printk(KERN_INFO "%x ", (u32)ioread32(port + i));
	//}
	//printk(KERN_INFO "\n");

	return 0;
}

/**
 *  start the Leonardo card identified by leo
 */
static int leo_start(struct leo *leo) {

	u32 *port = leo->card_space +  0x80000 + LEO_AUX_CTRL;
	//port = ((leo->card_space + 0x17710) + LEO_AUX_CTRL);

	printk(KERN_INFO "leodrv: leo_start at %x\n", (u32)port);

	iowrite32(LEO_RUN, port);

	return 0;
}

/**
 *  halt the 68HC001 on the leo card to disable sending irqs
 */
static void leo_halt(struct leo *leo) {

	u32 *port = leo->card_space + 0x80000 + LEO_MAIN_CTRL;

	printk(KERN_INFO "leodrv: leo_halt\n");

	iowrite32(LEO_RESET, port);


	return;
}

/**
 *  probe for device
 */
static int __devinit leo_probe(struct pci_dev *pdev, const struct pci_device_id *ent) {
	//char* cardname;

	struct leo *leo;

	int err = 0;

	printk(KERN_INFO "leodrv: leo_probe...\n");

	if (!(leo = kmalloc(sizeof(struct leo), GFP_KERNEL))) {
		printk(KERN_ERR "leodrv: cannont allocate mem for struct leo!\n");
		return -ENOMEM;
	}

	leo->pdev = pdev;
	pci_set_drvdata(pdev, leo);

	err = pci_enable_device(pdev);
	if (err < 0) {
		printk(KERN_ERR "leodrv: Cannot enable PCI device %s\n", \
				pci_name(pdev));
		goto err_out;
	}

	err = pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0, &leo->leo_base_phys);
	if (err < 0) {
		printk(KERN_ERR "leodrv: cannot read base addr of card\n");
		goto err_disable;
	}

	err = pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &leo->irq_line);
	if (err < 0) {
		printk(KERN_ERR "leodrv: cannot read irq line from card config space!\n");
		goto err_disable;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		printk(KERN_ERR "leodrv: Cannot find proper PCI device "
			"base address, aborting.\n");
		err = -ENODEV;
		goto err_disable;
	}

	printk (KERN_INFO "leodrv: leo at %x, irq %d\n", leo->leo_base_phys, leo->irq_line);

	err = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (err < 0) {
		printk(KERN_ERR "leodrv: request regins failed!\n");
		goto err_disable;
	}

	leo->csr_begin = pci_resource_start(pdev, 0);
	leo->csr_end   = pci_resource_end(pdev, 0);

	printk(KERN_INFO "leodrv: successfully enabled pci device"
					 " and requested region (%x to %x)\n", leo->csr_begin, leo->csr_end);

	leo->card_space = pci_iomap(pdev, 0, (leo->csr_end + 1 - leo->csr_begin));
	if (!leo->card_space) {
		printk(KERN_ERR "leodrv: cannot map device address space.\n");
		goto err_release_disable;
	}

	printk(KERN_INFO "leodrv: successfully mapped io space.\n");

	err = leo_hw_reset(leo);
	if (err < 0) {
		printk(KERN_INFO "leodrv: reset failed!\n");
		goto err_unmap;
	}

	err = leo_start(leo);
	if (err < 0) {
		printk(KERN_INFO "leodrv: start failed!\n");
		goto err_unmap;
	}

	return 0;

err_unmap:
	pci_iounmap(pdev, leo->card_space);

err_release_disable:
	pci_release_regions(pdev);

err_disable:
	pci_disable_device(pdev);

err_out:
	pci_set_drvdata(pdev, NULL);
	kfree(leo);
	return err;
}

/**
 *  remove after probing for device
 */
static void __devexit leo_remove(struct pci_dev *pdev) {

	//struct net_device *netdev = pci_get_drvdata(pdev);

	struct leo *leo = pci_get_drvdata(pdev);

	//halt_leo();

	if (leo) {
	//	struct nic *nic = netdev_priv(netdev);
//		unregister_netdev(netdev);
//		e100_free(nic);
		leo_halt(leo);
		pci_iounmap(pdev, leo->card_space);
//		pci_pool_destroy(nic->cbs_pool);
//		free_netdev(netdev);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
		kfree(leo);
	}
	printk(KERN_INFO "leodrv: successfully released regions and"
					 " disabled device!\n");

}

/**
 *  driver definition
 */
static struct pci_driver leo_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= leo_id_table,
	.probe		= leo_probe,
	.remove		= __devexit_p(leo_remove),
};


/**
 *  module initialization
 */
static int __init leo_init_module(void)
{
	printk(KERN_INFO "Loading leodrv...\n");
//	printk(KERN_INFO "Hello World! \n");

	return pci_register_driver(&leo_driver);
}

/**
 *  module cleanup
 */
static void __exit leo_cleanup_module(void)
{
	printk(KERN_INFO "Unloading leodrv...\n");

	pci_unregister_driver(&leo_driver);
}


module_init(leo_init_module);
module_exit(leo_cleanup_module);
#endif // LEODRV_C
