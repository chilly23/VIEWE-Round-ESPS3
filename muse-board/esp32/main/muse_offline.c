/* Local offline adapter for the official Muse UI, authored for VIEWE ROUND.
 * SPDX-License-Identifier: Apache-2.0
 * Uses the upstream UI, procedural avatar, fonts, settings and input unchanged
 * with only a local, interaction-driven offline state adapter.
 */
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "muse_board.h"
#include "muse_battery.h"
#include "muse_input.h"
#include "muse_link.h"
#include "muse_settings.h"
#include "muse_state.h"
#include "muse_ui.h"

static void enter(muse_mode_t mode, const char *caption)
{
    muse_state_set_mode(mode);
    muse_state_set_caption("%s", caption);
    muse_state_set_progress(0);
    muse_state_set_level(0);
}

static void demo_task(void *unused)
{
    (void)unused;
    muse_board = muse_board_get();
    ESP_ERROR_CHECK(muse_board->init());
    ESP_ERROR_CHECK(muse_settings_init());
    /* No network stack, account session, voice pipeline or radio is started.
     * These settings also make the untouched settings pages report that. */
    muse_settings_set_wifi_on(false);
    muse_settings_set_ble_on(false);
    muse_settings_set_sleep_s(0);
    muse_state_init();
    muse_battery_init();
    muse_link_set_state(MUSE_LINK_OFFLINE);
    muse_ui_set_offline_demo(true);
    enter(MUSE_MODE_BOOT, "");
    ESP_ERROR_CHECK(muse_ui_start());
    QueueHandle_t queue = xQueueCreate(16, sizeof(muse_input_event_t));
    ESP_ERROR_CHECK(queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(muse_input_start(queue));
    ESP_LOGI("muse.offline", "Official Muse UI on VIEWE: offline demo ready. Hold BOOT to simulate talk.");

    bool held = false;
    int phase = 0;
    int64_t started = esp_timer_get_time();
    const float duration[] = {1.8f, 8.0f, 3.0f, 2.2f, 5.0f, 4.0f};
    const muse_mode_t modes[] = {MUSE_MODE_BOOT, MUSE_MODE_IDLE, MUSE_MODE_LISTENING,
                                MUSE_MODE_THINKING, MUSE_MODE_SPEAKING, MUSE_MODE_IDLE};
    const char *captions[] = {"", "", "", "", "Hello!", ""};
    for (;;) {
        muse_input_event_t event;
        if (xQueueReceive(queue, &event, pdMS_TO_TICKS(40))) {
            if (event.type == MUSE_PTT_DOWN) {
                held = true;
                phase = 2;
                started = esp_timer_get_time();
                enter(MUSE_MODE_LISTENING, "");
            } else if (held) {
                held = false;
                phase = 3;
                started = esp_timer_get_time();
                enter(MUSE_MODE_THINKING, "");
            }
        }
        if (muse_state_asleep() || muse_state_mode(NULL) == MUSE_MODE_OFF) continue;
        float elapsed = (esp_timer_get_time() - started) / 1000000.0f;
        if (!held && phase != 1 && elapsed >= duration[phase]) {
            phase = phase == 5 ? 1 : phase + 1;
            started = esp_timer_get_time();
            elapsed = 0;
            enter(modes[phase], captions[phase]);
            /* Pet reactions remain driven by the original touch handler. */
        }
        if (phase == 2 || phase == 4) {
            float level = 0.30f + 0.25f * sinf(elapsed * 11) + 0.16f * sinf(elapsed * 23);
            muse_state_set_level(fmaxf(0.05f, level));
            muse_state_set_progress(fminf(1.0f, elapsed / (held ? 15.0f : duration[phase])));
        }
    }
}

void muse_offline_run(void)
{
    /* Match the full UI's separate startup task rather than use app_main's
     * small stack for LVGL and settings-page construction. */
    ESP_ERROR_CHECK(xTaskCreate(demo_task, "muse_offline", 12288, NULL, 5, NULL)
                    == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
