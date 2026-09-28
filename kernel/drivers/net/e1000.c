/* =============================================================================
 * Nyota OS — Intel 82540EM (E1000) Gigabit Ethernet Controller Driver
 * Descriptor ring allocation, hardware initialization, packet TX/RX, and IRQ handler.
 * =========================================================================== */

#include "drivers/e1000.h"
#include "drivers/pci.h"
#include "paging.h"
#include "pmm.h"
#include "heap.h"
#include "memory.h"
#include "interrupts.h"
#include "pic.h"
#include "timer.h"
#include "serial.h"
#include "vga.h"

static volatile uint8_t *e1000_mmio = NULL;
static pci_device_t     *e1000_pci  = NULL;
static net_device_t      e1000_netdev;

static e1000_rx_desc_t  *rx_descs   = NULL;
static uint8_t          *rx_buffers[E1000_NUM_RX_DESC];
static uint32_t          rx_cur     = 0;

static e1000_tx_desc_t  *tx_descs   = NULL;
static uint8_t          *tx_buffers[E1000_NUM_TX_DESC];
static uint32_t          tx_tail    = 0;

/* ── MMIO Register Access Helpers ─────────────────────────────────────────── */

static inline void e1000_write32(uint16_t reg, uint32_t val) {
    *(volatile uint32_t *)(e1000_mmio + reg) = val;
}

static inline uint32_t e1000_read32(uint16_t reg) {
    return *(volatile uint32_t *)(e1000_mmio + reg);
}

/* ── EEPROM Access ────────────────────────────────────────────────────────── */

static uint16_t e1000_read_eeprom(uint8_t addr) {
    uint32_t val = (1U) | ((uint32_t)addr << 8);
    e1000_write32(E1000_REG_EERD, val);

    for (int i = 0; i < 10000; i++) {
        val = e1000_read32(E1000_REG_EERD);
        if (val & (1 << 4)) {
            return (uint16_t)((val >> 16) & 0xFFFF);
        }
    }
    return 0;
}

static void e1000_read_mac(mac_addr_t mac) {
    /* 1. Try reading from Receive Address Low/High 0 (populated by firmware/QEMU) */
    uint32_t ral = e1000_read32(E1000_REG_RAL0);
    uint32_t rah = e1000_read32(E1000_REG_RAH0);

    if (rah & (1U << 31)) {
        mac[0] = (uint8_t)(ral & 0xFF);
        mac[1] = (uint8_t)((ral >> 8) & 0xFF);
        mac[2] = (uint8_t)((ral >> 16) & 0xFF);
        mac[3] = (uint8_t)((ral >> 24) & 0xFF);
        mac[4] = (uint8_t)(rah & 0xFF);
        mac[5] = (uint8_t)((rah >> 8) & 0xFF);
        return;
    }

    /* 2. Fallback to reading EEPROM registers */
    uint16_t w0 = e1000_read_eeprom(0);
    uint16_t w1 = e1000_read_eeprom(1);
    uint16_t w2 = e1000_read_eeprom(2);

    mac[0] = (uint8_t)(w0 & 0xFF);
    mac[1] = (uint8_t)((w0 >> 8) & 0xFF);
    mac[2] = (uint8_t)(w1 & 0xFF);
    mac[3] = (uint8_t)((w1 >> 8) & 0xFF);
    mac[4] = (uint8_t)(w2 & 0xFF);
    mac[5] = (uint8_t)((w2 >> 8) & 0xFF);

    /* 3. If MAC is still empty, supply standard QEMU default MAC */
    if (mac[0] == 0 && mac[1] == 0 && mac[2] == 0 &&
        mac[3] == 0 && mac[4] == 0 && mac[5] == 0) {
        mac[0] = 0x52; mac[1] = 0x54; mac[2] = 0x00;
        mac[3] = 0x12; mac[4] = 0x34; mac[5] = 0x56;
    }
}

/* ── Packet Transmission ─────────────────────────────────────────────────── */

int e1000_send(net_device_t *dev, const void *data, size_t len) {
    (void)dev;
    if (!e1000_mmio || !data || len == 0 || len > E1000_TX_BUF_SIZE) {
        return -1;
    }

    e1000_tx_desc_t *desc = &tx_descs[tx_tail];

    /* Wait if previous descriptor is still pending transmission */
    int timeout = 100000;
    while (!(desc->status & E1000_TXD_STAT_DD) && timeout > 0) {
        timeout--;
    }

    /* Copy payload into transmit buffer */
    memcpy(tx_buffers[tx_tail], data, len);

    desc->length = (uint16_t)len;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0;

    /* Advance tail pointer and notify hardware */
    uint32_t prev_tail = tx_tail;
    tx_tail = (tx_tail + 1) % E1000_NUM_TX_DESC;
    e1000_write32(E1000_REG_TDT, tx_tail);

    /* Spin wait briefly for descriptor completion */
    timeout = 100000;
    while (!(tx_descs[prev_tail].status & E1000_TXD_STAT_DD) && timeout > 0) {
        timeout--;
    }

    return 0;
}

