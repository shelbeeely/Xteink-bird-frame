#pragma once

#include <Arduino.h>

#include <EInkDisplay.h>

namespace inky_bird_frame {

bool downloadAndRenderImage(EInkDisplay& display, const String& displayPath,
                            const String& expectedSha256, String& actualSha256,
                            String& errorMessage);

}  // namespace inky_bird_frame
