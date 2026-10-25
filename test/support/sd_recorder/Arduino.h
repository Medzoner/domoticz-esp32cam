#pragma once

#include "fake_storage.h"

inline uint32_t millis() { return fake::now; }
