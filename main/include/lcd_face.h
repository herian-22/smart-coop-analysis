/**
 * @file lcd_face.h
 * @brief LCD Face Animation controller — context-aware, tick-based state machine.
 *
 * Supports three display modes based on temperature:
 *   NORMAL — Happy face (smile), standard blink, wave
 *   COLD   — Squinting eyes, fast blink (shiver), chattering mouth
 *   HOT    — Sweating, panting mouth, backlight blink at extreme temps
 */

#pragma once

#include <stdbool.h>
#include <hd44780.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the face animation engine.
 */
esp_err_t face_anim_init(hd44780_t *lcd, SemaphoreHandle_t i2c_mutex);

/**
 * @brief Draw one animation frame (non-blocking, tick-based).
 * Caller must hold I2C mutex.
 */
void face_anim_draw_frame(void);

/**
 * @brief Set display mode (triggers CGRAM reload + behavior change).
 * Thread-safe. Call from sensor task when temperature zone changes.
 * @param mode 0=NORMAL, 1=COLD, 2=HOT (cast from DisplayMode)
 */
void face_set_display_mode(int mode);

/**
 * @brief Get current display mode.
 */
int face_get_display_mode(void);

/** @brief Set speaking state (mouth animation). */
void face_set_speaking(bool speaking);
bool face_is_speaking(void);

/** @brief Set waving state (hand animation). */
void face_set_waving(bool waving);
bool face_is_waving(void);

#ifdef __cplusplus
}
#endif
