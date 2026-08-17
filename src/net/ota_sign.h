#pragma once
#include <Arduino.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool otaVerifySignature(const uint8_t* hash, const uint8_t* sig);
bool otaVerifyAndCommit();
