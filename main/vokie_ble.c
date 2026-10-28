#include "vokie_ble.h"

#include "bsp_audio.h"
#include "boot_sound.h"
#include "ui_status.h"
#include "bsp_button.h"
#include "button_gesture.h"
#include "ble_tx_retry.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "vokie_ble";
static const char *DEVICE_NAME = "Vokie Passport";
extern const uint8_t boot_pcm_start[] asm("_binary_vokie_boot_16k_pcm_start");
extern const uint8_t boot_pcm_end[] asm("_binary_vokie_boot_16k_pcm_end");
static const uint16_t NO_CONN = BLE_HS_CONN_HANDLE_NONE;

#define AUDIO_SAMPLES 320
#define ADPCM_BYTES 166
#define MAX_CONTROL 182
#define EMPTY_FINAL_SEQUENCE 0xffffffffUL
#define AUDIO_TX_TIMEOUT_MS 80
#define CONTROL_TX_TIMEOUT_MS 200
#define TX_RETRY_INTERVAL_MS 5
#define SERVICE_BYTES 0x00,0x45,0x49,0x4b,0x4f,0x56,0x1a,0x9d,0x6f,0x4b,0x7b,0x6a,0x01,0x00,0x0e,0x7f
#define CONTROL_BYTES 0x00,0x45,0x49,0x4b,0x4f,0x56,0x1a,0x9d,0x6f,0x4b,0x7b,0x6a,0x02,0x00,0x0e,0x7f
#define AUDIO_BYTES 0x00,0x45,0x49,0x4b,0x4f,0x56,0x1a,0x9d,0x6f,0x4b,0x7b,0x6a,0x03,0x00,0x0e,0x7f
#define INFO_BYTES 0x00,0x45,0x49,0x4b,0x4f,0x56,0x1a,0x9d,0x6f,0x4b,0x7b,0x6a,0x04,0x00,0x0e,0x7f

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(SERVICE_BYTES);
static const ble_uuid128_t s_control_uuid = BLE_UUID128_INIT(CONTROL_BYTES);
static const ble_uuid128_t s_audio_uuid = BLE_UUID128_INIT(AUDIO_BYTES);
static const ble_uuid128_t s_info_uuid = BLE_UUID128_INIT(INFO_BYTES);
static uint16_t s_control_handle;
static uint16_t s_audio_handle;
static uint16_t s_info_handle;
static uint16_t s_conn = NO_CONN;
static uint8_t s_addr_type;
static volatile bool s_control_subscribed;
static volatile bool s_audio_subscribed;
static volatile bool s_host_ready;
static volatile bool s_recording;
static volatile bool s_stop_requested;
static volatile uint32_t s_session;
static volatile uint32_t s_control_seq;
static volatile uint32_t s_button_seq;
static TaskHandle_t s_audio_task;
static bool s_started;
static button_gesture_t s_button_gestures[3];
static esp_timer_handle_t s_ok_hold_timer;
static volatile uint32_t s_connection_epoch = 1;
static bool s_hello_sent;
static bool s_link_parameters_requested;
static uint32_t s_tx_retries;
#define BUTTON_LONG_PRESS_MS CONFIG_BUTTON_LONG_PRESS_TIME_MS

static bool host_is_ready(void)
{
    return s_conn != NO_CONN && s_host_ready;
}

static int notify_control(const char *json);

static const char *button_name(bsp_btn_t button)
{
    switch (button) {
    case BSP_BTN_DOWN: return "down";
    case BSP_BTN_OK: return "ok";
    default: return "up";
    }
}

static void send_button_event(bsp_btn_t button, const char *event,
                              uint32_t duration_ms)
{
    if (s_conn == NO_CONN || !s_host_ready) return;
    char json[MAX_CONTROL + 1];
    snprintf(json, sizeof(json),
             "{\"v\":1,\"type\":\"button_event\",\"button\":\"%s\",\"event\":\"%s\",\"durationMs\":%lu,\"seq\":%lu}",
             button_name(button), event, (unsigned long)duration_ms,
             (unsigned long)++s_button_seq);
    (void)notify_control(json);
}

static void button_cb(bsp_btn_t button, bsp_btn_ev_t event, void *user);

