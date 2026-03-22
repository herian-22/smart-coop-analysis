#ifndef SYNTHETIC_INFERENCE_H
#define SYNTHETIC_INFERENCE_H

#include <stdint.h>

/**
 * @brief Initialize the TFLite model and interpreter.
 * 
 * @return true if initialization is successful, false otherwise.
 */
bool init_autoencoder();

/**
 * @brief Run inference on a window of sensor data.
 * 
 * @param input_window A 2D array of raw readings [WINDOW_SIZE][NUM_FEATURES].
 * @return The reconstruction error (MSE). Returns -1.0 if inference fails.
 */
float run_autoencoder_inference(float input_window[30][2]);

#endif // SYNTHETIC_INFERENCE_H
