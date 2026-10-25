#pragma once

#include "fake_storage.h"
#include "freertos/FreeRTOS.h"

using SemaphoreHandle_t = fake::Mutex*;
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  return fake::failCreate ? nullptr : &fake::mutex;
}
inline int xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t) {
  ++mutex->takes;
  if (mutex->failTake || mutex->locked) return pdFALSE;
  mutex->locked = true;
  return pdTRUE;
}
inline int xSemaphoreGive(SemaphoreHandle_t mutex) {
  if (!mutex->locked) return pdFALSE;
  ++mutex->gives;
  mutex->locked = false;
  return pdTRUE;
}