static void ok_hold_expired(void *arg)
{
    (void)arg;
    // Both this timer and the button driver's callbacks run on ESP_TIMER_TASK.
    button_cb(BSP_BTN_OK, BSP_BTN_LONG, NULL);
}

static int access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gap_event(struct ble_gap_event *event, void *arg);
static int notify_bytes(uint16_t handle, const uint8_t *data, size_t len)
{
    if (s_conn == NO_CONN) return BLE_HS_ENOTCONN;
    if (len > ble_att_mtu(s_conn) - 3) return BLE_HS_EMSGSIZE;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) return BLE_HS_ENOMEM;
    return ble_gatts_notify_custom(s_conn, handle, om);
}

static int notify_control(const char *json)
{
    size_t len = strlen(json);
    if (len == 0 || len > MAX_CONTROL) return BLE_HS_EMSGSIZE;
    int rc = notify_bytes(s_control_handle, (const uint8_t *)json, len);
    if (rc != 0) ESP_LOGW(TAG, "Control enqueue failed rc=%d mtu=%u", rc, ble_att_mtu(s_conn));
    return rc;
}

typedef struct {
    uint16_t conn;
    uint16_t handle;
    uint32_t epoch;
    uint32_t session;
    const uint8_t *data;
    size_t len;
    bool require_recording;
} tx_context_t;

static bool tx_valid(void *arg)
{
    const tx_context_t *tx = arg;
    return tx->conn != NO_CONN && tx->conn == s_conn &&
           tx->epoch == s_connection_epoch && tx->session == s_session &&
           (!tx->require_recording || s_recording) &&
           (tx->handle == s_audio_handle ? s_audio_subscribed : s_control_subscribed);
}

static int tx_once(void *arg)
{
    const tx_context_t *tx = arg;
    if (!tx_valid(arg)) return BLE_HS_ENOTCONN;
    if (tx->len > ble_att_mtu(tx->conn) - 3) return BLE_HS_EMSGSIZE;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(tx->data, tx->len);
    if (!om) return BLE_HS_ENOMEM;
    // NimBLE consumes om on both success and failure. Allocate anew per attempt.
    return ble_gatts_notify_custom(tx->conn, tx->handle, om);
}

static bool tx_retryable(int rc)
{
    return rc == BLE_HS_ENOMEM || rc == BLE_HS_EAGAIN || rc == BLE_HS_EBUSY;
}

static uint32_t tx_now_ms(void *arg)
{
    (void)arg;
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void tx_wait_ms(void *arg, uint32_t delay_ms)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(delay_ms) ? pdMS_TO_TICKS(delay_ms) : 1);
}

static int notify_worker(uint16_t handle, const uint8_t *data, size_t len,
                         uint32_t session, uint32_t epoch, bool require_recording,
                         uint32_t timeout_ms)
{
    tx_context_t tx = {s_conn, handle, epoch, session, data, len, require_recording};
    const ble_tx_retry_ops_t ops = {&tx, tx_valid, tx_once, tx_retryable, tx_now_ms, tx_wait_ms};
    const ble_tx_retry_result_t result = ble_tx_retry_send(&ops, timeout_ms, TX_RETRY_INTERVAL_MS);
    if (result.attempts > 1) s_tx_retries += result.attempts - 1;
    if (result.cancelled) return BLE_HS_ENOTCONN;
    if (result.code != 0) {
        ESP_LOGE(TAG, "TX failed session=%lu handle=%u rc=%d attempts=%lu wait_ms=%lu mtu=%u heap=%lu",
                 (unsigned long)session, handle, result.code, (unsigned long)result.attempts,
                 (unsigned long)result.elapsed_ms, ble_att_mtu(tx.conn),
                 (unsigned long)esp_get_free_heap_size());
    }
    return result.code;
}

static int notify_control_worker(const char *json, uint32_t session, uint32_t epoch,
                                bool require_recording)
{
    const size_t len = strlen(json);
    if (!len || len > MAX_CONTROL) return BLE_HS_EMSGSIZE;
    return notify_worker(s_control_handle, (const uint8_t *)json, len, session, epoch,
                         require_recording, CONTROL_TX_TIMEOUT_MS);
}

