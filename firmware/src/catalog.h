#pragma once

#include <Arduino.h>

#include <vector>

namespace inky_bird_frame {

struct CatalogEntry {
  uint32_t taxonId = 0;
  String commonName;
  String scientificName;
  String slug;
  String portraitPath;
  String portraitSha256;
  String displayPath;
  String displaySha256;
  String approvedAt;
  uint32_t observationCount = 1;
  String latestDetectionAt;
};

class CatalogClient {
 public:
  explicit CatalogClient(String controllerUrl);

  bool fetch(std::vector<CatalogEntry>& entries, String& errorMessage) const;

 private:
  String controllerUrl_;
};

}  // namespace inky_bird_frame
