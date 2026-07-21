#include "network.h"
#include "e1000.h"
#include "process.h"
#include "runtime.h"

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP  0x0806
#define IP_PROTOCOL_UDP 17
#define IP_PROTOCOL_TCP 6

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define LOCAL_IP   IPV4(10,0,2,15)
#define GATEWAY_IP IPV4(10,0,2,2)
#define NETMASK    IPV4(255,255,255,0)

typedef struct __attribute__((packed)) {
    uint8_t destination[6];
    uint8_t source[6];
    uint16_t type;
} EthernetHeader;

typedef struct __attribute__((packed)) {
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint8_t hardware_length;
    uint8_t protocol_length;
    uint16_t operation;
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint8_t target_mac[6];
    uint32_t target_ip;
} ArpPacket;

typedef struct __attribute__((packed)) {
    uint8_t version_header_length;
    uint8_t service;
    uint16_t total_length;
    uint16_t identification;
    uint16_t fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t source;
    uint32_t destination;
} Ipv4Header;

typedef struct __attribute__((packed)) {
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t length;
    uint16_t checksum;
} UdpHeader;

typedef struct __attribute__((packed)) {
    uint16_t source_port;
    uint16_t destination_port;
    uint32_t sequence;
    uint32_t acknowledgement;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} TcpHeader;

typedef enum {
    TCP_CLOSED,
    TCP_SYN_SENT,
    TCP_ESTABLISHED
} TcpState;

static uint8_t local_mac[6];
static uint8_t gateway_mac[6];
static bool adapter_ready;
static bool gateway_ready;
static uint64_t sent_packets;
static uint64_t received_packets;
static uint64_t last_arp_tick;
static uint16_t ip_identifier;
static uint16_t udp_source_port;
static char status_text[48];
static TcpState tcp_state;
static uint32_t tcp_destination_ip;
static uint16_t tcp_destination_port;
static uint16_t tcp_local_port;
static uint32_t tcp_local_sequence;
static uint32_t tcp_remote_sequence;
static uint8_t tcp_receive_buffer[8192];
static uint16_t tcp_receive_size;

static uint16_t swap16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint32_t swap32(uint32_t value) {
    return ((value & 0x000000FFU) << 24) | ((value & 0x0000FF00U) << 8) |
           ((value & 0x00FF0000U) >> 8) | ((value & 0xFF000000U) >> 24);
}

static uint16_t internet_checksum(const void *data, size_t length) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;
    while (length > 1) {
        sum += ((uint16_t)bytes[0] << 8) | bytes[1];
        bytes += 2;
        length -= 2;
    }
    if (length) sum += (uint16_t)bytes[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t tcp_checksum(uint32_t source, uint32_t destination,
                             const void *segment, uint16_t length) {
    uint32_t sum = (source >> 16) + (source & 0xFFFF) +
                   (destination >> 16) + (destination & 0xFFFF) +
                   IP_PROTOCOL_TCP + length;
    const uint8_t *bytes = (const uint8_t *)segment;
    uint16_t remaining = length;
    while (remaining > 1) {
        sum += ((uint16_t)bytes[0] << 8) | bytes[1];
        bytes += 2;
        remaining -= 2;
    }
    if (remaining) sum += (uint16_t)bytes[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static bool transmit(const void *frame, uint16_t length) {
    if (!e1000_send(frame, length)) return false;
    sent_packets++;
    return true;
}

static void send_arp_request(void) {
    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));
    EthernetHeader *ethernet = (EthernetHeader *)frame;
    memset(ethernet->destination, 0xFF, 6);
    memcpy(ethernet->source, local_mac, 6);
    ethernet->type = swap16(ETHERTYPE_ARP);
    ArpPacket *arp = (ArpPacket *)(frame + sizeof(EthernetHeader));
    arp->hardware_type = swap16(1);
    arp->protocol_type = swap16(ETHERTYPE_IPV4);
    arp->hardware_length = 6;
    arp->protocol_length = 4;
    arp->operation = swap16(1);
    memcpy(arp->sender_mac, local_mac, 6);
    arp->sender_ip = swap32(LOCAL_IP);
    memset(arp->target_mac, 0, 6);
    arp->target_ip = swap32(GATEWAY_IP);
    transmit(frame, 60);
    last_arp_tick = scheduler_ticks;
}

static void send_arp_reply(const ArpPacket *request) {
    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));
    EthernetHeader *ethernet = (EthernetHeader *)frame;
    memcpy(ethernet->destination, request->sender_mac, 6);
    memcpy(ethernet->source, local_mac, 6);
    ethernet->type = swap16(ETHERTYPE_ARP);
    ArpPacket *arp = (ArpPacket *)(frame + sizeof(EthernetHeader));
    arp->hardware_type = swap16(1);
    arp->protocol_type = swap16(ETHERTYPE_IPV4);
    arp->hardware_length = 6;
    arp->protocol_length = 4;
    arp->operation = swap16(2);
    memcpy(arp->sender_mac, local_mac, 6);
    arp->sender_ip = swap32(LOCAL_IP);
    memcpy(arp->target_mac, request->sender_mac, 6);
    arp->target_ip = request->sender_ip;
    transmit(frame, 60);
}

