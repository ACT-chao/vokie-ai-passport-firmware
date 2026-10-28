#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Persisted V1 event IDs: append new IDs; never renumber existing entries.
typedef enum {
    DD_BOOT = 1, DD_BUILD, DD_HEARTBEAT, DD_DROPPED, DD_STORAGE_SLOW,
    DD_CONNECT = 10, DD_DISCONNECT, DD_MTU, DD_SUBSCRIBE, DD_LINK,
    DD_LINK_REQUEST, DD_DATA_LENGTH, DD_NIMBLE_RESET,
    DD_HOST_READY = 20, DD_HOST_STATE, DD_HOST_REJECT,
    DD_BUTTON = 30, DD_START_REQUEST, DD_STOP_REQUEST, DD_BUTTON_SEND,
    DD_CAPTURE_START = 40, DD_FIRST_FRAME, DD_AUDIO_STATS, DD_TX_RETRY,
    DD_TX_FAILURE, DD_CAPTURE_STOP, DD_CAPTURE_ABORT, DD_MIC_ERROR,
    DD_TX_RESOURCES, DD_START_BLOCKED, DD_END_DELIVERY,
    DD_MSYS_POOLS,
} device_diag_event_t;

typedef enum {
    DD_REASON_NONE, DD_REASON_UP, DD_REASON_OK_CLICK, DD_REASON_OK_LONG,
    DD_REASON_HOST_ERROR, DD_REASON_DISCONNECT, DD_REASON_TRANSPORT,
    DD_REASON_AUDIO_PAUSE, DD_REASON_AUDIO_OPEN, DD_REASON_AUDIO_READ,
    DD_REASON_MTU, DD_REASON_START_TX, DD_REASON_SESSION_CHANGED,
} device_diag_reason_t;

esp_err_t device_diag_init(void);
// Enqueue fixed numeric metadata, never PCM, transcripts or arbitrary JSON.
// Task contexts only; bounded RAM, zero queue wait, drops counted explicitly.
void device_diag_emit(device_diag_event_t event, uint32_t session,
                      uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                      uint32_t e, uint32_t f, uint32_t g, uint32_t h);
// Audio worker only. One running append may finish before enter returns; once
// entered no journal read, write or erase starts until idle after I2S pause.
void device_diag_audio_enter(void);
void device_diag_audio_idle(void);
