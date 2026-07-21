#ifndef NOVA_NETWORK_H
#define NOVA_NETWORK_H

#include "types.h"

bool network_init(void);
void network_poll(void);
bool network_is_ready(void);
bool network_gateway_resolved(void);
bool network_send_udp(uint32_t destination_ip, uint16_t destination_port,
                      const void *data, uint16_t size);
bool network_tcp_connect(uint32_t destination_ip, uint16_t destination_port);
bool network_tcp_connected(void);
bool network_tcp_send(const void *data, uint16_t size);
int network_tcp_receive(void *buffer, uint16_t capacity);
uint64_t network_packets_sent(void);
uint64_t network_packets_received(void);
const char *network_status(void);

#define IPV4(a,b,c,d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | \
                       ((uint32_t)(c) << 8) | (uint32_t)(d))

#endif