static bool send_tcp_segment(uint8_t flags, const void *data, uint16_t size) {
    if (!gateway_ready || size > 1400) return false;
    uint8_t frame[1514];
    EthernetHeader *ethernet = (EthernetHeader *)frame;
    memcpy(ethernet->destination, gateway_mac, 6);
    memcpy(ethernet->source, local_mac, 6);
    ethernet->type = swap16(ETHERTYPE_IPV4);

    Ipv4Header *ip = (Ipv4Header *)(frame + sizeof(EthernetHeader));
    memset(ip, 0, sizeof(*ip));
    uint16_t tcp_length = (uint16_t)(sizeof(TcpHeader) + size);
    ip->version_header_length = 0x45;
    ip->total_length = swap16((uint16_t)(sizeof(Ipv4Header) + tcp_length));
    ip->identification = swap16(ip_identifier++);
    ip->fragment = swap16(0x4000);
    ip->ttl = 64;
    ip->protocol = IP_PROTOCOL_TCP;
    ip->source = swap32(LOCAL_IP);
    ip->destination = swap32(tcp_destination_ip);
    ip->checksum = swap16(internet_checksum(ip, sizeof(*ip)));

    TcpHeader *tcp = (TcpHeader *)((uint8_t *)ip + sizeof(*ip));
    memset(tcp, 0, sizeof(*tcp));
    tcp->source_port = swap16(tcp_local_port);
    tcp->destination_port = swap16(tcp_destination_port);
    tcp->sequence = swap32(tcp_local_sequence);
    tcp->acknowledgement = swap32(tcp_remote_sequence);
    tcp->data_offset = 5 << 4;
    tcp->flags = flags;
    tcp->window = swap16(8192);
    if (size) memcpy((uint8_t *)tcp + sizeof(*tcp), data, size);
    tcp->checksum = swap16(tcp_checksum(LOCAL_IP, tcp_destination_ip, tcp, tcp_length));
    if (!transmit(frame, (uint16_t)(sizeof(EthernetHeader) + sizeof(Ipv4Header) + tcp_length))) {
        return false;
    }
    if (flags & TCP_SYN) tcp_local_sequence++;
    if (flags & TCP_FIN) tcp_local_sequence++;
    tcp_local_sequence += size;
    return true;
}

