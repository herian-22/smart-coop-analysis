#include "synthetic_inference.h"
#include "synthetic_autoencoder.h"
#include "model_params.h"

#include "esp_log.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

static const char* TAG_INF = "Inference";

// TFLite globals
namespace {
  const tflite::Model* model = nullptr;
  tflite::MicroInterpreter* interpreter = nullptr;
  TfLiteTensor* input = nullptr;
  TfLiteTensor* output = nullptr;

  constexpr int kTensorArenaSize = 64 * 1024;
  uint8_t tensor_arena[kTensorArenaSize];

  // Internals
  void preprocess_window(float input_window[30][2]) {
    float input_scale = input->params.scale;
    int input_zero_point = input->params.zero_point;

    for (int i = 0; i < WINDOW_SIZE; i++) {
      for (int j = 0; j < NUM_FEATURES; j++) {
          float normalized_val = (input_window[i][j] - MODEL_MEAN[j]) / MODEL_STD[j];
          int8_t quantized_val = (int8_t)(normalized_val / input_scale + input_zero_point);
          input->data.int8[i * NUM_FEATURES + j] = quantized_val;
      }
    }
  }

  float calculate_mse() {
    float output_scale = output->params.scale;
    int output_zero_point = output->params.zero_point;
    float input_scale = input->params.scale;
    int input_zero_point = input->params.zero_point;

    float mse = 0;
    int num_elements = WINDOW_SIZE * NUM_FEATURES;

    for (int i = 0; i < num_elements; i++) {
      float dequantized_output = (output->data.int8[i] - output_zero_point) * output_scale;
      float dequantized_input = (input->data.int8[i] - input_zero_point) * input_scale;
      float diff = dequantized_input - dequantized_output;
      mse += diff * diff;
    }
    return mse / num_elements;
  }
} // namespace

bool init_autoencoder() {
  tflite::InitializeTarget();
  model = tflite::GetModel(synthetic_autoencoder_tflite);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    ESP_LOGE(TAG_INF, "Model version mismatch!");
    return false;
  }

  // Define operations needed by the Conv1D model
  static tflite::MicroMutableOpResolver<18> resolver;
  resolver.AddConv2D(); 
  resolver.AddMaxPool2D();
  resolver.AddReshape();
  resolver.AddFullyConnected();
  resolver.AddTransposeConv(); 
  resolver.AddRelu();
  resolver.AddExpandDims();
  resolver.AddSqueeze(); 
  resolver.AddConcatenation(); 
  resolver.AddShape();
  resolver.AddStridedSlice();
  resolver.AddPack();
  // Note: Add other ops if training script used them (like AddLeakyRelu if used)

  static tflite::MicroInterpreter static_interpreter(
      model, resolver, tensor_arena, kTensorArenaSize, nullptr);
  interpreter = &static_interpreter;

  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    ESP_LOGE(TAG_INF, "AllocateTensors() failed");
    return false;
  }

  input = interpreter->input(0);
  output = interpreter->output(0);

  return true;
}

float run_autoencoder_inference(float input_window[WINDOW_SIZE][NUM_FEATURES]) {
  if (interpreter == nullptr) {
    ESP_LOGE(TAG_INF, "Interpreter not initialized!");
    return -1.0f;
  }

  // 1. Preprocessing
  preprocess_window(input_window);

  // 2. Run Inference
  if (interpreter->Invoke() != kTfLiteOk) {
    ESP_LOGE(TAG_INF, "Invoke failed");
    return -1.0f;
  }

  // 3. Post-processing (MSE)
  return calculate_mse();
}
