#define PCI_DEVICE_ID_MEDIATEK_7902 0x7902 /* PCI Device ID for MediaTek 7902 */
#define DRV_NAME "pci_mt7902" /* PCI Driver name for MediaTek 7902*/

int mt7902_probe(struct pci_dev *dev, const struct pci_device_id *id);
void release_device(struct pci_dev *pdev);
void read_sample_data(struct pci_dev *pdev);