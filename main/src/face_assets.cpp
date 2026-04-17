/**
 * @file face_assets.cpp
 * @brief Pixel art bitmaps and CGRAM management for context-aware face.
 *
 * Mouth is 3 characters wide: [LEFT][CENTER][RIGHT]
 * Each mode has its own set of mouth bitmaps uploaded to CGRAM slots 2, 3, 7.
 */

#include "face_assets.h"
#include "app_config.h"
#include "esp_log.h"

static const char *TAG = "FaceAssets";

// ============================================================================
// EYE BITMAPS
// ============================================================================

// Eye Open — Saluting Emoji style (Vertical Oval) [NORMAL / HOT]
//   .....  .XXX.  .XXX.  .XXX.  .XXX.  .....  .....  .....
const uint8_t face_eye_open[8] = {
    0x00, 0x0E, 0x0E, 0x0E, 0x0E, 0x00, 0x00, 0x00
};

// Eye Closed — horizontal line (blink) [ALL MODES]
//   .....  .....  .....  XXXXX  .....  .....  .....  .....
const uint8_t face_eye_closed[8] = {
    0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00
};

// Eye Cold — squinting [COLD]
//   .....  XX.XX  .XXX.  .....  XX.XX  .XXX.  .....  .....
const uint8_t face_eye_cold[8] = {
    0x00, 0x1B, 0x0E, 0x00, 0x1B, 0x0E, 0x00, 0x00
};

// Eye Error — X shape (sensor fail / confused)
//   .....  X...X  .X.X.  ..X..  .X.X.  X...X  .....  .....
const uint8_t face_eye_error[8] = {
    0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00, 0x00
};

// ============================================================================
// MOUTH BITMAPS — 3-char wide (15 pixels across)
// ============================================================================

// --- Smile (NORMAL mode) — Flat neutral line (saluting emoji) ---

const uint8_t face_smile_left[8] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
const uint8_t face_smile_center[8] = {
    0x00, 0x00, 0x00, 0x0E, 0x0E, 0x00, 0x00, 0x00
};
const uint8_t face_smile_right[8] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// --- Open (HOT panting / COLD chattering) — FILLED solid oval ---
// Left:            Center:          Right:
//  .....  0x00     .....  0x00      .....  0x00
//  ....X  0x01     XXXXX  0x1F      X....  0x10
//  ...XX  0x03     XXXXX  0x1F      XX...  0x18
//  ...XX  0x03     XXXXX  0x1F      XX...  0x18
//  ....X  0x01     XXXXX  0x1F      X....  0x10
//  .....  0x00     .....  0x00      .....  0x00
//  .....  0x00     .....  0x00      .....  0x00
//  .....  0x00     .....  0x00      .....  0x00

const uint8_t face_mouth_open_left[8] = {
    0x00, 0x01, 0x03, 0x03, 0x01, 0x00, 0x00, 0x00
};
const uint8_t face_mouth_open_center[8] = {
    0x00, 0x1F, 0x1F, 0x1F, 0x1F, 0x00, 0x00, 0x00
};
const uint8_t face_mouth_open_right[8] = {
    0x00, 0x10, 0x18, 0x18, 0x10, 0x00, 0x00, 0x00
};

// NOTE: Mouth CLOSED reuses CGRAM slot 1 (eye_blink = horizontal line)
// Written 3 times at mouth positions for a wide "---" line.

// ============================================================================
// HAND BITMAPS
// ============================================================================

// Hand Up — Saluting Right Hand
const uint8_t face_hand_up[8] = {
    0x00, 0x07, 0x0F, 0x1E, 0x0C, 0x00, 0x00, 0x00
};

// Hand Down — Saluting Right Hand (slightly shifted for animation)
const uint8_t face_hand_down[8] = {
    0x00, 0x03, 0x07, 0x0F, 0x06, 0x00, 0x00, 0x00
};

// ============================================================================
// EFFECT BITMAPS
// ============================================================================

// Sweat Drop [HOT]
//   ..X..  ..X..  .X.X.  .X.X.  X...X  X...X  .XXX.  .....
const uint8_t face_sweat_drop[8] = {
    0x04, 0x04, 0x0A, 0x0A, 0x11, 0x11, 0x0E, 0x00
};

// ============================================================================
// CGRAM Upload Functions
// ============================================================================

esp_err_t face_assets_upload_base(const hd44780_t *lcd)
{
    esp_err_t ret;
    ESP_LOGI(TAG, "Uploading base characters (slots 1, 4, 5)...");

    ret = hd44780_upload_character(lcd, CGRAM_EYE_BLINK, face_eye_closed);
    if (ret != ESP_OK) return ret;

    ret = hd44780_upload_character(lcd, CGRAM_HAND_UP, face_hand_up);
    if (ret != ESP_OK) return ret;

    ret = hd44780_upload_character(lcd, CGRAM_HAND_DOWN, face_hand_down);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Base characters uploaded.");
    return ESP_OK;
}

esp_err_t face_assets_upload_for_mode(const hd44780_t *lcd, int mode)
{
    esp_err_t ret;
    DisplayMode dm = (DisplayMode)mode;

    switch (dm) {
        case DISPLAY_MODE_NORMAL:
            ESP_LOGI(TAG, "CGRAM → NORMAL (smile + open eyes)");
            ret = hd44780_upload_character(lcd, CGRAM_EYE_PRIMARY, face_eye_open);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_MOUTH_LEFT, face_smile_left);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_MOUTH_CENTER, face_smile_center);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_MOUTH_RIGHT, face_smile_right);
            if (ret != ESP_OK) return ret;
            break;

        case DISPLAY_MODE_COLD:
            ESP_LOGI(TAG, "CGRAM → COLD (squint + chatter)");
            ret = hd44780_upload_character(lcd, CGRAM_EYE_PRIMARY, face_eye_cold);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_MOUTH_LEFT, face_mouth_open_left);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_MOUTH_CENTER, face_mouth_open_center);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_MOUTH_RIGHT, face_mouth_open_right);
            if (ret != ESP_OK) return ret;
            break;

        case DISPLAY_MODE_HOT:
            ESP_LOGI(TAG, "CGRAM → HOT (pant + sweat)");
            ret = hd44780_upload_character(lcd, CGRAM_EYE_PRIMARY, face_eye_open);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_MOUTH_LEFT, face_mouth_open_left);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_MOUTH_CENTER, face_mouth_open_center);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_MOUTH_RIGHT, face_mouth_open_right);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_SWEAT, face_sweat_drop);
            if (ret != ESP_OK) return ret;
            break;

        case DISPLAY_MODE_ERROR:
            ESP_LOGI(TAG, "CGRAM → ERROR (X eyes + open mouth)");
            ret = hd44780_upload_character(lcd, CGRAM_EYE_PRIMARY, face_eye_error);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_MOUTH_LEFT, face_mouth_open_left);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_MOUTH_CENTER, face_mouth_open_center);
            if (ret != ESP_OK) return ret;
            ret = hd44780_upload_character(lcd, CGRAM_MOUTH_RIGHT, face_mouth_open_right);
            if (ret != ESP_OK) return ret;
            break;
    }

    return ESP_OK;
}