static void log_link(const char *event)
{
    struct ble_gap_conn_desc desc;
    if (s_conn != NO_CONN && ble_gap_conn_find(s_conn, &desc) == 0) {
        ESP_LOGI(TAG, "%s conn=%u mtu=%u interval_us=%lu latency=%u supervision_ms=%lu",
                 event, s_conn, ble_att_mtu(s_conn), (unsigned long)desc.conn_itvl * 1250,
                 desc.conn_latency, (unsigned long)desc.supervision_timeout * 10);
    }
}

static void request_audio_link(void)
{
    if (s_link_parameters_requested || s_conn == NO_CONN) return;
    s_link_parameters_requested = true;
    const struct ble_gap_upd_params params = {
        .itvl_min = 12, .itvl_max = 24, .latency = 0,
        .supervision_timeout = 400, .min_ce_len = 0, .max_ce_len = 0,
    };
    // Request 15-30 ms connection events for 20 ms voice frames. The central
    // retains final control; a refusal is diagnostic, never a handshake failure.
    int rc = ble_gap_update_params(s_conn, &params);
    ESP_LOGI(TAG, "Audio link parameter request rc=%d target_interval_us=15000..30000", rc);
    rc = ble_gap_set_data_len(s_conn, 251, 2120);
    ESP_LOGI(TAG, "Audio link data length request rc=%d tx_octets=251", rc);
}

static void send_error(uint32_t session, const char *message)
{
    char json[MAX_CONTROL + 1];
    snprintf(json, sizeof(json), "{\"v\":1,\"type\":\"device_error\",\"sessionId\":%lu,\"message\":\"%s\"}",
             (unsigned long)session, message);
    (void)notify_control(json);
}

