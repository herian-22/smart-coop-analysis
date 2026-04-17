/**
 * @file lcd_face.cpp
 * @brief Context-aware face animation with 3-char wide mouth.
 *
 * Features:
 *   - Temperature display at col 13-15 row 0 (e.g. "28C")
 *   - Random blink timing (2000-6000ms) via esp_random()
 *   - ERROR mode with X eyes for sensor failure
 *
 * LCD Layout (16x2):
 *   Row 0:  ....[S]E....E..28C   Eyes + temp readout + sweat(HOT)
 *   Row 1:  ...H..MMM...H.....   Hands col 3/12, Mouth col 6-7-8
 */

#include "lcd_face.h"
#include "face_assets.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_random.h"
#include <stdio.h>

static const char *TAG = "LcdFace";

// --- Layout positions ---
#define EYE_LEFT_COL     5
#define EYE_RIGHT_COL    10
#define EYE_ROW          0
#define MOUTH_LEFT_COL   6
#define MOUTH_CENTER_COL 7
#define MOUTH_RIGHT_COL  8
#define MOUTH_ROW        1
#define HAND_LEFT_COL    3
#define HAND_RIGHT_COL   12
#define HAND_ROW         1
#define SWEAT_COL        4
#define SWEAT_ROW        0
#define TEMP_COL         13   // Temperature display col 13-15
#define TEMP_ROW         0

// --- Random blink range ---
#define BLINK_MIN_MS     2000
#define BLINK_MAX_MS     6000

// --- Timing (overridden per mode) ---
static uint32_t s_blink_open_ms   = 3000;
static uint32_t s_blink_close_ms  = 150;
static uint32_t s_mouth_toggle_ms = 1000;
static uint32_t s_hand_wave_ms    = 500;

// --- State ---
typedef enum { EYE_STATE_OPEN, EYE_STATE_CLOSED } eye_state_t;
typedef enum { MOUTH_STATE_CLOSED, MOUTH_STATE_OPEN } mouth_state_t;
typedef enum { HAND_PHASE_LEFT_UP, HAND_PHASE_RIGHT_UP } hand_phase_t;

static hd44780_t        *s_lcd         = NULL;
static SemaphoreHandle_t s_i2c_mutex   = NULL;

static eye_state_t      s_eye_state       = EYE_STATE_OPEN;
static mouth_state_t    s_mouth_state     = MOUTH_STATE_CLOSED;
static hand_phase_t     s_hand_phase      = HAND_PHASE_LEFT_UP;
static volatile bool    s_is_speaking     = true;
static volatile bool    s_is_waving       = true;
static volatile int     s_display_mode    = DISPLAY_MODE_NORMAL;
static volatile int     s_pending_mode    = -1;
static bool             s_initialized     = false;
static bool             s_need_full_redraw = true;
static bool             s_sweat_visible   = false;
static bool             s_backlight_on    = true;
static float            s_last_drawn_temp = -999.0f;
static int              s_jitter_phase    = 0;     // COLD: column offset 0 or 1
static int              s_sweat_row       = 0;     // HOT: sweat drip position

static TickType_t       s_eye_last_change   = 0;
static TickType_t       s_mouth_last_change = 0;
static TickType_t       s_hand_last_change  = 0;
static TickType_t       s_backlight_last    = 0;
static TickType_t       s_sweat_last        = 0;

// --- Helpers ---

/**
 * @brief Generate a random blink interval between BLINK_MIN_MS and BLINK_MAX_MS.
 */
static uint32_t random_blink_interval(void)
{
    uint32_t range = BLINK_MAX_MS - BLINK_MIN_MS;
    return BLINK_MIN_MS + (esp_random() % (range + 1));
}

static void put_custom_char(uint8_t col, uint8_t row, uint8_t cgram_id)
{
    hd44780_gotoxy(s_lcd, col, row);
    hd44780_putc(s_lcd, (char)cgram_id);
}

static void clear_position(uint8_t col, uint8_t row)
{
    hd44780_gotoxy(s_lcd, col, row);
    hd44780_putc(s_lcd, ' ');
}

static void draw_eyes(void)
{
    uint8_t id = (s_eye_state == EYE_STATE_OPEN) ? CGRAM_EYE_PRIMARY : CGRAM_EYE_BLINK;
    put_custom_char(EYE_LEFT_COL,  EYE_ROW, id);
    put_custom_char(EYE_RIGHT_COL, EYE_ROW, id);
}