/* ── Packet Reception & Polling ──────────────────────────────────────────── */

void e1000_poll_rx(void) {
    if (!e1000_mmio || !rx_descs) return;

    while (rx_descs[rx_cur].status & E1000_RXD_STAT_DD) {
        uint16_t len = rx_descs[rx_cur].length;
        uint8_t *buf = rx_buffers[rx_cur];

        /* Deliver frame to network device stack */
        netdev_receive(&e1000_netdev, buf, len);

        /* Reset descriptor */
        rx_descs[rx_cur].status = 0;
        uint32_t old_cur = rx_cur;
        rx_cur = (rx_cur + 1) % E1000_NUM_RX_DESC;

        /* Advance hardware receive tail to free descriptor */
        e1000_write32(E1000_REG_RDT, old_cur);
    }
}

/* ── Interrupt Service Routine ────────────────────────────────────────────── */

static void e1000_irq_handler(interrupt_frame_t *frame) {
    (void)frame;
    if (!e1000_mmio) return;

    /* Reading ICR acknowledges and clears interrupts in E1000 */
    uint32_t icr = e1000_read32(E1000_REG_ICR);

    if (icr & (E1000_IMS_RXT0 | E1000_IMS_RXO | E1000_IMS_RXDMT0)) {
        e1000_poll_rx();
    }
}

/* ── Driver Initialization ───────────────────────────────────────────────── */

