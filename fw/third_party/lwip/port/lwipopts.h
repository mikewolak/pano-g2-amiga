/* lwipopts.h -- lwIP 2.2.0 configuration for the Pano G1 ethernet bring-up.
 *
 * NO_SYS=1 (bare metal, polled from the shell's main loop), raw API.  Ethernet,
 * ARP, IPv4, ICMP (ping), UDP, TCP, DHCP.  IPv6/PPP/SLIP/SNMP/sockets/netconn
 * are disabled.  Memory is generous -- the stack runs in LPDDR (32 MB), not in
 * the 16 KB boot RAM.
 *
 * Derived from Mike's PicoRV32 SLIP port (olimex-ice40hx8k-picorv32/firmware/lwIP/port/lwipopts.h),
 * flipped from SLIP point-to-point to a real ethernet netif with ARP and DHCP.
 */
#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/* ---- bare metal -------------------------------------------------------- */
#define NO_SYS                  1
#define LWIP_TIMERS             1
#define LWIP_NETCONN            0
#define LWIP_SOCKET             0

/* ---- memory ------------------------------------------------------------ */
#define MEM_ALIGNMENT           4
#if defined(PANO_DE2115)
/* THE DE2-115 HEAP IN SDRAM: the shell runs from 256 KB of block RAM, but 128 MB of SDRAM is ready before lwIP starts
 * (rtl/de2115/sdram32_ctrl.v initialises it after configuration; the shell's commands use the lower 64 MB).  The heap
 * is the top 1 MB, 0x17F0_0000, cached; MEM_SIZE holds TCP_SND_BUF of queued output and the stack's other pbufs. */
#define LWIP_RAM_HEAP_POINTER   ((void *)0x17F00000u)
#define MEM_SIZE                (256 * 1024)
#define LWIP_AUTOIP             0
#define LWIP_ACD                0
#define LWIP_DHCP_DOES_ACD_CHECK 0
#elif defined(PANO_G2) && !defined(PANO_G2_DDR)
/* The G2 stage 1 runs from 256 KB of block RAM (fw/link_g2.ld), code included: a quarter of the G1's heap and pool.
 * MEM_SIZE holds TCP_SND_BUF of queued console output plus DHCP, ARP and beacon pbufs. */
#define MEM_SIZE                (16 * 1024)
#else
#define MEM_SIZE                (64 * 1024)   /* heap for pbuf/mem allocations */
#endif

#if defined(PANO_DE2115)
#define MEMP_NUM_PBUF           160           /* PBUF_ROM segments of netsh_write_ref (fw/netsh.c), up to TCP_SND_BUF */
#else
#define MEMP_NUM_PBUF           32
#endif
#define MEMP_NUM_UDP_PCB        8
#define MEMP_NUM_TCP_PCB        8
#define MEMP_NUM_TCP_PCB_LISTEN 4
#if defined(PANO_DE2115)
#define MEMP_NUM_TCP_SEG        160           /* TCP_SND_QUEUELEN (128) and the segments of the receive side */
#elif defined(PANO_G2)
#define MEMP_NUM_TCP_SEG        32
#else
#define MEMP_NUM_TCP_SEG        64
#endif
#define MEMP_NUM_SYS_TIMEOUT    8

#if defined(PANO_DE2115)
#define PBUF_POOL_SIZE          2             /* received frames go into PBUF_RAM pbufs in the SDRAM heap through the
                                                 * window mover (fw/netif_pano.c); the pool is the fallback without it */
#define LWIP_DISABLE_TCP_SANITY_CHECKS 1    /* TCP_WND is held in heap pbufs, not in the pool the check counts */
#elif defined(PANO_G2)
#define PBUF_POOL_SIZE          4             /* received frames are PBUF_REF into the MAC ring; the pool is the fallback */
#else
#define PBUF_POOL_SIZE          16
#endif
#define PBUF_POOL_BUFSIZE       1536          /* one MTU */

/* ---- protocols --------------------------------------------------------- */
#define LWIP_ARP                1
#define LWIP_ETHERNET           1
#define LWIP_IPV4               1
#define LWIP_IPV6               0
#define LWIP_ICMP               1
#define LWIP_RAW                1
#define LWIP_UDP                1
#define LWIP_TCP                1

#define LWIP_DHCP               1
#define LWIP_DNS                0
#define LWIP_AUTOIP             0
#define LWIP_ACD                0          /* address conflict detection: off */
#define LWIP_DHCP_DOES_ACD_CHECK 0          /* DHCP must not reach into the ACD module */

/* ---- TCP --------------------------------------------------------------- */
#define TCP_MSS                 1460
/* ONE segment in flight toward the board (measured 2026-09-19, NETSH on B1): the MAC's RX ring is 2,560 bytes -- one
 * full frame plus ~1 KB -- and the CPU cannot drain it at wire speed (every LPDDR store costs ~31 cycles), so with
 * 2 x MSS the second back-to-back segment was always dropped and every window waited out a retransmit: 20 KB/s. */
