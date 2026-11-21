#include "../include/photo_codec.h"
#include <assert.h>
#include <string>
int main() {
  std::string body(photo::kBytes * 2, '0');
  assert(photo::validHex(body.c_str(), body.size()));
  assert(!photo::validHex(body.c_str(), body.size() - 1));
  body.back() = 'x';
  assert(!photo::validHex(body.c_str(), body.size()));
  assert(photo::decodeByte("80") == 128);  // leftmost pixel black
  assert(photo::decodeByte("01") == 1);    // eighth pixel black
  assert(photo::decodeByte("fF") == 255);
  assert(photo::decodeByte("00") == 0);
}