static void draw_mouth(void)
{
    if (s_mouth_state == MOUTH_STATE_OPEN) {
        put_custom_char(MOUTH_LEFT_COL,   MOUTH_ROW, CGRAM_MOUTH_LEFT);
        put_custom_char(MOUTH_CENTER_COL, MOUTH_ROW, CGRAM_MOUTH_CENTER);
        put_custom_char(MOUTH_RIGHT_COL,  MOUTH_ROW, CGRAM_MOUTH_RIGHT);
    } else {
        put_custom_char(MOUTH_LEFT_COL,   MOUTH_ROW, CGRAM_EYE_BLINK);
        put_custom_char(MOUTH_CENTER_COL, MOUTH_ROW, CGRAM_EYE_BLINK);
        put_custom_char(MOUTH_RIGHT_COL,  MOUTH_ROW, CGRAM_EYE_BLINK);
    }
}

/**
 * @brief Draw hands with mode-specific behavior:
 *   NORMAL — symmetric wave (both alternate)
 *   COLD   — both hands shiver together (same direction, fast)
 *   HOT    — left hand lemas (always down), right hand fans slowly
 *   ERROR  — both hands frozen down
 */
static void draw_hands(void)
{
    if (s_display_mode == DISPLAY_MODE_HOT) {
        clear_position(EYE_RIGHT_COL + 1, EYE_ROW); // clear salute location
        // HOT: left hand droops, only right hand fans
        put_custom_char(HAND_LEFT_COL, HAND_ROW, CGRAM_HAND_DOWN);
        uint8_t fan_id = (s_hand_phase == HAND_PHASE_LEFT_UP) ? CGRAM_HAND_UP : CGRAM_HAND_DOWN;
        put_custom_char(HAND_RIGHT_COL, HAND_ROW, fan_id);
    } else if (s_display_mode == DISPLAY_MODE_COLD) {
        clear_position(EYE_RIGHT_COL + 1, EYE_ROW); // clear salute location
        // COLD: both hands shiver in same direction (synchronized shaking)
        uint8_t shiver_id = (s_hand_phase == HAND_PHASE_LEFT_UP) ? CGRAM_HAND_UP : CGRAM_HAND_DOWN;
        put_custom_char(HAND_LEFT_COL,  HAND_ROW, shiver_id);
        put_custom_char(HAND_RIGHT_COL, HAND_ROW, shiver_id);
    } else if (s_display_mode == DISPLAY_MODE_ERROR) {
        clear_position(EYE_RIGHT_COL + 1, EYE_ROW); // clear salute location
        // ERROR: both hands frozen down (confused)
        put_custom_char(HAND_LEFT_COL,  HAND_ROW, CGRAM_HAND_DOWN);
        put_custom_char(HAND_RIGHT_COL, HAND_ROW, CGRAM_HAND_DOWN);
    } else {
        // NORMAL: Saluting character!
        clear_position(HAND_LEFT_COL, HAND_ROW);          // Clear old left hand
        clear_position(HAND_RIGHT_COL, HAND_ROW);         // Clear old right hand
        
        // Draw salute at Right Eye level
        if (s_hand_phase == HAND_PHASE_LEFT_UP) {
            put_custom_char(EYE_RIGHT_COL + 1, EYE_ROW, CGRAM_HAND_UP);
        } else {
            put_custom_char(EYE_RIGHT_COL + 1, EYE_ROW, CGRAM_HAND_DOWN);
        }
    }
}

/**
 * @brief Draw sweat with drip animation.
 * In HOT mode, sweat alternates between row 0 and row 1 (falling effect).
 */
static void draw_sweat(bool visible)
{
    if (visible) {
        // Clear previous position
        clear_position(SWEAT_COL, 0);
        clear_position(SWEAT_COL, 1);
        // Draw at current drip row
        put_custom_char(SWEAT_COL, s_sweat_row, CGRAM_SWEAT);
    } else {
        clear_position(SWEAT_COL, 0);
        clear_position(SWEAT_COL, 1);
    }
    s_sweat_visible = visible;
}

/**
 * @brief Draw temperature reading at col 13-15 row 0.
 * Format: "28C" or "--C" if in ERROR mode.
 * Only redraws when value changes (to avoid I2C spam).
 */
static void draw_temperature(void)
{
    char buf[4];  // "28C\0"

    if (s_display_mode == DISPLAY_MODE_ERROR) {
        // Sensor failed — show dashes
        if (s_last_drawn_temp != -999.0f || s_need_full_redraw) {
            snprintf(buf, sizeof(buf), "--C");
            hd44780_gotoxy(s_lcd, TEMP_COL, TEMP_ROW);
            hd44780_puts(s_lcd, buf);
            s_last_drawn_temp = -999.0f;
        }
        return;
    }

    float current_temp = sensorData.temp;
    // Only redraw if temp changed (rounded to integer)
    int cur_int = (int)(current_temp + 0.5f);
    int last_int = (int)(s_last_drawn_temp + 0.5f);

    if (cur_int != last_int || s_need_full_redraw) {
        // Clamp to 2 digits for display
        if (cur_int > 99) cur_int = 99;
        if (cur_int < -9) cur_int = -9;
        snprintf(buf, sizeof(buf), "%2dC", cur_int);
        hd44780_gotoxy(s_lcd, TEMP_COL, TEMP_ROW);
        hd44780_puts(s_lcd, buf);
        s_last_drawn_temp = current_temp;
    }
}