/* THE RECEIVE-SIDE TCP CHECKSUM: CHECKSUM_CHECK_TCP is defined once, in the checksum block below, where it is 1 (the
 * value in force; a second definition of 0 here was overridden by it and has been removed).  Setting it to 0 would
 * skip one more read of every payload byte from LPDDR -- the largest part of the stack's ~81k cycles per KB -- and is
 * safe in the sense that every frame is covered by the Ethernet FCS the MAC checks in hardware (rtl/eth_mac_rx.v) and
 * bulk transfers (`recv') carry their own end-to-end CRC-32 that the host compares.  Transmitted segments carry a
 * correct checksum either way (CHECKSUM_GEN_* stay on). */
/* ZERO-COPY RECEIVE (fw/netif_pano.c): a received frame is handed to lwIP as a custom PBUF_REF pointing INTO the MAC's
 * RX ring (uncached fast RAM, single-cycle), freed back to the ring when lwIP lets go.  Out-of-order queueing would
 * hold a ring slot hostage, and with a one-segment window there is nothing to reorder anyway. */
#define LWIP_SUPPORT_CUSTOM_PBUF 1
#define TCP_QUEUE_OOSEQ          0
#if defined(PANO_DE2115)
/* the DE2-115's ENET0 MAC has the 16 KB ring (rtl/g2/g2_gmac.v, eleven full frames): eight segments in flight toward
 * the board; thirty-two queued toward the host (by reference, or in the SDRAM heap).  TCP checksums are generated and checked by the MAC
 * (fw/netif_pano.c), switched per netif */
#define TCP_WND                 (8 * TCP_MSS)
#define LWIP_CHECKSUM_CTRL_PER_NETIF 1
#define TCP_SND_BUF             (32 * TCP_MSS)
#else
#define TCP_WND                 (TCP_MSS)
#endif
#if defined(PANO_DE2115)
#elif defined(PANO_G2)
#define TCP_SND_BUF             (4 * TCP_MSS)
#else
#define TCP_SND_BUF             (8 * TCP_MSS)
#endif
#define TCP_SND_QUEUELEN        ((4 * TCP_SND_BUF + TCP_MSS - 1) / TCP_MSS)
#define TCP_LISTEN_BACKLOG      1

/* ---- checksums -- software, no offload ---------------------------------- */
#define CHECKSUM_GEN_IP         1
#define CHECKSUM_GEN_UDP        1
#define CHECKSUM_GEN_TCP        1
#define CHECKSUM_GEN_ICMP       1
#define CHECKSUM_CHECK_IP       1
#define CHECKSUM_CHECK_UDP      1
#define CHECKSUM_CHECK_TCP      1
#define CHECKSUM_CHECK_ICMP     1

/* ---- diagnostics -- quiet in the shipping build ------------------------- */
#if defined(PANO_DE2115)
/* the TCP, IP and heap counters only, for `net stat' (fw/bootsh.c) */
#define LWIP_STATS              1
#define LINK_STATS              0
#define ETHARP_STATS            0
#define IPFRAG_STATS            0
#define ICMP_STATS              0
#define UDP_STATS               0
#define MEMP_STATS              0
#define SYS_STATS               0
#define TCP_STATS               1
#define IP_STATS                1
#define MEM_STATS               1
#else
#define LWIP_STATS              0
#endif
#define LWIP_DEBUG              0

/* ---- the Amiga's sockets (fw/amiga/sock.c, AMIGA.FW) ---------------------- */
#if defined(AMIGA_SOCK)
/* 32 sockets of the Amiga beside the console's; the MAC's 16 KB receive ring (rtl/g2/g2_gmac.v RX_BIG) holds eight
 * full frames with room left for the X-Surf board's and the console's, so eight segments may be in flight toward the
 * unit; the Amiga's data waits in sock.c's rings, so the heap holds only what lwIP has queued for sending */
#undef  MEM_SIZE
#define MEM_SIZE                (1024 * 1024)
#undef  MEMP_NUM_PBUF
#define MEMP_NUM_PBUF           64
#undef  MEMP_NUM_UDP_PCB
#define MEMP_NUM_UDP_PCB        40
#undef  MEMP_NUM_TCP_PCB
#define MEMP_NUM_TCP_PCB        48
#undef  MEMP_NUM_TCP_PCB_LISTEN
#define MEMP_NUM_TCP_PCB_LISTEN 16
#undef  MEMP_NUM_TCP_SEG
#define MEMP_NUM_TCP_SEG        512
#undef  MEMP_NUM_SYS_TIMEOUT
#define MEMP_NUM_SYS_TIMEOUT    12
#undef  TCP_WND
#define TCP_WND                 (8 * TCP_MSS)
#undef  TCP_SND_BUF
#define TCP_SND_BUF             (16 * TCP_MSS)
#undef  LWIP_DNS
#define LWIP_DNS                1
#define DNS_TABLE_SIZE          8
#define DNS_MAX_NAME_LENGTH     128
#define SO_REUSE                1
#define LWIP_TCP_KEEPALIVE      1
#define LWIP_DISABLE_TCP_SANITY_CHECKS 1    /* TCP_WND is held in the MAC ring and sock.c's rings, not in the pool */
#endif

#endif /* LWIP_LWIPOPTS_H */
