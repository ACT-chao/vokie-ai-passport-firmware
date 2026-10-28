#pragma once
#include <stddef.h>
#include <stdint.h>
struct os_mbuf {
    uint8_t *om_data;
    uint16_t om_len;
    unsigned pool;
    uint8_t storage[320];
    uint8_t *end;
};
#define OS_MBUF_TRAILINGSPACE(om) ((size_t)((om)->end - (om)->om_data - (om)->om_len))
struct os_mbuf *os_msys_get_pkthdr(uint16_t size, uint16_t user_header);
int os_mbuf_append(struct os_mbuf *, const void *, uint16_t);
int os_mbuf_free_chain(struct os_mbuf *);