static int ima_index_table[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
static int ima_step_table[89] = {
 7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767
};

static size_t encode_adpcm(const int16_t *pcm, uint8_t out[ADPCM_BYTES])
{
    int predictor = pcm[0];
    int index = 0;
    out[0] = (uint8_t)predictor; out[1] = (uint8_t)(predictor >> 8);
    out[2] = 0; out[3] = AUDIO_SAMPLES & 0xff; out[4] = AUDIO_SAMPLES >> 8; out[5] = 0;
    int nibble = 0;
    for (int i = 1; i < AUDIO_SAMPLES; ++i) {
        int diff = pcm[i] - predictor;
        int sign = diff < 0 ? 8 : 0;
        if (diff < 0) diff = -diff;
        int step = ima_step_table[index], delta = 0, vpdiff = step >> 3;
        if (diff >= step) { delta |= 4; diff -= step; vpdiff += step; }
        if (diff >= (step >> 1)) { delta |= 2; diff -= step >> 1; vpdiff += step >> 1; }
        if (diff >= (step >> 2)) { delta |= 1; vpdiff += step >> 2; }
        int code = delta | sign;
        predictor += sign ? -vpdiff : vpdiff;
        if (predictor > 32767) predictor = 32767;
        if (predictor < -32768) predictor = -32768;
        index += ima_index_table[code];
        if (index < 0) index = 0;
        if (index > 88) index = 88;
        if (nibble == 0) { out[6 + (i - 1) / 2] = (uint8_t)code; nibble = 1; }
        else { out[6 + (i - 1) / 2] |= (uint8_t)(code << 4); nibble = 0; }
    }
    return ADPCM_BYTES;
}

static int notify_audio(uint32_t session, uint32_t epoch, uint32_t sequence,
                        const uint8_t *payload, size_t len)
{
    if (s_conn == NO_CONN || epoch != s_connection_epoch || session != s_session) return BLE_HS_ENOTCONN;
    const size_t mtu_payload = ble_att_mtu(s_conn) - 3;
    if (mtu_payload <= 16) return BLE_HS_EMSGSIZE;
    const size_t chunk = mtu_payload - 16;
    uint8_t packet[16 + ADPCM_BYTES];
    uint8_t count = (uint8_t)((len + chunk - 1) / chunk);
    if (count == 0 || count > 64) return BLE_HS_EMSGSIZE;
    for (uint8_t part = 0; part < count; ++part) {
        size_t offset = part * chunk;
        size_t amount = len - offset < chunk ? len - offset : chunk;
        packet[0] = 0x41; packet[1] = 0x50; packet[2] = 1; packet[3] = 0;
        memcpy(packet + 4, &session, 4); memcpy(packet + 8, &sequence, 4);
        packet[12] = part; packet[13] = count; packet[14] = amount & 0xff; packet[15] = amount >> 8;
        memcpy(packet + 16, payload + offset, amount);
        int rc = notify_worker(s_audio_handle, packet, 16 + amount, session, epoch,
                               true, AUDIO_TX_TIMEOUT_MS);
        if (rc != 0) return rc;
    }
    return 0;
}

static bool boot_recording_requested(void)
{
    return s_recording;
}

static void audio_task(void *arg)
{
    (void)arg;
    // Runs once per boot, outside button/LVGL callbacks. Keep the existing
    // microphone format and prioritize a voice request over the startup sound.
    esp_err_t boot_err = bsp_audio_set_format(16000, 16, 1);
    if (boot_err == ESP_OK) {
        boot_err = boot_sound_play(boot_pcm_start,
                                  boot_pcm_end - boot_pcm_start,
                                  boot_recording_requested);
    }
    if (boot_err != ESP_OK) {
        ESP_LOGW(TAG, "Boot sound unavailable: %s; continuing voice input",
                 esp_err_to_name(boot_err));
    }
    int16_t pcm[AUDIO_SAMPLES];
    uint8_t encoded[ADPCM_BYTES];
    uint32_t sequence = 0;
    uint32_t capture_session = 0;
    uint32_t capture_epoch = 0;
    uint32_t stats_ms = 0;
    bool capturing = false;
    for (;;) {
        if (!s_recording) {
            capturing = false;
            sequence = 0;
            // A failed pause keeps the affected I2S channel and its sleep lock
            // held; retry after a short pause instead of aborting the device.
            // A capture request that arrives during the retry is picked up by
            // the loop condition, so no notification is lost.
            if (bsp_audio_pause() != ESP_OK) {
                ESP_LOGE(TAG, "I2S pause failed; retrying");
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            // A request during pause leaves a pending notification. Do not
            // clear notifications separately from this atomic wait.
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (!capturing || capture_session != s_session || capture_epoch != s_connection_epoch) {
            capture_session = s_session;
            capture_epoch = s_connection_epoch;
            sequence = 0;
            s_tx_retries = 0;
            char start_json[MAX_CONTROL + 1];
            snprintf(start_json, sizeof(start_json),
                     "{\"v\":1,\"type\":\"ptt_down\",\"sessionId\":%lu,\"seq\":0}",
                     (unsigned long)capture_session);
            // Retry in the audio worker; button and NimBLE callbacks never wait.
            int start_rc = notify_control_worker(start_json, capture_session, capture_epoch, true);
            if (start_rc != 0) {
                if (s_recording && capture_session == s_session && capture_epoch == s_connection_epoch) {
                    s_recording = false;
                    s_stop_requested = false;
                    ui_status_set_state(UI_STATUS_ERROR, "BLE send failed");
                }
                continue;
            }
            stats_ms = tx_now_ms(NULL);
            ESP_LOGI(TAG, "Capture start session=%lu epoch=%lu", (unsigned long)capture_session,
                     (unsigned long)capture_epoch);
            log_link("Capture link");
            // Pause also resets DMA between sessions; a failure returns to
            // idle so the next press can retry instead of rebooting.
            if (bsp_audio_pause() != ESP_OK) {
                if (s_recording && capture_session == s_session && capture_epoch == s_connection_epoch) {
                    s_recording = false;
                    s_stop_requested = false;
                    send_error(capture_session, "audio_pause");
                    ui_status_set_state(UI_STATUS_ERROR, "Microphone unavailable");
                }
                continue;
            }
            if (bsp_audio_set_format(16000, 16, 1) != ESP_OK) {
                if (s_recording && capture_session == s_session && capture_epoch == s_connection_epoch) {
                    s_recording = false;
                    s_stop_requested = false;
                    send_error(capture_session, "audio_open");
                    ui_status_set_state(UI_STATUS_ERROR, "Microphone unavailable");
                }
                continue;
            }
            capturing = true;
        }
        if (!s_recording || capture_session != s_session || capture_epoch != s_connection_epoch) continue;
        if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) {
            if (s_recording && capture_session == s_session && capture_epoch == s_connection_epoch) {
                s_stop_requested = false;
                send_error(capture_session, "audio_read");
                s_recording = false;
                ui_status_set_state(UI_STATUS_ERROR, "Microphone unavailable");
            }
            continue;
        }
        // A disconnect or a new session during blocking I/O invalidates PCM.
        if (!s_recording || capture_session != s_session || capture_epoch != s_connection_epoch) continue;
        if (!s_audio_subscribed || ble_att_mtu(s_conn) < 185) {
            s_stop_requested = false; send_error(s_session, "mtu");
            s_recording = false;
            ui_status_set_state(UI_STATUS_ERROR, "BLE audio unavailable");
            continue;
        }
        size_t encoded_len = encode_adpcm(pcm, encoded);
        int tx_rc = notify_audio(capture_session, capture_epoch, sequence, encoded, encoded_len);
        // A reconnect/new request invalidates this worker's old frame and state.
        if (capture_session != s_session || capture_epoch != s_connection_epoch || !s_recording) continue;
        if (tx_rc != 0) {
            s_stop_requested = true;
            ESP_LOGE(TAG, "Capture transport failure session=%lu next_sequence=%lu rc=%d retries=%lu",
                     (unsigned long)capture_session, (unsigned long)sequence, tx_rc,
                     (unsigned long)s_tx_retries);
            send_error(capture_session, "transport");
            s_recording = false;
            ui_status_set_state(UI_STATUS_ERROR, "BLE send failed");
        } else {
            ++sequence;
        }
        if (tx_now_ms(NULL) - stats_ms >= 1000) {
            ESP_LOGI(TAG, "Capture stats session=%lu queued_frames=%lu retries=%lu",
                     (unsigned long)capture_session, (unsigned long)sequence, (unsigned long)s_tx_retries);
            stats_ms = tx_now_ms(NULL);
        }
        if (s_stop_requested) {
            char json[MAX_CONTROL + 1];
            snprintf(json, sizeof(json), "{\"v\":1,\"type\":\"ptt_up\",\"sessionId\":%lu,\"seq\":%lu,\"finalSequence\":%lu}",
                     (unsigned long)capture_session, (unsigned long)++s_control_seq,
                     sequence ? (unsigned long)(sequence - 1) : EMPTY_FINAL_SEQUENCE);
            int stop_rc = notify_control_worker(json, capture_session, capture_epoch, false);
            ESP_LOGI(TAG, "Capture stop session=%lu queued_frames=%lu retries=%lu control_rc=%d",
                     (unsigned long)capture_session, (unsigned long)sequence,
                     (unsigned long)s_tx_retries, stop_rc);
            if (capture_session != s_session || capture_epoch != s_connection_epoch) continue;
            if (stop_rc != 0) {
                send_error(capture_session, "transport");
                ui_status_set_state(UI_STATUS_ERROR, "BLE send failed");
            }
            s_recording = false; s_stop_requested = false; sequence = 0;
        }
    }
}

static void button_cb(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (button < BSP_BTN_UP || button > BSP_BTN_OK) return;
    button_edge_t edge;
    switch (event) {
    case BSP_BTN_PRESS: edge = BUTTON_EDGE_PRESS; break;
    case BSP_BTN_RELEASE: edge = BUTTON_EDGE_RELEASE; break;
    case BSP_BTN_LONG: edge = BUTTON_EDGE_LONG; break;
    default: return; // Do not wait for or replay delayed CLICK/DOUBLE events.
    }
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    const uint32_t context = host_is_ready() ? s_connection_epoch : 0;
    button_gesture_t *gesture = &s_button_gestures[button];
    button_action_t action = button_gesture_update(gesture, edge, now_ms,
                                                   context, BUTTON_LONG_PRESS_MS);
    const uint32_t duration_ms = now_ms - gesture->started_ms;
    if (button == BSP_BTN_OK && event == BSP_BTN_PRESS) {
        // The driver's double-click state does not time a held second press.
        // An independent one-shot keeps OK long-press valid after quick taps.
        (void)esp_timer_stop(s_ok_hold_timer);
        esp_err_t err = esp_timer_start_once(s_ok_hold_timer,
                                             BUTTON_LONG_PRESS_MS * 1000);
        if (err != ESP_OK) ESP_LOGW(TAG, "OK hold timer: %s", esp_err_to_name(err));
    } else if (button == BSP_BTN_OK && event == BSP_BTN_RELEASE) {
        (void)esp_timer_stop(s_ok_hold_timer);
    }
    if (event == BSP_BTN_PRESS) {
        switch (button) {
        case BSP_BTN_UP: ui_status_touch(UI_STATUS_HINT_VOICE); break;
        case BSP_BTN_DOWN: ui_status_touch(UI_STATUS_HINT_SEND); break;
        case BSP_BTN_OK: ui_status_touch(UI_STATUS_HINT_UNDO); break;
        default: break;
        }
    }
    if (action == BUTTON_ACTION_NONE) return;
    if (button == BSP_BTN_UP) {
        if (s_conn == NO_CONN || !s_host_ready || action != BUTTON_ACTION_CLICK) return;
        // One toggle per short release, without the double-click wait.
        if (!s_recording) {
            s_session = esp_random();
            if (s_session == 0) s_session = 1;
            s_control_seq = 0;
            s_stop_requested = false;
            s_recording = true;
            ui_status_set_state(UI_STATUS_RECORDING, "Listening");
            if (s_audio_task) xTaskNotifyGive(s_audio_task);
        } else {
            s_stop_requested = true;
            ui_status_set_state(UI_STATUS_PROCESSING, "Processing");
        }
        return;
    }
    if (button == BSP_BTN_DOWN && action == BUTTON_ACTION_CLICK) {
        if (!host_is_ready()) return;
        send_button_event(button, "click", 0);
        ui_status_set_state(UI_STATUS_READY, "Send message");
        return;
    }
    if (button == BSP_BTN_OK) {
        if (!host_is_ready()) return;
        if (action == BUTTON_ACTION_LONG) {
            // While a voice request is still being captured, the edit button is
            // an ESC/cancel gesture. The Worker owns the full cloud/paste
            // cancellation window; this flag only releases the microphone.
            if (s_recording) {
                s_stop_requested = true;
                send_button_event(button, "long", duration_ms);
                ui_status_set_state(UI_STATUS_PROCESSING, "Cancelling");
            } else {
                send_button_event(button, "long", duration_ms);
                ui_status_set_state(UI_STATUS_READY, "Clear input");
            }
        } else if (action == BUTTON_ACTION_CLICK) {
            if (s_recording) {
                s_stop_requested = true;
                send_button_event(button, "click", duration_ms);
                ui_status_set_state(UI_STATUS_PROCESSING, "Cancelling");
            } else {
                send_button_event(button, "click", duration_ms);
                ui_status_set_state(UI_STATUS_READY, "Delete character");
            }
        }
    }
}

static int access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr;
    int kind = (int)(intptr_t)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && kind == 3) {
        const char info[] = "{\"v\":1,\"type\":\"hello\",\"device\":\"ai-passport\",\"fw\":\"0.1.0\",\"codec\":\"ima-adpcm\",\"sampleRate\":16000,\"channels\":1,\"frameMs\":20}";
        return os_mbuf_append(ctxt->om, info, strlen(info));
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || kind != 1) return BLE_ATT_ERR_READ_NOT_PERMITTED;
    char value[MAX_CONTROL + 1] = {0}; uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, value, MAX_CONTROL, &len) != 0 || len == 0 || len > MAX_CONTROL) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    cJSON *root = cJSON_ParseWithLength(value, len);
    if (!root || !cJSON_IsObject(root) || !cJSON_IsNumber(cJSON_GetObjectItem(root, "v")) || cJSON_GetObjectItem(root, "v")->valueint != 1) { cJSON_Delete(root); return BLE_ATT_ERR_UNLIKELY;
    }
    const cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "host_ready") == 0) {
        if (conn != s_conn || !s_control_subscribed || !s_audio_subscribed || ble_att_mtu(conn) < 185) {
            ESP_LOGW(TAG, "Host ready rejected conn=%u control=%u audio=%u mtu=%u",
                     conn, s_control_subscribed, s_audio_subscribed, ble_att_mtu(conn));
            cJSON_Delete(root);
            return BLE_ATT_ERR_UNLIKELY;
        }
        s_host_ready = true;
        ESP_LOGI(TAG, "Host ready epoch=%lu", (unsigned long)s_connection_epoch);
        log_link("Host link");
        request_audio_link();
        ui_status_set_state(UI_STATUS_READY, "Ready");
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "host_state") == 0) {
        const cJSON *state = cJSON_GetObjectItem(root, "state");
        if (cJSON_IsString(state)) {
            if (strcmp(state->valuestring, "ready") == 0) {
                s_host_ready = true;
                ui_status_set_state(UI_STATUS_READY, "Ready");
            } else if (strcmp(state->valuestring, "recording") == 0) {
                ui_status_set_state(UI_STATUS_RECORDING, "Listening");
            } else if (strcmp(state->valuestring, "processing") == 0) {
                ui_status_set_state(UI_STATUS_PROCESSING, "Processing");
            } else if (strcmp(state->valuestring, "success") == 0) {
                ui_status_set_state(UI_STATUS_SUCCESS, "Sent");
            } else if (strcmp(state->valuestring, "error") == 0) {
                ESP_LOGW(TAG, "Host reported error session=%lu recording=%u; cancelling capture",
                         (unsigned long)s_session, s_recording);
                s_recording = false;
                s_stop_requested = false;
                ui_status_set_state(UI_STATUS_ERROR, "Try again");
            }
        }
    }
    cJSON_Delete(root); (void)conn; return 0;
}