static void apply_mode_timing(int mode)
{
    switch ((DisplayMode)mode) {
        case DISPLAY_MODE_NORMAL:
            s_blink_open_ms   = random_blink_interval();
            s_blink_close_ms  = 150;
            s_mouth_toggle_ms = 0;       // Smile stays
            s_hand_wave_ms    = 500;     // Cheerful wave
            s_is_speaking     = false;
            s_is_waving       = true;
            break;
        case DISPLAY_MODE_COLD:
            s_blink_open_ms   = 500;     // Rapid squint
            s_blink_close_ms  = 150;
            s_mouth_toggle_ms = 100;     // Fast chatter
            s_hand_wave_ms    = 80;      // ★ Shivering! Very fast
            s_is_speaking     = true;
            s_is_waving       = true;
            break;
        case DISPLAY_MODE_HOT:
            s_blink_open_ms   = random_blink_interval();
            s_blink_close_ms  = 150;
            s_mouth_toggle_ms = 300;     // Slow panting
            s_hand_wave_ms    = 1000;    // ★ Sluggish fanning
            s_is_speaking     = true;
            s_is_waving       = true;    // Only right hand fans
            break;
        case DISPLAY_MODE_ERROR:
            s_blink_open_ms   = 1000;    // Slow confused blink
            s_blink_close_ms  = 300;
            s_mouth_toggle_ms = 800;     // Slow mouth wiggle
            s_hand_wave_ms    = 0;       // Hands frozen
            s_is_speaking     = true;
            s_is_waving       = false;
            break;
    }
}

// --- Public API ---

