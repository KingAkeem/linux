#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include "mt76.h"
#include "mt76_connac.h"
#include "mt7902.h"

MODULE_AUTHOR("Akeem T. L. King");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.0.1");
MODULE_DESCRIPTION("PCI Driver for MediaTek MT7902");

struct mt7902_priv {
    u32 __iomem *hwmem; /* Memory pointer for the I/O operations*/
};

void release_device(struct pci_dev *pdev)
{
    /* Disable IRQ */
    free_irq(pdev->irq, pdev);
    /* Free memory region */
    pci_release_regions(pdev);
    /* Disable DMA */
    pci_clear_master(pdev);
    /* And disable device */
    pci_disable_device(pdev);
}

static irqreturn_t irq_handler(int irq, void *dev) 
{
    return IRQ_HANDLED;
}

static struct pci_device_id mt7902_ids[] = {
    { PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, PCI_DEVICE_ID_MEDIATEK_7902) },
    { },
};

MODULE_DEVICE_TABLE(pci, mt7902_ids);

void read_sample_data(struct pci_dev *pdev) 
{
    struct mt7902_priv *drv_priv = (struct mt7902_priv *) pci_get_drvdata(pdev);
    if (!drv_priv) {
        return;
    }

    u32 data_to_write = 0xDEADBEEF;
    iowrite32(data_to_write, &drv_priv->hwmem);

    u32 data;
    data = ioread32(&drv_priv->hwmem);
    printk(KERN_INFO "Data retrieved 0x%X\n", data);
}


int mt7902_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    static const struct mt76_driver_ops mt7902_drv_ops = {
        .txwi_size = MT_TXD_SIZE + sizeof(struct mt76_connac_hw_txp),
        .drv_flags = MT_DRV_TXWI_NO_FREE | MT_DRV_HW_MGMT_TXQ | MT_DRV_AMSDU_OFFLOAD,
        .survey_flags = SURVEY_INFO_TIME_TX | SURVEY_INFO_TIME_RX | SURVEY_INFO_TIME_BSS_RX,
        /*
        .tx_prepare_skb = mt7902_tx_prepare_skb, // Replace with your own implementation
        .tx_complete_skb = mt76_connac_tx_complete_skb,
        .rx_check = mt7902_rx_check,  // Replace with your own implementation
        .rx_skb = mt7902_queue_rx_skb, // Replace with your own implementation
        .rx_poll_complete = mt7902_rx_poll_complete, // Replace with your own implementation
        */
    };

    /* Read vendor and device IDs */
    u16 vendor_id, device_id;
    pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor_id);
    pci_read_config_word(pdev, PCI_DEVICE_ID, &device_id) ;
    printk(KERN_INFO "Vendor id: %d Device id %d\n", vendor_id, device_id);

    /* Enable PCI device */
    int err;
    if ((err = pcim_enable_device(pdev))) {
        printk(KERN_ERR "Cannot enable PCI device, aborting\n");
        return err;
    }
    printk(KERN_INFO "MediaTek 7902 PCI device enabled\n");

    /**
     * Validate memory, there are 2 regions.
     * The first region is pre-fetchable and read/write.
     * The second region is not pre-fetchable and also read/write.
     */
    if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) | !(pci_resource_flags(pdev, 0) & IORESOURCE_PREFETCH) | (pci_resource_flags(pdev, 0) & IORESOURCE_READONLY)) {
        pci_disable_device(pdev);
        printk(KERN_WARNING "Cannot find proper PCI device base address, aborting\n");
        return -ENODEV;
    }

    if ((!(pci_resource_flags(pdev, 2) & IORESOURCE_MEM)) | (pci_resource_flags(pdev, 2) & IORESOURCE_PREFETCH) | (pci_resource_flags(pdev, 2) & IORESOURCE_READONLY)) {
        pci_disable_device(pdev);
        printk(KERN_WARNING "Cannot find proper PCI device base address, aborting\n");
        return -ENODEV;
    }

    printk(KERN_INFO "Found PCI device base addresses\n");

    /* Request MMIO resources */
    if ((err = pci_request_regions(pdev, DRV_NAME))) {
        pci_disable_device(pdev);
        printk(KERN_ERR "Cannot obtain PCI resources, aborting\n");
        return err;
    }
    printk(KERN_INFO "Requested PCI resources\n");

    /* Setting bus master bit in PCI_COMMAND register to enable DMA*/
    pci_set_master(pdev);

    if ((err = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32)))) {
        pci_disable_device(pdev);
        printk(KERN_ERR "Unable to set DMA mask, aborting\n");
        return err;
    }
    printk(KERN_INFO "DMA enabled\n");

    /* Allocate memory for the driver private data */
    struct mt7902_priv * drv_priv = kzalloc(sizeof(struct mt7902_priv), GFP_KERNEL);
    if (!drv_priv) {
        printk(KERN_ERR "Unable to allocate memory for driver private data, aborting\n");
        release_device(pdev);
        return -ENOMEM;
    }

    /* Get the start and stop positions */
    unsigned long mmio_len = pci_resource_len(pdev, 0);
    if (mmio_len != 0x100000) {
        printk(KERN_ALERT "Prefetchable MMIO region is not 1 MB (size: %lu bytes), aborting\n", mmio_len);
        release_device(pdev);
        return -ENODEV;
    }

    /* map provided resource to the local memory pointer */
    drv_priv->hwmem = pcim_iomap(pdev, 0, mmio_len);
    if (!drv_priv->hwmem) {
        printk(KERN_ERR "Unable to allocate memory for MMIO mapping, aborting\n");
        release_device(pdev);
        return -EIO;
    }

    pci_set_drvdata(pdev, drv_priv);

    read_sample_data(pdev);

    /* Registering interrupt handler */
    if ((err = request_irq(pdev->irq, irq_handler, IRQF_SHARED, "pci_irq_handler0", pdev))) {
        printk(KERN_ERR "Unable to start interrupt queue, aborting\n");
        return err;
    }
    printk(KERN_INFO "Interrupt handler assigned\n");

    return err;
}

static void mt7902_remove(struct pci_dev *pdev)
{
    struct mt7902_priv *drv_priv = pci_get_drvdata(pdev);
    if (drv_priv) {
        kfree(drv_priv);
    }

    release_device(pdev);
}

static struct pci_driver pci_mt7902_driver = {
    .name = DRV_NAME,
    .id_table = mt7902_ids,
    .probe = mt7902_probe,
    .remove = mt7902_remove,
};

static int __init pci_mt7902_init(void) 
{
    return pci_register_driver(&pci_mt7902_driver);
}

static void __exit pci_mt7902_exit(void)
{
    pci_unregister_driver(&pci_mt7902_driver);
}

module_init(pci_mt7902_init);
module_exit(pci_mt7902_exit);