static const struct ble_gatt_svc_def services[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &s_service_uuid.u, .characteristics = (struct ble_gatt_chr_def[]) {
        { .uuid = &s_control_uuid.u, .access_cb = access_cb, .arg = (void *)(intptr_t)1, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_control_handle },
        { .uuid = &s_audio_uuid.u, .access_cb = access_cb, .arg = (void *)(intptr_t)2, .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_audio_handle },
        { .uuid = &s_info_uuid.u, .access_cb = access_cb, .arg = (void *)(intptr_t)3, .flags = BLE_GATT_CHR_F_READ, .val_handle = &s_info_handle },
        {0} } }, {0}
};

static int advertise(void)
{
    // Flags + a 128-bit UUID fit in the 31-byte primary packet. The complete
    // name goes in scan response; placing both in one packet exceeds the limit.
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;

    struct ble_hs_adv_fields response = {0};
    response.name = (const uint8_t *)DEVICE_NAME;
    response.name_len = strlen(DEVICE_NAME);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) return rc;

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}

static void on_sync(void)
{
    if (ble_hs_id_infer_auto(0, &s_addr_type) == 0) {
        (void)advertise();
    }
}
static void on_reset(int reason) { ESP_LOGE(TAG, "NimBLE reset %d", reason); }
static void host_task(void *arg) { (void)arg; nimble_port_run(); nimble_port_freertos_deinit(); }

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        if (event->connect.status == 0) {
            ++s_connection_epoch;
            s_conn = event->connect.conn_handle;
            s_host_ready = false; s_control_subscribed = false; s_audio_subscribed = false;
            s_hello_sent = false; s_link_parameters_requested = false;
            log_link("Connected");
            ui_status_set_state(UI_STATUS_STARTING, "Connecting");
        } else {
            ESP_LOGW(TAG, "Connection failed status=%d", event->connect.status);
            ui_status_set_state(UI_STATUS_DISCONNECTED, "Waiting for Vokie");
            advertise();
        }
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        ESP_LOGW(TAG, "Disconnected reason=%d session=%lu recording=%u", event->disconnect.reason,
                 (unsigned long)s_session, s_recording);
        ++s_connection_epoch;
        s_recording = false; s_stop_requested = false; s_host_ready = false;
        s_control_subscribed = false; s_audio_subscribed = false; s_conn = NO_CONN;
        s_hello_sent = false; s_link_parameters_requested = false;
        ui_status_set_state(UI_STATUS_DISCONNECTED, "Waiting for Vokie");
        advertise();
    } else if (event->type == BLE_GAP_EVENT_SUBSCRIBE && event->subscribe.conn_handle == s_conn) {
        // NimBLE reports the characteristic value handle, not the CCCD handle.
        if (event->subscribe.attr_handle == s_control_handle) {
            s_control_subscribed = event->subscribe.cur_notify;
        }
        if (event->subscribe.attr_handle == s_audio_handle) {
            s_audio_subscribed = event->subscribe.cur_notify;
        }
        ESP_LOGI(TAG, "Subscribe handle=%u notify=%u control=%u audio=%u", event->subscribe.attr_handle,
                 event->subscribe.cur_notify, s_control_subscribed, s_audio_subscribed);
        if (!s_control_subscribed || !s_audio_subscribed) {
            s_hello_sent = false;
            if (s_recording) ESP_LOGW(TAG, "Subscription removed during capture session=%lu", (unsigned long)s_session);
        } else if (!s_hello_sent) {
            const char hello[] = "{\"v\":1,\"type\":\"hello\",\"device\":\"ai-passport\",\"fw\":\"0.1.0\",\"codec\":\"ima-adpcm\",\"sampleRate\":16000,\"channels\":1,\"frameMs\":20}";
            s_hello_sent = notify_control(hello) == 0;
        }
    } else if (event->type == BLE_GAP_EVENT_MTU) {
        ESP_LOGI(TAG, "MTU updated conn=%u mtu=%u", event->mtu.conn_handle, event->mtu.value);
    } else if (event->type == BLE_GAP_EVENT_CONN_UPDATE) {
        ESP_LOGI(TAG, "Connection parameter update status=%d", event->conn_update.status);
        log_link("Updated link");
    } else if (event->type == BLE_GAP_EVENT_DATA_LEN_CHG) {
        ESP_LOGI(TAG, "Data length updated conn=%u tx_octets=%u rx_octets=%u",
                 event->data_len_chg.conn_handle, event->data_len_chg.max_tx_octets,
                 event->data_len_chg.max_rx_octets);
    } else if (event->type == BLE_GAP_EVENT_NOTIFY_TX && event->notify_tx.status != 0) {
        // Do not synchronously print every retry on the time-sensitive TX path.
        // Persistent failures and accumulated retries are reported by the worker.
        ESP_LOGD(TAG, "Notify rejected conn=%u handle=%u rc=%d", event->notify_tx.conn_handle,
                 event->notify_tx.attr_handle, event->notify_tx.status);
    }
    return 0;
}

