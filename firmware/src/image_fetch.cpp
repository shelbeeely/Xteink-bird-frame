#include "image_fetch.h"

#include <HTTPClient.h>
#include <WiFiClient.h>
#include <lodepng.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <vector>

#include "config.h"

namespace inky_bird_frame {
namespace {

String percentEncodePath(const String& path) {
  String encoded;
  encoded.reserve(path.length() * 3);
  constexpr char hex[] = "0123456789ABCDEF";
  for (size_t index = 0; index < path.length(); ++index) {
    const char character = path[index];
    const bool safe = (character >= 'A' && character <= 'Z') ||
                      (character >= 'a' && character <= 'z') ||
                      (character >= '0' && character <= '9') || character == '-' ||
                      character == '_' || character == '.' || character == '~' ||
                      character == '/';
    if (safe) {
      encoded += character;
      continue;
    }
    encoded += '%';
    encoded += hex[(static_cast<uint8_t>(character) >> 4) & 0x0F];
    encoded += hex[static_cast<uint8_t>(character) & 0x0F];
  }
  return encoded;
}

String toHex(const uint8_t* digest, size_t length) {
  String encoded;
  encoded.reserve(length * 2);
  constexpr char hex[] = "0123456789abcdef";
  for (size_t index = 0; index < length; ++index) {
    encoded += hex[(digest[index] >> 4) & 0x0F];
    encoded += hex[digest[index] & 0x0F];
  }
  return encoded;
}

uint8_t bayerThreshold(uint16_t x, uint16_t y) {
  static constexpr uint8_t matrix[4][4] = {
      {0, 8, 2, 10},
      {12, 4, 14, 6},
      {3, 11, 1, 9},
      {15, 7, 13, 5},
  };
  return matrix[y % 4][x % 4];
}

void renderToFramebuffer(EInkDisplay& display, const std::vector<unsigned char>& rgba,
                         unsigned sourceWidth, unsigned sourceHeight) {
  const uint16_t targetWidth = display.getDisplayWidth();
  const uint16_t targetHeight = display.getDisplayHeight();
  const uint16_t targetWidthBytes = display.getDisplayWidthBytes();
  auto* framebuffer = display.getFrameBuffer();
  memset(framebuffer, 0xFF, display.getBufferSize());

  const double scale = std::max(static_cast<double>(targetWidth) / sourceWidth,
                                static_cast<double>(targetHeight) / sourceHeight);
  const double scaledWidth = sourceWidth * scale;
  const double scaledHeight = sourceHeight * scale;
  const double offsetX = (scaledWidth - targetWidth) / 2.0;
  const double offsetY = (scaledHeight - targetHeight) / 2.0;

  for (uint16_t y = 0; y < targetHeight; ++y) {
    const unsigned sourceY = std::min(
        sourceHeight - 1,
        static_cast<unsigned>(std::max(0.0, (static_cast<double>(y) + offsetY) / scale)));
    for (uint16_t x = 0; x < targetWidth; ++x) {
      const unsigned sourceX = std::min(
          sourceWidth - 1,
          static_cast<unsigned>(std::max(0.0, (static_cast<double>(x) + offsetX) / scale)));
      const size_t pixelIndex = (static_cast<size_t>(sourceY) * sourceWidth + sourceX) * 4;
      const uint8_t alpha = rgba[pixelIndex + 3];
      uint8_t luminance = 255;
      if (alpha >= 128) {
        const uint8_t red = rgba[pixelIndex + 0];
        const uint8_t green = rgba[pixelIndex + 1];
        const uint8_t blue = rgba[pixelIndex + 2];
        luminance = static_cast<uint8_t>((red * 30 + green * 59 + blue * 11) / 100);
      }
      const uint8_t threshold = static_cast<uint8_t>(96 + bayerThreshold(x, y) * 8);
      const bool white = luminance >= threshold;
      const size_t byteIndex = static_cast<size_t>(y) * targetWidthBytes + (x / 8);
      const uint8_t mask = static_cast<uint8_t>(0x80U >> (x % 8));
      if (white) {
        framebuffer[byteIndex] |= mask;
      } else {
        framebuffer[byteIndex] &= static_cast<uint8_t>(~mask);
      }
    }
  }
}

bool fetchPngBytes(const String& displayPath, std::vector<uint8_t>& pngBytes, String& actualSha256,
                   String& errorMessage) {
  errorMessage = "";
  pngBytes.clear();

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(ASSET_TIMEOUT_MS);
  const String assetUrl = String(CONTROLLER_URL) + "/v1/assets/" + percentEncodePath(displayPath);
  if (!http.begin(client, assetUrl)) {
    errorMessage = "Could not prepare asset request";
    return false;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    errorMessage = "Asset request failed with HTTP status " + String(status);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  const int length = http.getSize();
  if (length > 0) {
    pngBytes.reserve(static_cast<size_t>(length));
  }

  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  mbedtls_sha256_starts_ret(&shaContext, 0);

  uint8_t buffer[1024];
  int remaining = length;
  while (http.connected() && (remaining > 0 || remaining == -1)) {
    const size_t available = stream->available();
    if (available == 0) {
      delay(1);
      continue;
    }

    const size_t chunkSize = std::min(available, sizeof(buffer));
    const size_t readCount = stream->readBytes(buffer, chunkSize);
    if (readCount == 0) {
      continue;
    }
    pngBytes.insert(pngBytes.end(), buffer, buffer + readCount);
    mbedtls_sha256_update_ret(&shaContext, buffer, readCount);
    if (remaining > 0) {
      remaining -= static_cast<int>(readCount);
    }
  }

  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&shaContext, digest);
  mbedtls_sha256_free(&shaContext);
  http.end();

  actualSha256 = toHex(digest, sizeof(digest));
  return true;
}

}  // namespace

bool downloadAndRenderImage(EInkDisplay& display, const String& displayPath,
                            const String& expectedSha256, String& actualSha256,
                            String& errorMessage) {
  std::vector<uint8_t> pngBytes;
  if (!fetchPngBytes(displayPath, pngBytes, actualSha256, errorMessage)) {
    return false;
  }
  if (actualSha256 != expectedSha256) {
    errorMessage = "Downloaded asset checksum mismatch";
    return false;
  }

  std::vector<unsigned char> rgba;
  unsigned width = 0;
  unsigned height = 0;
  const unsigned decodeError = lodepng::decode(rgba, width, height, pngBytes);
  if (decodeError != 0) {
    errorMessage = String("Could not decode PNG: ") + lodepng_error_text(decodeError);
    return false;
  }

  renderToFramebuffer(display, rgba, width, height);
  display.displayBuffer(EInkDisplay::FAST_REFRESH, true);
  return true;
}

}  // namespace inky_bird_frame
