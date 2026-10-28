#include "ble_capture_end.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    bool valid, invalidate_during_send;
    int notify_rc, disconnect_rc;
    unsigned releases, errors, disconnects;
} link_t;

static bool valid(void *arg) { return ((link_t *)arg)->valid; }
static int notify(void *arg, bool submit)
{
    link_t *link = arg;
    if (submit) ++link->releases;
    else ++link->errors;
    if (link->invalidate_during_send) link->valid = false;
    return link->notify_rc;
}
static int disconnect(void *arg)
{
    link_t *link = arg;
    ++link->disconnects;
    return link->disconnect_rc;
}
static ble_capture_end_result_t run(link_t *link, bool submit)
{
    const ble_capture_end_ops_t ops = {link, valid, notify, disconnect};
    return ble_capture_end(&ops, submit);
}

int main(void)
{
    link_t link = {.valid = true};
    ble_capture_end_result_t r = run(&link, true);
    assert(link.releases == 1 && !link.errors && !link.disconnects && !r.cancelled);

    // Reproduce the field failure: audio ENOMEM must not submit truncated speech.
    link = (link_t){.valid = true};
    r = run(&link, false);
    assert(link.errors == 1 && !link.releases && !link.disconnects && !r.notify_rc);

    // A rejected terminal packet cannot fall through to a different end packet.
    for (unsigned submit = 0; submit < 2; ++submit) {
        link = (link_t){.valid = true, .notify_rc = 6};
        r = run(&link, submit);
        assert(link.releases == submit && link.errors == 1 - submit);
        assert(r.notify_rc == 6 && r.disconnect_requested && link.disconnects == 1);
    }

    // Reconnection during bounded retries must not tear down the replacement link.
    link = (link_t){.valid = true, .notify_rc = 7, .invalidate_during_send = true};
    r = run(&link, false);
    assert(r.cancelled && !r.disconnect_requested && !link.disconnects && !link.releases);
    link = (link_t){0};
    r = run(&link, true);
    assert(r.cancelled && !link.errors && !link.releases && !link.disconnects);

    link = (link_t){.valid = true, .notify_rc = 6, .disconnect_rc = 15};
    r = run(&link, false);
    assert(r.notify_rc == 6 && r.disconnect_rc == 15 && link.disconnects == 1);
    puts("BLE capture terminal delivery: PASS");
    return 0;
}
