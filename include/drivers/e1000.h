/* =============================================================================
 * Nyota OS — Intel 82540EM (E1000) Gigabit Ethernet Controller Driver
 * Register offsets, descriptor formats, and driver interface.
 * =========================================================================== */

#ifndef NYOTA_DRIVERS_E1000_H
#define NYOTA_DRIVERS_E1000_H

#include "types.h"
#include "drivers/pci.h"
#include "net/netdev.h"

/* PCI IDs */
#define E1000_VENDOR_INTEL      0x8086
#define E1000_DEV_82540EM       0x100E
#define E1000_DEV_82543GC       0x1004
#define E1000_DEV_82545EM       0x100F
#define E1000_DEV_82574L        0x10D3

/* Core Register Offsets */
#define E1000_REG_CTRL          0x0000
#define E1000_REG_STATUS        0x0008
#define E1000_REG_EECD          0x0010
#define E1000_REG_EERD          0x0014
#define E1000_REG_CTRL_EXT      0x0018
#define E1000_REG_ICR           0x00C0
#define E1000_REG_ICS           0x00C8
#define E1000_REG_IMS           0x00D0
#define E1000_REG_IMC           0x00D8
#define E1000_REG_RCTL          0x0100
#define E1000_REG_TCTL          0x0400
#define E1000_REG_TIPG          0x0410
#define E1000_REG_RDBAL         0x2800
#define E1000_REG_RDBAH         0x2804
#define E1000_REG_RDLEN         0x2808
#define E1000_REG_RDH           0x2810
#define E1000_REG_RDT           0x2818
#define E1000_REG_TDBAL         0x3800
#define E1000_REG_TDBAH         0x3804
#define E1000_REG_TDLEN         0x3808
#define E1000_REG_TDH           0x3810
#define E1000_REG_TDT           0x3818
#define E1000_REG_MTA           0x5200
#define E1000_REG_RAL0          0x5400
#define E1000_REG_RAH0          0x5404

/* Control Register Bits */
#define E1000_CTRL_SLU          (1 << 6)   /* Set Link Up */
#define E1000_CTRL_RST          (1 << 26)  /* Device Reset */

/* Receive Control Register Bits */
#define E1000_RCTL_EN           (1 << 1)   /* Receiver Enable */
#define E1000_RCTL_SBP          (1 << 2)   /* Store Bad Packets */
#define E1000_RCTL_UPE          (1 << 3)   /* Unicast Promiscuous */
#define E1000_RCTL_MPE          (1 << 4)   /* Multicast Promiscuous */
#define E1000_RCTL_BAM          (1 << 15)  /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE_2048   (0 << 16)  /* Buffer Size 2048 */
#define E1000_RCTL_SECRC        (1 << 26)  /* Strip Ethernet CRC */

/* Transmit Control Register Bits */
#define E1000_TCTL_EN           (1 << 1)   /* Transmit Enable */
#define E1000_TCTL_PSP          (1 << 3)   /* Pad Short Packets */

/* Interrupt Bits */
#define E1000_IMS_TXDW          (1 << 0)
#define E1000_IMS_TXQE          (1 << 1)
#define E1000_IMS_LSC           (1 << 2)
#define E1000_IMS_RXSEQ         (1 << 3)
#define E1000_IMS_RXDMT0        (1 << 4)
#define E1000_IMS_RXO           (1 << 6)
#define E1000_IMS_RXT0          (1 << 7)

/* Descriptors Configuration */
#define E1000_NUM_RX_DESC       32
#define E1000_NUM_TX_DESC       32
#define E1000_RX_BUF_SIZE       2048
#define E1000_TX_BUF_SIZE       2048

/* Receive Descriptor */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} e1000_rx_desc_t;

#define E1000_RXD_STAT_DD       (1 << 0)   /* Descriptor Done */
#define E1000_RXD_STAT_EOP      (1 << 1)   /* End of Packet */

/* Transmit Descriptor */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} e1000_tx_desc_t;

#define E1000_TXD_CMD_EOP       (1 << 0)   /* End of Packet */
#define E1000_TXD_CMD_IFCS      (1 << 1)   /* Insert FCS/CRC */
#define E1000_TXD_CMD_RS        (1 << 3)   /* Report Status */

#define E1000_TXD_STAT_DD       (1 << 0)   /* Descriptor Done */

/* E1000 Driver APIs */
int  e1000_init(void);
int  e1000_send(net_device_t *dev, const void *data, size_t len);
void e1000_poll_rx(void);
net_device_t *e1000_get_device(void);

#endif /* NYOTA_DRIVERS_E1000_H */
