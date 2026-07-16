#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <optional>
#include <vector>

#include <EInkDisplay.h>

#include "catalog.h"

namespace inky_bird_frame {

struct DisplayCycleResult {
  bool updated = false;
  uint32_t taxonId = 0;
  String commonName;
  String selectionReason;
  String sha256;
};

class DisplayCycleRunner {
 public:
  explicit DisplayCycleRunner(EInkDisplay& display);

  bool run(const std::vector<CatalogEntry>& entries, bool force, DisplayCycleResult& result,
           String& errorMessage);

 private:
  struct DisplayState {
    uint32_t nextIndex = 0;
    String lastSha256;
    std::optional<uint32_t> lastTaxonId;
    std::vector<uint32_t> shuffleBagRemaining;
    std::vector<uint32_t> shuffleBagSeen;
    String lastPrioritizedDetectionAt;
    std::vector<uint32_t> prioritizedDetectionTaxa;
  };

  EInkDisplay& display_;
  Preferences preferences_;

  DisplayState loadState();
  void saveState(const DisplayState& state) const;
  CatalogEntry selectEntry(const std::vector<CatalogEntry>& entries, DisplayState& state,
                           String& selectionReason);
  std::optional<CatalogEntry> selectPrioritizedEntry(const std::vector<CatalogEntry>& entries,
                                                     const DisplayState& state) const;
};

}  // namespace inky_bird_frame