static void handle_tcp(const Ipv4Header *ip, const uint8_t *packet, int length) {
    if (length < (int)sizeof(TcpHeader)) return;
    const TcpHeader *tcp = (const TcpHeader *)packet;
    int header_size = (tcp->data_offset >> 4) * 4;
    if (header_size < 20 || header_size > length || swap16(tcp->destination_port) != tcp_local_port ||
        swap16(tcp->source_port) != tcp_destination_port || swap32(ip->source) != tcp_destination_ip) return;
    uint16_t tcp_length = (uint16_t)length;
    if (tcp_checksum(tcp_destination_ip, LOCAL_IP, tcp, tcp_length) != 0) return;

    uint8_t flags = tcp->flags;
    uint32_t sequence = swap32(tcp->sequence);
    uint32_t acknowledgement = swap32(tcp->acknowledgement);
    if (flags & TCP_RST) {
        tcp_state = TCP_CLOSED;
        strcpy(status_text, "Nova bridge refused connection");
        return;
    }
    if (tcp_state == TCP_SYN_SENT && (flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) &&
        acknowledgement == tcp_local_sequence) {
        tcp_remote_sequence = sequence + 1;
        tcp_state = TCP_ESTABLISHED;
        send_tcp_segment(TCP_ACK, NULL, 0);
        strcpy(status_text, "Nova internet bridge connected");
        static const char hello[] = "NOVA/1 HELLO\n";
        network_tcp_send(hello, sizeof(hello) - 1);
        return;
    }
    if (tcp_state != TCP_ESTABLISHED) return;
    int payload_size = length - header_size;
    if (payload_size > 0 && sequence == tcp_remote_sequence) {
        int space = (int)sizeof(tcp_receive_buffer) - tcp_receive_size;
        int copy = payload_size < space ? payload_size : space;
        if (copy > 0) {
            memcpy(tcp_receive_buffer + tcp_receive_size, packet + header_size, (size_t)copy);
            tcp_receive_size += (uint16_t)copy;
        }
        tcp_remote_sequence += (uint32_t)payload_size;
        send_tcp_segment(TCP_ACK, NULL, 0);
        strcpy(status_text, "Internet bridge response received");
    }
    if (flags & TCP_FIN) {
        tcp_remote_sequence++;
        send_tcp_segment(TCP_ACK, NULL, 0);
        tcp_state = TCP_CLOSED;
    }
}

static void handle_arp(const uint8_t *frame, int length) {
    if (length < (int)(sizeof(EthernetHeader) + sizeof(ArpPacket))) return;
    const ArpPacket *arp = (const ArpPacket *)(frame + sizeof(EthernetHeader));
    if (arp->hardware_type != swap16(1) || arp->protocol_type != swap16(ETHERTYPE_IPV4) ||
        arp->hardware_length != 6 || arp->protocol_length != 4) return;
    uint16_t operation = swap16(arp->operation);
    uint32_t sender_ip = swap32(arp->sender_ip);
    uint32_t target_ip = swap32(arp->target_ip);
    if (operation == 2 && sender_ip == GATEWAY_IP && target_ip == LOCAL_IP) {
        memcpy(gateway_mac, arp->sender_mac, 6);
        gateway_ready = true;
        strcpy(status_text, "IPv4 ready: 10.0.2.15");
    } else if (operation == 1 && target_ip == LOCAL_IP) {
        send_arp_reply(arp);
    }
}

static void handle_ipv4(const uint8_t *frame, int length) {
    if (length < (int)(sizeof(EthernetHeader) + sizeof(Ipv4Header))) return;
    const Ipv4Header *ip = (const Ipv4Header *)(frame + sizeof(EthernetHeader));
    if ((ip->version_header_length >> 4) != 4 || swap32(ip->destination) != LOCAL_IP) return;
    int header_length = (ip->version_header_length & 0x0F) * 4;
    if (header_length < 20 || length < (int)sizeof(EthernetHeader) + header_length) return;
    if (internet_checksum(ip, (size_t)header_length) != 0) return;
    int total_length = swap16(ip->total_length);
    if (total_length < header_length || total_length > length - (int)sizeof(EthernetHeader)) return;
    const uint8_t *payload = (const uint8_t *)ip + header_length;
    int payload_length = total_length - header_length;
    if (ip->protocol == IP_PROTOCOL_UDP) strcpy(status_text, "IPv4/UDP packet received");
    else if (ip->protocol == IP_PROTOCOL_TCP) handle_tcp(ip, payload, payload_length);
}

bool network_init(void) {
    adapter_ready = e1000_init();
    gateway_ready = false;
    sent_packets = received_packets = 0;
    last_arp_tick = 0;
    ip_identifier = 1;
    udp_source_port = 49152;
    tcp_state = TCP_CLOSED;
    tcp_receive_size = 0;
    if (!adapter_ready) {
        strcpy(status_text, "e1000 adapter unavailable");
        return false;
    }
    memcpy(local_mac, e1000_mac(), 6);
    strcpy(status_text, "e1000 ready, resolving gateway");
    send_arp_request();
    return true;
}

