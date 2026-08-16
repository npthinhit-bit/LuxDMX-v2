#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <Arduino.h>

bool otaVerifySignature(const uint8_t* hash, const uint8_t* sig);
bool otaVerifyAndCommit();
