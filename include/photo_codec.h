#pragma once
#include <stddef.h>
#include <stdint.h>

namespace photo {
constexpr size_t kBytes = 400 * 300 / 8;
inline int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
inline bool validHex(const char* text, size_t length) {
  if (length != kBytes * 2) return false;
  for (size_t i = 0; i < length; ++i) if (nibble(text[i]) < 0) return false;
  return true;
}
inline uint8_t decodeByte(const char* text) {
  return static_cast<uint8_t>((nibble(text[0]) << 4) | nibble(text[1]));
}
}  // namespace photo