esp_err_t vokie_ble_start(void)
{
    if (s_started) return ESP_OK;
    // Per-frame NimBLE INFO traffic hides failures and adds console work to the
    // audio path. Keep our bounded session/connection diagnostics instead.
    esp_log_level_set("NimBLE", ESP_LOG_WARN);
    ESP_LOGI(TAG, "BLE audio transport: bounded TX retries and link tuning");
    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) return err;
    // The worker owns stream open/close as well as all PCM I/O.
    if (!s_ok_hold_timer) {
        const esp_timer_create_args_t timer = {
            .callback = ok_hold_expired, .name = "ok_hold",
        };
        err = esp_timer_create(&timer, &s_ok_hold_timer);
        if (err != ESP_OK) return err;
    }
    if (bsp_button_init(button_cb, NULL) != ESP_OK) return ESP_FAIL;
    int rc = nimble_port_init(); if (rc != ESP_OK) return ESP_FAIL;
    ble_svc_gap_init(); ble_svc_gatt_init(); ble_svc_gap_device_name_set(DEVICE_NAME);
    if (ble_gatts_count_cfg(services) != 0 || ble_gatts_add_svcs(services) != 0) return ESP_FAIL;
    ble_hs_cfg.reset_cb = on_reset; ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    if (xTaskCreate(audio_task, "vokie_audio", 4096, NULL, 5, &s_audio_task) != pdPASS) return ESP_ERR_NO_MEM;
    s_started = true; ESP_LOGI(TAG, "Vokie Passport BLE ready"); return ESP_OK;
}

bool vokie_ble_ready(void) { return s_control_subscribed && s_audio_subscribed && s_host_ready; }