int e1000_init(void) {
    /* 1. Discover E1000 on PCI bus */
    e1000_pci = pci_find_device(E1000_VENDOR_INTEL, E1000_DEV_82540EM);
    if (!e1000_pci) {
        e1000_pci = pci_find_device(E1000_VENDOR_INTEL, E1000_DEV_82545EM);
    }
    if (!e1000_pci) {
        e1000_pci = pci_find_device(E1000_VENDOR_INTEL, E1000_DEV_82543GC);
    }
    if (!e1000_pci) {
        e1000_pci = pci_find_device(E1000_VENDOR_INTEL, E1000_DEV_82574L);
    }
    if (!e1000_pci) {
        e1000_pci = pci_find_class(PCI_CLASS_NETWORK, PCI_SUBCLASS_ETHERNET);
    }

    if (!e1000_pci) {
        serial_write("[E1000] No compatible Intel network controller found\n");
        return -1;
    }

    /* 2. Enable PCI Bus Mastering and memory access */
    pci_enable_bus_master(e1000_pci);

    /* 3. Map BAR0 MMIO space (128 KB = 32 pages) */
    uint64_t bar0_phys = e1000_pci->bar[0] & ~0xFULL;
    if (!bar0_phys) {
        serial_write("[E1000] Invalid BAR0 address\n");
        return -1;
    }

    for (uint64_t p = 0; p < 32; p++) {
        uint64_t pa = bar0_phys + p * 4096;
        paging_map_page(pa, pa, PAGE_PRESENT | PAGE_WRITABLE);
    }

    e1000_mmio = (volatile uint8_t *)bar0_phys;

    /* 4. Reset controller */
    e1000_write32(E1000_REG_CTRL, e1000_read32(E1000_REG_CTRL) | E1000_CTRL_RST);
    for (volatile int d = 0; d < 50000; d++);

    /* 5. Clear Multicast Table Array */
    for (int i = 0; i < 128; i++) {
        e1000_write32((uint16_t)(E1000_REG_MTA + i * 4), 0);
    }

    /* 6. Read MAC Address */
    mac_addr_t mac;
    e1000_read_mac(mac);
    memcpy(e1000_netdev.mac, mac, ETH_ALEN);

    /* Set Link Up */
    e1000_write32(E1000_REG_CTRL, e1000_read32(E1000_REG_CTRL) | E1000_CTRL_SLU);

    /* 7. Setup RX Descriptor Ring */
    rx_descs = (e1000_rx_desc_t *)pmm_alloc_page();
    memset(rx_descs, 0, 4096);

    for (int i = 0; i < E1000_NUM_RX_DESC; i += 2) {
        /* Allocate 4096-byte physical frame for two 2048-byte RX buffers */
        void *page = pmm_alloc_page();
        rx_buffers[i] = (uint8_t *)page;
        rx_descs[i].addr = (uint64_t)rx_buffers[i];
        rx_descs[i].status = 0;

        rx_buffers[i + 1] = (uint8_t *)page + 2048;
        rx_descs[i + 1].addr = (uint64_t)rx_buffers[i + 1];
        rx_descs[i + 1].status = 0;
    }

    e1000_write32(E1000_REG_RDBAL, (uint32_t)((uint64_t)rx_descs & 0xFFFFFFFF));
    e1000_write32(E1000_REG_RDBAH, (uint32_t)(((uint64_t)rx_descs >> 32) & 0xFFFFFFFF));
    e1000_write32(E1000_REG_RDLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    e1000_write32(E1000_REG_RDH, 0);
    e1000_write32(E1000_REG_RDT, E1000_NUM_RX_DESC - 1);
    rx_cur = 0;

    /* Enable receiver */
    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE |
                    E1000_RCTL_MPE | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2048 |
                    E1000_RCTL_SECRC;
    e1000_write32(E1000_REG_RCTL, rctl);

    /* 8. Setup TX Descriptor Ring */
    tx_descs = (e1000_tx_desc_t *)pmm_alloc_page();
    memset(tx_descs, 0, 4096);

    for (int i = 0; i < E1000_NUM_TX_DESC; i += 2) {
        /* Allocate 4096-byte physical frame for two 2048-byte TX buffers */
        void *page = pmm_alloc_page();
        tx_buffers[i] = (uint8_t *)page;
        tx_descs[i].addr = (uint64_t)tx_buffers[i];
        tx_descs[i].cmd = 0;
        tx_descs[i].status = E1000_TXD_STAT_DD;

        tx_buffers[i + 1] = (uint8_t *)page + 2048;
        tx_descs[i + 1].addr = (uint64_t)tx_buffers[i + 1];
        tx_descs[i + 1].cmd = 0;
        tx_descs[i + 1].status = E1000_TXD_STAT_DD;
    }

    e1000_write32(E1000_REG_TDBAL, (uint32_t)((uint64_t)tx_descs & 0xFFFFFFFF));
    e1000_write32(E1000_REG_TDBAH, (uint32_t)(((uint64_t)tx_descs >> 32) & 0xFFFFFFFF));
    e1000_write32(E1000_REG_TDLEN, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t));
    e1000_write32(E1000_REG_TDH, 0);
    e1000_write32(E1000_REG_TDT, 0);
    tx_tail = 0;

    /* Configure Inter-Packet Gap */
    e1000_write32(E1000_REG_TIPG, 10 | (8 << 10) | (6 << 20));

    /* Enable transmitter */
    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (15 << 4) | (64 << 12);
    e1000_write32(E1000_REG_TCTL, tctl);

    /* 9. Configure IRQ Handling */
    if (e1000_pci->irq_line > 0 && e1000_pci->irq_line < 16) {
        uint8_t irq = e1000_pci->irq_line;
        interrupt_register_handler(IRQ_BASE_VECTOR + irq, e1000_irq_handler);
        pic_unmask_irq(irq);

        /* Enable RX and link status change interrupts */
        e1000_write32(E1000_REG_IMS, E1000_IMS_RXT0 | E1000_IMS_RXO | E1000_IMS_LSC);
        /* Read ICR to clear initial pending status */
        e1000_read32(E1000_REG_ICR);
    }

    /* 10. Register eth0 interface */
    memset(&e1000_netdev, 0, sizeof(net_device_t));
    memcpy(e1000_netdev.name, "eth0", 5);
    memcpy(e1000_netdev.mac, mac, ETH_ALEN);
    e1000_netdev.ip_addr.addr = ip_parse("10.0.2.15");
    e1000_netdev.netmask.addr = ip_parse("255.255.255.0");
    e1000_netdev.gateway.addr = ip_parse("10.0.2.2");
    e1000_netdev.dns.addr     = ip_parse("10.0.2.3");
    e1000_netdev.mtu          = ETH_MTU;
    e1000_netdev.send         = e1000_send;
    e1000_netdev.is_up        = true;

    netdev_register(&e1000_netdev);

    /* Print Diagnostics */
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("[ OK ] ");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_println("E1000");

    char mac_str[20];
    mac_format(mac, mac_str, sizeof(mac_str));
    serial_write("[E1000] Initialized eth0 MAC: ");
    serial_write(mac_str);
    serial_write(" IRQ: ");
    serial_write_dec(e1000_pci->irq_line);
    serial_write("\n");

    return 0;
}

net_device_t *e1000_get_device(void) {
    return &e1000_netdev;
}