esp_err_t face_anim_init(hd44780_t *lcd, SemaphoreHandle_t i2c_mutex)
{
    if (lcd == NULL) {
        ESP_LOGE(TAG, "LCD descriptor is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    s_lcd = lcd;
    s_i2c_mutex = i2c_mutex;

    esp_err_t ret = face_assets_upload_base(s_lcd);
    if (ret != ESP_OK) return ret;

    ret = face_assets_upload_for_mode(s_lcd, DISPLAY_MODE_NORMAL);
    if (ret != ESP_OK) return ret;

    s_display_mode = DISPLAY_MODE_NORMAL;
    apply_mode_timing(DISPLAY_MODE_NORMAL);

    s_eye_state         = EYE_STATE_OPEN;
    s_mouth_state       = MOUTH_STATE_CLOSED;
    s_hand_phase        = HAND_PHASE_LEFT_UP;
    s_eye_last_change   = xTaskGetTickCount();
    s_mouth_last_change = xTaskGetTickCount();
    s_hand_last_change  = xTaskGetTickCount();
    s_backlight_last    = xTaskGetTickCount();
    s_need_full_redraw  = true;
    s_sweat_visible     = false;
    s_backlight_on      = true;
    s_last_drawn_temp   = -999.0f;
    s_initialized       = true;

    ESP_LOGI(TAG, "Face animation initialized (3-char mouth, temp display, random blink)");
    return ESP_OK;
}

void face_set_display_mode(int mode)
{
    if (mode != s_display_mode) {
        s_pending_mode = mode;
    }
}

int face_get_display_mode(void) { return s_display_mode; }

void face_anim_draw_frame(void)
{
    if (!s_initialized || s_lcd == NULL) return;

    TickType_t now = xTaskGetTickCount();
    bool eyes_changed  = false;
    bool mouth_changed = false;
    bool hands_changed = false;

    // --- Handle pending mode change ---
    if (s_pending_mode >= 0) {
        int new_mode = s_pending_mode;
        s_pending_mode = -1;

        ESP_LOGI(TAG, "Mode change: %d -> %d", s_display_mode, new_mode);
        face_assets_upload_for_mode(s_lcd, new_mode);
        apply_mode_timing(new_mode);
        s_display_mode = new_mode;

        if (new_mode != DISPLAY_MODE_HOT && s_sweat_visible) {
            draw_sweat(false);
        }
        if (new_mode != DISPLAY_MODE_HOT && !s_backlight_on) {
            hd44780_switch_backlight(s_lcd, true);
            s_backlight_on = true;
        }
        // Re-enable waving if not in ERROR mode
        if (new_mode != DISPLAY_MODE_ERROR) {
            s_is_waving = true;
        }

        s_need_full_redraw  = true;
        s_last_drawn_temp   = -999.0f;
        s_eye_last_change   = now;
        s_mouth_last_change = now;
        s_hand_last_change  = now;
        s_sweat_last        = now;
        s_jitter_phase      = 0;
        s_sweat_row         = 0;
    }

    // --- Full redraw ---
    if (s_need_full_redraw) {
        hd44780_clear(s_lcd);
        s_need_full_redraw = false;
        eyes_changed  = true;
        mouth_changed = true;
        hands_changed = true;

        if (s_display_mode == DISPLAY_MODE_HOT) {
            draw_sweat(true);
        }
    }

    // --- Eye blink with random interval ---
    TickType_t eye_elapsed = (now - s_eye_last_change) * portTICK_PERIOD_MS;
    if (s_eye_state == EYE_STATE_OPEN && eye_elapsed >= s_blink_open_ms) {
        s_eye_state = EYE_STATE_CLOSED;
        s_eye_last_change = now;
        eyes_changed = true;
    } else if (s_eye_state == EYE_STATE_CLOSED && eye_elapsed >= s_blink_close_ms) {
        s_eye_state = EYE_STATE_OPEN;
        s_eye_last_change = now;
        eyes_changed = true;

        // Randomize next blink interval (only for modes that use random blink)
        if (s_display_mode == DISPLAY_MODE_NORMAL || s_display_mode == DISPLAY_MODE_HOT) {
            s_blink_open_ms = random_blink_interval();
        }
    }

    // --- Mouth toggle ---
    if (s_is_speaking && s_mouth_toggle_ms > 0) {
        TickType_t mouth_elapsed = (now - s_mouth_last_change) * portTICK_PERIOD_MS;
        if (mouth_elapsed >= s_mouth_toggle_ms) {
            s_mouth_state = (s_mouth_state == MOUTH_STATE_OPEN)
                            ? MOUTH_STATE_CLOSED : MOUTH_STATE_OPEN;
            s_mouth_last_change = now;
            mouth_changed = true;
        }
    } else if (!s_is_speaking) {
        if (s_mouth_state != MOUTH_STATE_OPEN) {
            s_mouth_state = MOUTH_STATE_OPEN;
            mouth_changed = true;
        }
    }

    // --- Hand wave ---
    if (s_is_waving && s_hand_wave_ms > 0) {
        TickType_t hand_elapsed = (now - s_hand_last_change) * portTICK_PERIOD_MS;
        if (hand_elapsed >= s_hand_wave_ms) {
            s_hand_phase = (s_hand_phase == HAND_PHASE_LEFT_UP)
                           ? HAND_PHASE_RIGHT_UP : HAND_PHASE_LEFT_UP;
            s_hand_last_change = now;
            hands_changed = true;
        }
    }

    // --- HOT effects: backlight blink + sweat drip ---
    if (s_display_mode == DISPLAY_MODE_HOT) {
        // Backlight alarm >35°C
        TickType_t bl_elapsed = (now - s_backlight_last) * portTICK_PERIOD_MS;
        if (sensorData.temp > 35.0f && bl_elapsed >= 500) {
            s_backlight_on = !s_backlight_on;
            hd44780_switch_backlight(s_lcd, s_backlight_on);
            s_backlight_last = now;
        } else if (sensorData.temp <= 35.0f && !s_backlight_on) {
            hd44780_switch_backlight(s_lcd, true);
            s_backlight_on = true;
        }

        // Sweat drip: alternate row 0 → row 1 every 500ms
        TickType_t sweat_elapsed = (now - s_sweat_last) * portTICK_PERIOD_MS;
        if (sweat_elapsed >= 500) {
            s_sweat_row = (s_sweat_row == 0) ? 1 : 0;
            s_sweat_last = now;
            draw_sweat(true);
        }
    }

    // --- COLD effect: jitter phase toggle (used by draw functions for shiver) ---
    if (s_display_mode == DISPLAY_MODE_COLD) {
        s_jitter_phase = (s_jitter_phase + 1) % 2;
    }

    // --- Draw changes ---
    if (eyes_changed)  draw_eyes();
    if (mouth_changed) draw_mouth();
    if (hands_changed) draw_hands();

    // --- Always update temperature (has internal dirty check) ---
    draw_temperature();
}

void face_set_speaking(bool speaking)
{
    s_is_speaking = speaking;
    if (speaking) s_mouth_last_change = xTaskGetTickCount();
}

bool face_is_speaking(void) { return s_is_speaking; }

void face_set_waving(bool waving)
{
    s_is_waving = waving;
    if (waving) s_hand_last_change = xTaskGetTickCount();
}

bool face_is_waving(void) { return s_is_waving; }
