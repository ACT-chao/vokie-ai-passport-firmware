#include "ble_tx_retry.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

enum { TRY_AGAIN = 1, NO_MEMORY = 6, NOT_CONNECTED = 7, TOO_LARGE = 4, BUSY = 15 };
typedef struct {
    uint32_t now;
    uint32_t elapsed;
    uint32_t ready_after;
    uint32_t invalidate_after;
    unsigned sends;
    unsigned accepted;
    int failure;
    bool alive;
} fake_link_t;

static bool valid(void *arg) { return ((fake_link_t *)arg)->alive; }
static uint32_t now_ms(void *arg) { return ((fake_link_t *)arg)->now; }
static bool retryable(int code) { return code == NO_MEMORY || code == TRY_AGAIN || code == BUSY; }
static void wait_ms(void *arg, uint32_t delay)
{
    fake_link_t *link = arg;
    link->now += delay;
    link->elapsed += delay;
    if (link->invalidate_after && link->elapsed >= link->invalidate_after) link->alive = false;
}
static int send_packet(void *arg)
{
    fake_link_t *link = arg;
    ++link->sends;
    if (link->elapsed < link->ready_after) return link->failure;
    ++link->accepted;
    return 0;
}
static ble_tx_retry_result_t run(fake_link_t *link, uint32_t timeout, uint32_t step)
{
    const ble_tx_retry_ops_t ops = {link, valid, send_packet, retryable, now_ms, wait_ms};
    return ble_tx_retry_send(&ops, timeout, step);
}

int main(void)
{
    // One enqueue per successful packet; retrying accepted data would duplicate audio.
    fake_link_t link = {.alive = true};
    ble_tx_retry_result_t result = run(&link, 80, 5);
    assert(result.code == 0 && result.attempts == 1 && link.accepted == 1);

    // A Windows-style 60 ms scheduling gap must not abort recording on ENOMEM.
    link = (fake_link_t){.alive = true, .ready_after = 60, .failure = NO_MEMORY};
    result = run(&link, 80, 5);
    assert(result.code == 0 && result.elapsed_ms == 60 && link.accepted == 1);

    link = (fake_link_t){.alive = true, .ready_after = 15, .failure = BUSY};
    result = run(&link, 80, 5);
    assert(result.code == 0 && result.attempts == 4 && link.accepted == 1);

    link = (fake_link_t){.alive = true, .ready_after = 10, .failure = TRY_AGAIN};
    result = run(&link, 80, 5);
    assert(result.code == 0 && result.attempts == 3 && link.accepted == 1);

    // Persistent congestion remains bounded; no false success or skipped packet.
    link = (fake_link_t){.alive = true, .ready_after = 1000, .failure = NO_MEMORY};
    result = run(&link, 80, 5);
    assert(result.code == NO_MEMORY && result.elapsed_ms == 80 && link.accepted == 0);
    assert(link.sends == 16);

    // Disconnect, unsubscribe, or a new session cancels before the next enqueue.
    link = (fake_link_t){.alive = true, .ready_after = 60, .invalidate_after = 10, .failure = NO_MEMORY};
    result = run(&link, 80, 5);
    assert(result.cancelled && result.attempts == 2 && link.accepted == 0);
    link = (fake_link_t){.alive = false};
    result = run(&link, 80, 5);
    assert(result.cancelled && result.attempts == 0);

    // Permanent failures cannot be repaired by sleeping/replaying the packet.
    link = (fake_link_t){.alive = true, .ready_after = 60, .failure = NOT_CONNECTED};
    result = run(&link, 80, 5);
    assert(result.code == NOT_CONNECTED && result.attempts == 1 && result.elapsed_ms == 0);
    link.failure = TOO_LARGE;
    result = run(&link, 80, 5);
    assert(result.code == TOO_LARGE && result.attempts == 1);

    // Unsigned timer wrap and a final partial delay must not extend the deadline.
    link = (fake_link_t){.alive = true, .now = UINT_MAX - 3, .ready_after = 10, .failure = BUSY};
    result = run(&link, 11, 5);
    assert(result.code == 0 && result.elapsed_ms == 10 && link.accepted == 1);
    link = (fake_link_t){.alive = true, .ready_after = 1000, .failure = BUSY};
    result = run(&link, 12, 5);
    assert(result.code == BUSY && result.elapsed_ms == 12 && result.attempts == 3);
    puts("BLE bounded notification retry: PASS");
    return 0;
}
