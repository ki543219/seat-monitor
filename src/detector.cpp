#include "detector.h"

#include <Arduino.h>
#include <Chirale_TensorFlowLite.h>
#include <esp_heap_caps.h>

#include "model_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace detector {
namespace {

constexpr size_t kArenaSize = 160 * 1024;
constexpr int kPersonIndex = 1;  // output: [no_person, person]

tflite::MicroInterpreter* interpreter = nullptr;

}  // namespace

bool init() {
  const tflite::Model* model = tflite::GetModel(g_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.printf("model schema %lu != %d\n", model->version(), TFLITE_SCHEMA_VERSION);
    return false;
  }

  // Only the ops this model uses, to keep flash small.
  static tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddAveragePool2D();
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddReshape();
  resolver.AddSoftmax();

  uint8_t* arena = static_cast<uint8_t*>(
      heap_caps_aligned_alloc(16, kArenaSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!arena) {
    Serial.println("tensor arena alloc failed");
    return false;
  }

  static tflite::MicroInterpreter interp(model, resolver, arena, kArenaSize);
  if (interp.AllocateTensors() != kTfLiteOk) {
    Serial.println("AllocateTensors failed");
    return false;
  }
  interpreter = &interp;
  Serial.printf("model ready, arena used %u bytes\n", (unsigned)interp.arena_used_bytes());
  return true;
}

float person_score(const uint8_t* gray96) {
  if (!interpreter) return -1.0f;
  TfLiteTensor* in = interpreter->input(0);
  // Input quantization is scale 1/127.5, zero point -1, i.e. q = pixel - 128.
  for (int i = 0; i < kSize * kSize; i++) {
    in->data.int8[i] = static_cast<int8_t>(static_cast<int>(gray96[i]) - 128);
  }
  if (interpreter->Invoke() != kTfLiteOk) return -1.0f;

  TfLiteTensor* out = interpreter->output(0);
  return (out->data.int8[kPersonIndex] - out->params.zero_point) * out->params.scale;
}

}  // namespace detector
