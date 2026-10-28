#include "device_diag.h"
#include "diag_journal.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "device_diag";
static const esp_partition_t *s_partition;
static QueueHandle_t s_queue;
static SemaphoreHandle_t s_gate;
static bool s_audio_active;
static uint32_t s_boot_id, s_dropped;
static portMUX_TYPE s_drop_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct { uint8_t *cache; uint32_t base; } read_context_t;

static bool read_bytes(void *arg, uint32_t offset, void *data, size_t size)
{
    read_context_t *ctx = arg;
    if (ctx->cache && size == sizeof(diag_record_t)) {
        const uint32_t base = offset & ~(DIAG_SECTOR_BYTES - 1u);
        if (ctx->base != base) {
            if (esp_partition_read(s_partition, base, ctx->cache, DIAG_SECTOR_BYTES) != ESP_OK) return false;
            ctx->base = base;
        }
        memcpy(data, ctx->cache + offset - base, size);
        return true;
    }
    return esp_partition_read(s_partition, offset, data, size) == ESP_OK;
}

static bool write_bytes(void *arg, uint32_t offset, const void *data, size_t size)
{
    (void)arg;
    return esp_partition_write(s_partition, offset, data, size) == ESP_OK;
}

static bool erase_sector(void *arg, uint32_t offset, size_t size)
{
    (void)arg;
    return offset >= DIAG_SECTOR_BYTES &&
           esp_partition_erase_range(s_partition, offset, size) == ESP_OK;
}

void device_diag_emit(device_diag_event_t event, uint32_t session,
                      uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                      uint32_t e, uint32_t f, uint32_t g, uint32_t h)
{
    if (!s_queue) return;
    const diag_record_t record = {.event = event, .boot_id = s_boot_id,
        .uptime_ms = (uint32_t)(esp_timer_get_time() / 1000), .session = session,
        .args = {a, b, c, d, e, f, g, h}};
    if (xQueueSend(s_queue, &record, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_drop_lock);
        s_dropped += event == DD_DROPPED ? a : 1;
        portEXIT_CRITICAL(&s_drop_lock);
    }
}

void device_diag_audio_enter(void)
{
    if (!s_gate) return;
    xSemaphoreTake(s_gate, portMAX_DELAY);
    s_audio_active = true;
    xSemaphoreGive(s_gate);
}

void device_diag_audio_idle(void)
{
    if (!s_gate) return;
    xSemaphoreTake(s_gate, portMAX_DELAY);
    s_audio_active = false;
    xSemaphoreGive(s_gate);
}

static void worker(void *arg)
{
    (void)arg;
    read_context_t ctx = {.cache = malloc(DIAG_SECTOR_BYTES), .base = UINT32_MAX};
    const diag_io_t io = {&ctx, read_bytes, write_bytes, erase_sector};
    diag_journal_t journal;
    // Boot scanning obeys the same gate as runtime appends; it may otherwise
    // overlap the startup sound or an unusually early capture request.
    for (;;) {
        xSemaphoreTake(s_gate, portMAX_DELAY);
        if (!s_audio_active) break;
        xSemaphoreGive(s_gate);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    diag_result_t result = diag_journal_init(&journal, &io, s_partition->size);
    xSemaphoreGive(s_gate);
    free(ctx.cache);
    ctx.cache = NULL;
    if (result != DIAG_OK) {
        ESP_LOGE(TAG, "Journal unavailable rc=%d; no automatic format or erase", result);
        // Keep the bounded queue valid for producers; logging failure must not
        // change button, microphone, or BLE behaviour. Do not retry bad storage.
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Persistent journal ready: slots=%lu next=%lu offset=0x%lx",
             (unsigned long)journal.slots, (unsigned long)journal.next_sequence,
             (unsigned long)s_partition->address);
    device_diag_emit(DD_BOOT, 0, esp_reset_reason(), esp_get_free_heap_size(),
                     esp_get_minimum_free_heap_size(), journal.next_sequence,
                     journal.slots, 0, 0, 0);
    uint32_t build[8];
    memcpy(build, esp_app_get_description()->app_elf_sha256, sizeof(build));
    device_diag_emit(DD_BUILD, 0, build[0], build[1], build[2], build[3], build[4], build[5], build[6], build[7]);
    uint32_t last_slow_ms = 0;
    for (;;) {
        diag_record_t event;
        if (xQueueReceive(s_queue, &event, pdMS_TO_TICKS(15 * 60 * 1000)) != pdTRUE) {
            device_diag_emit(DD_HEARTBEAT, 0, esp_get_free_heap_size(),
                             esp_get_minimum_free_heap_size(), uxTaskGetStackHighWaterMark(NULL),
                             journal.next_sequence, 0, 0, 0, 0);
            continue;
        }
        do {
            xSemaphoreTake(s_gate, portMAX_DELAY);
            const int64_t start = esp_timer_get_time();
            // Flash programs also affect caches. The portable journal gate
            // performs no read/write/erase while audio owns this mutex gate.
            result = diag_journal_append(&journal, &event, !s_audio_active);
            const uint32_t duration = (uint32_t)(esp_timer_get_time() - start);
            xSemaphoreGive(s_gate);
            if (result == DIAG_BUSY) vTaskDelay(pdMS_TO_TICKS(20));
            const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
            if (result == DIAG_OK && duration > 10000 && now_ms - last_slow_ms >= 5000) {
                last_slow_ms = now_ms;
                device_diag_emit(DD_STORAGE_SLOW, event.session, duration, journal.next_sequence,
                                 uxQueueMessagesWaiting(s_queue), 0, 0, 0, 0, 0);
            }
        } while (result == DIAG_BUSY);
        if (result != DIAG_OK) {
            ESP_LOGE(TAG, "Journal write failed rc=%d; diagnostic writer stopped", result);
            vTaskDelete(NULL);
            return;
        }
        portENTER_CRITICAL(&s_drop_lock);
        const uint32_t dropped = s_dropped;
        s_dropped = 0;
        portEXIT_CRITICAL(&s_drop_lock);
        if (dropped) device_diag_emit(DD_DROPPED, 0, dropped, 0, 0, 0, 0, 0, 0, 0);
    }
}

esp_err_t device_diag_init(void)
{
    if (s_queue) return ESP_OK;
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "diag_log");
    if (!s_partition || s_partition->address != 0x310000 || s_partition->size != DIAG_PARTITION_BYTES)
        return ESP_ERR_NOT_FOUND;
    s_gate = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(192, sizeof(diag_record_t));
    s_boot_id = esp_random();
    if (!s_gate || !s_queue) {
        if (s_gate) vSemaphoreDelete(s_gate);
        if (s_queue) vQueueDelete(s_queue);
        s_gate = NULL;
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(worker, "device_diag", 4096, NULL, 1, NULL) != pdPASS) {
        vSemaphoreDelete(s_gate);
        vQueueDelete(s_queue);
        s_gate = NULL;
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