void network_poll(void) {
    if (!adapter_ready) return;
    uint8_t frame[E1000_FRAME_MAX];
    for (int budget = 0; budget < 16; ++budget) {
        int length = e1000_receive(frame, sizeof(frame));
        if (length <= 0) break;
        received_packets++;
        if (length < (int)sizeof(EthernetHeader)) continue;
        const EthernetHeader *ethernet = (const EthernetHeader *)frame;
        uint16_t type = swap16(ethernet->type);
        if (type == ETHERTYPE_ARP) handle_arp(frame, length);
        else if (type == ETHERTYPE_IPV4) handle_ipv4(frame, length);
    }
    if (!gateway_ready && scheduler_ticks - last_arp_tick >= 100) send_arp_request();
}

bool network_send_udp(uint32_t destination_ip, uint16_t destination_port,
                      const void *data, uint16_t size) {
    if (!adapter_ready || !gateway_ready || !data || size > 1400) return false;
    uint8_t frame[1514];
    EthernetHeader *ethernet = (EthernetHeader *)frame;
    memcpy(ethernet->destination, gateway_mac, 6);
    memcpy(ethernet->source, local_mac, 6);
    ethernet->type = swap16(ETHERTYPE_IPV4);

    Ipv4Header *ip = (Ipv4Header *)(frame + sizeof(EthernetHeader));
    memset(ip, 0, sizeof(*ip));
    ip->version_header_length = 0x45;
    ip->total_length = swap16((uint16_t)(sizeof(Ipv4Header) + sizeof(UdpHeader) + size));
    ip->identification = swap16(ip_identifier++);
    ip->fragment = swap16(0x4000);
    ip->ttl = 64;
    ip->protocol = IP_PROTOCOL_UDP;
    ip->source = swap32(LOCAL_IP);
    ip->destination = swap32(destination_ip);
    ip->checksum = swap16(internet_checksum(ip, sizeof(*ip)));

    UdpHeader *udp = (UdpHeader *)((uint8_t *)ip + sizeof(*ip));
    udp->source_port = swap16(udp_source_port++);
    udp->destination_port = swap16(destination_port);
    udp->length = swap16((uint16_t)(sizeof(UdpHeader) + size));
    udp->checksum = 0;
    memcpy((uint8_t *)udp + sizeof(*udp), data, size);
    return transmit(frame, (uint16_t)(sizeof(EthernetHeader) + sizeof(Ipv4Header) +
                                      sizeof(UdpHeader) + size));
}

bool network_tcp_connect(uint32_t destination_ip, uint16_t destination_port) {
    if (!adapter_ready || !gateway_ready || tcp_state != TCP_CLOSED) return false;
    tcp_destination_ip = destination_ip;
    tcp_destination_port = destination_port;
    tcp_local_port = 50000;
    tcp_local_sequence = (uint32_t)scheduler_ticks ^ 0x4E4F5641U;
    tcp_remote_sequence = 0;
    tcp_receive_size = 0;
    tcp_state = TCP_SYN_SENT;
    strcpy(status_text, "Connecting to Nova internet bridge");
    if (!send_tcp_segment(TCP_SYN, NULL, 0)) {
        tcp_state = TCP_CLOSED;
        return false;
    }
    return true;
}

bool network_tcp_connected(void) {
    return tcp_state == TCP_ESTABLISHED;
}

bool network_tcp_send(const void *data, uint16_t size) {
    return tcp_state == TCP_ESTABLISHED && data && size &&
           send_tcp_segment(TCP_PSH | TCP_ACK, data, size);
}

int network_tcp_receive(void *buffer, uint16_t capacity) {
    if (!buffer || !capacity || !tcp_receive_size) return 0;
    uint16_t copied = tcp_receive_size < capacity ? tcp_receive_size : capacity;
    memcpy(buffer, tcp_receive_buffer, copied);
    memmove(tcp_receive_buffer, tcp_receive_buffer + copied, tcp_receive_size - copied);
    tcp_receive_size -= copied;
    return copied;
}

bool network_is_ready(void) {
    return adapter_ready;
}

bool network_gateway_resolved(void) {
    return gateway_ready;
}

uint64_t network_packets_sent(void) {
    return sent_packets;
}

uint64_t network_packets_received(void) {
    return received_packets;
}

const char *network_status(void) {
    return status_text;
}
