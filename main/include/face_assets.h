/**
 * @file face_assets.h
 * @brief LCD Custom Character (CGRAM) bitmap definitions for face animation.
 *
 * CGRAM Slot Strategy:
 *   Slot 0 — Eye Primary (dynamic per mode)
 *   Slot 1 — Eye Blink / also reused as mouth_closed line
 *   Slot 2 — Mouth Left edge (dynamic per mode)
 *   Slot 3 — Mouth Center (dynamic per mode)
 *   Slot 4 — Hand Up
 *   Slot 5 — Hand Down
 *   Slot 6 — Sweat Drop (HOT mode)
 *   Slot 7 — Mouth Right edge (dynamic per mode)
 */

#pragma once

#include <stdint.h>
#include <hd44780.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- CGRAM Slot IDs ---
#define CGRAM_EYE_PRIMARY    0
#define CGRAM_EYE_BLINK      1   // Also reused as mouth_closed (horizontal line)
#define CGRAM_MOUTH_LEFT     2   // Dynamic per mode
#define CGRAM_MOUTH_CENTER   3   // Dynamic per mode
#define CGRAM_HAND_UP        4
#define CGRAM_HAND_DOWN      5
#define CGRAM_SWEAT          6
#define CGRAM_MOUTH_RIGHT    7   // Dynamic per mode

// --- Eye Bitmaps ---
extern const uint8_t face_eye_open[8];
extern const uint8_t face_eye_closed[8];
extern const uint8_t face_eye_cold[8];
extern const uint8_t face_eye_error[8];

// --- Mouth Bitmaps (3-char wide: left + center + right) ---
// Smile (NORMAL)
extern const uint8_t face_smile_left[8];
extern const uint8_t face_smile_center[8];
extern const uint8_t face_smile_right[8];
// Open (HOT panting / COLD chattering)
extern const uint8_t face_mouth_open_left[8];
extern const uint8_t face_mouth_open_center[8];
extern const uint8_t face_mouth_open_right[8];

// --- Hand Bitmaps ---
extern const uint8_t face_hand_up[8];
extern const uint8_t face_hand_down[8];

// --- Effect Bitmaps ---
extern const uint8_t face_sweat_drop[8];

/**
 * @brief Upload base characters that never change (slots 1, 4, 5).
 */
esp_err_t face_assets_upload_base(const hd44780_t *lcd);

/**
 * @brief Upload mode-specific characters (slots 0, 2, 3, 6, 7).
 */
esp_err_t face_assets_upload_for_mode(const hd44780_t *lcd, int mode);

#ifdef __cplusplus
}
#endif
