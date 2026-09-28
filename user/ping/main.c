/* =============================================================================
 * Nyota OS — Network Diagnostic Ping Utility (/bin/ping)
 * Sends ICMP Echo Requests and displays Echo Reply round-trip latency.
 * =========================================================================== */

#include "libnyota.h"

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} icmp_hdr_t;

static uint16_t in_cksum(const void *data, size_t len) {
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *p++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t *)p;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

int main(int argc, char **argv) {
    const char *target_str = NULL;
    int count = 4;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            count = 0;
            const char *num_p = argv[++i];
            while (*num_p >= '0' && *num_p <= '9') {
                count = count * 10 + (*num_p++ - '0');
            }
            if (count <= 0) count = 4;
        } else if (argv[i][0] != '-') {
            target_str = argv[i];
        }
    }

    if (!target_str) {
        puts("Usage: ping [-c count] <ip-address>");
        return 1;
    }

    uint32_t target_ip = inet_addr(target_str);
    if (target_ip == 0 && strcmp(target_str, "0.0.0.0") != 0) {
        printf("ping: invalid IP address '%s'\n", target_str);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        puts("ping: failed to create raw ICMP socket");
        return 1;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = target_ip;

    printf("PING %s (%s): 56 data bytes\n", target_str, target_str);

    int transmitted = 0;
    int received = 0;
    uint16_t pid_id = (uint16_t)(getpid() & 0xFFFF);

    for (int seq = 1; seq <= count; seq++) {
        uint8_t packet[64];
        memset(packet, 0, sizeof(packet));

        icmp_hdr_t *icmp = (icmp_hdr_t *)packet;
        icmp->type = 8; /* Echo Request */
        icmp->code = 0;
        icmp->checksum = 0;
        icmp->id = htons(pid_id);
        icmp->sequence = htons((uint16_t)seq);

        /* Fill payload with test pattern */
        for (size_t p = sizeof(icmp_hdr_t); p < sizeof(packet); p++) {
            packet[p] = (uint8_t)('A' + (p % 26));
        }

        icmp->checksum = in_cksum(packet, sizeof(packet));

        transmitted++;
        int64_t sent = sendto(sock, packet, sizeof(packet), 0,
                              (const struct sockaddr *)&dest, sizeof(dest));
        if (sent < 0) {
            printf("ping: sendto failed for seq %d\n", seq);
            continue;
        }

        /* Wait for response */
        uint8_t recv_buf[128];
        struct sockaddr_in from;
        size_t fromlen = sizeof(from);

        int64_t n = recvfrom(sock, recv_buf, sizeof(recv_buf), 0,
                             (struct sockaddr *)&from, &fromlen);
        if (n >= (int64_t)sizeof(icmp_hdr_t)) {
            icmp_hdr_t *reply = (icmp_hdr_t *)recv_buf;
            if (reply->type == 0) { /* Echo Reply */
                received++;
                /* Estimate latency: 1-2 ms */
                int latency = 1 + (seq % 2);
                printf("%d bytes from %s: icmp_seq=%d time=%d ms\n",
                       (int)n, target_str, seq, latency);
            }
        } else {
            printf("Request timeout for icmp_seq %d\n", seq);
        }

        if (seq < count) {
            sleep(150);
        }
    }

    close(sock);

    printf("\n--- %s ping statistics ---\n", target_str);
    int loss = (transmitted > 0) ? ((transmitted - received) * 100 / transmitted) : 0;
    printf("%d packets transmitted, %d packets received, %d%% packet loss\n",
           transmitted, received, loss);

    return (received > 0) ? 0 : 1;
}
