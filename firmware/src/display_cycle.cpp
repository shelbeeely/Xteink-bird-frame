#include "display_cycle.h"

#include <Preferences.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdlib>
#include <random>

#include "config.h"
#include "image_fetch.h"

namespace inky_bird_frame {
namespace {

String joinTaxonIds(const std::vector<uint32_t>& values) {
  String joined;
  for (size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      joined += ',';
    }
    joined += String(values[index]);
  }
  return joined;
}

std::vector<uint32_t> parseTaxonIds(const String& raw) {
  std::vector<uint32_t> values;
  if (raw.isEmpty()) {
    return values;
  }

  int start = 0;
  while (start < raw.length()) {
    const int comma = raw.indexOf(',', start);
    const String piece = raw.substring(start, comma == -1 ? raw.length() : comma);
    bool digitsOnly = !piece.isEmpty();
    for (size_t index = 0; index < piece.length(); ++index) {
      if (!isDigit(piece[index])) {
        digitsOnly = false;
        break;
      }
    }
    if (digitsOnly) {
      const uint32_t value = static_cast<uint32_t>(strtoul(piece.c_str(), nullptr, 10));
      values.push_back(value);
    }
    if (comma == -1) {
      break;
    }
    start = comma + 1;
  }
  return values;
}

bool containsTaxonId(const std::vector<uint32_t>& values, uint32_t taxonId) {
  return std::find(values.begin(), values.end(), taxonId) != values.end();
}

std::vector<uint32_t> activeTaxa(const std::vector<CatalogEntry>& entries) {
  std::vector<uint32_t> values;
  values.reserve(entries.size());
  for (const auto& entry : entries) {
    values.push_back(entry.taxonId);
  }
  return values;
}

std::vector<uint32_t> filterActiveTaxa(const std::vector<uint32_t>& values,
                                       const std::vector<CatalogEntry>& entries) {
  std::vector<uint32_t> filtered;
  for (const uint32_t taxonId : values) {
    for (const auto& entry : entries) {
      if (entry.taxonId == taxonId) {
        filtered.push_back(taxonId);
        break;
      }
    }
  }
  return filtered;
}

const CatalogEntry* entryByTaxonId(const std::vector<CatalogEntry>& entries, uint32_t taxonId) {
  for (const auto& entry : entries) {
    if (entry.taxonId == taxonId) {
      return &entry;
    }
  }
  return nullptr;
}

void shuffleWithoutImmediateRepeat(std::vector<uint32_t>& values,
                                   const std::optional<uint32_t>& lastTaxonId) {
  std::mt19937 generator(static_cast<uint32_t>(esp_random()));
  std::shuffle(values.begin(), values.end(), generator);
  if (values.size() > 1 && lastTaxonId.has_value() && values.front() == *lastTaxonId) {
    std::swap(values[0], values[1]);
  }
}

bool isLaterIsoTimestamp(const String& left, const String& right) {
  return strcmp(left.c_str(), right.c_str()) > 0;
}

bool isSameIsoTimestamp(const String& left, const String& right) {
  return strcmp(left.c_str(), right.c_str()) == 0;
}

}  // namespace

DisplayCycleRunner::DisplayCycleRunner(EInkDisplay& display) : display_(display) {}

DisplayCycleRunner::DisplayState DisplayCycleRunner::loadState() {
  DisplayState state;
  preferences_.begin("display-cycle", true);
  state.nextIndex = preferences_.getUInt("next_index", 0);
  state.lastSha256 = preferences_.getString("last_sha256", "");
  const bool hasLastTaxon = preferences_.getBool("has_last_taxon", false);
  if (hasLastTaxon) {
    state.lastTaxonId = preferences_.getUInt("last_taxon_id", 0);
  }
  state.shuffleBagRemaining = parseTaxonIds(preferences_.getString("shuffle_remaining", ""));
  state.shuffleBagSeen = parseTaxonIds(preferences_.getString("shuffle_seen", ""));
  state.lastPrioritizedDetectionAt = preferences_.getString("priority_ts", "");
  state.prioritizedDetectionTaxa = parseTaxonIds(preferences_.getString("priority_taxa", ""));
  preferences_.end();
  return state;
}

void DisplayCycleRunner::saveState(const DisplayState& state) const {
  Preferences preferences;
  preferences.begin("display-cycle", false);
  preferences.putUInt("next_index", state.nextIndex);
  preferences.putString("last_sha256", state.lastSha256);
  preferences.putBool("has_last_taxon", state.lastTaxonId.has_value());
  if (state.lastTaxonId.has_value()) {
    preferences.putUInt("last_taxon_id", *state.lastTaxonId);
  }
  preferences.putString("shuffle_remaining", joinTaxonIds(state.shuffleBagRemaining));
  preferences.putString("shuffle_seen", joinTaxonIds(state.shuffleBagSeen));
  preferences.putString("priority_ts", state.lastPrioritizedDetectionAt);
  preferences.putString("priority_taxa", joinTaxonIds(state.prioritizedDetectionTaxa));
  preferences.end();
}

std::optional<CatalogEntry> DisplayCycleRunner::selectPrioritizedEntry(
    const std::vector<CatalogEntry>& entries, const DisplayState& state) const {
  if (!PRIORITIZE_LATEST_DETECTION) {
    return std::nullopt;
  }

  std::optional<CatalogEntry> selected;
  for (const auto& entry : entries) {
    if (entry.latestDetectionAt.isEmpty()) {
      continue;
    }
    const bool alreadyConsumed = !state.lastPrioritizedDetectionAt.isEmpty() &&
                                 isSameIsoTimestamp(entry.latestDetectionAt,
                                                    state.lastPrioritizedDetectionAt) &&
                                 containsTaxonId(state.prioritizedDetectionTaxa, entry.taxonId);
    const bool unseen = state.lastPrioritizedDetectionAt.isEmpty() ||
                        isLaterIsoTimestamp(entry.latestDetectionAt,
                                            state.lastPrioritizedDetectionAt) ||
                        (!alreadyConsumed &&
                         isSameIsoTimestamp(entry.latestDetectionAt,
                                            state.lastPrioritizedDetectionAt));
    if (!unseen) {
      continue;
    }
    if (!selected.has_value() ||
        isLaterIsoTimestamp(entry.latestDetectionAt, selected->latestDetectionAt) ||
        (isSameIsoTimestamp(entry.latestDetectionAt, selected->latestDetectionAt) &&
         entry.taxonId < selected->taxonId)) {
      selected = entry;
    }
  }
  return selected;
}

CatalogEntry DisplayCycleRunner::selectEntry(const std::vector<CatalogEntry>& entries,
                                             DisplayState& state, String& selectionReason) {
  auto prioritized = selectPrioritizedEntry(entries, state);
  if (prioritized.has_value()) {
    selectionReason = "latest_detection";
    const auto selected = *prioritized;
    if (isSameIsoTimestamp(selected.latestDetectionAt, state.lastPrioritizedDetectionAt)) {
      if (!containsTaxonId(state.prioritizedDetectionTaxa, selected.taxonId)) {
        state.prioritizedDetectionTaxa.push_back(selected.taxonId);
      }
    } else {
      state.lastPrioritizedDetectionAt = selected.latestDetectionAt;
      state.prioritizedDetectionTaxa = {selected.taxonId};
    }

    if (ROTATION_MODE == RotationMode::Sequential) {
      const auto& sequential = entries[state.nextIndex % entries.size()];
      if (sequential.taxonId == selected.taxonId) {
        state.nextIndex = (state.nextIndex + 1) % entries.size();
      }
    } else {
      state.shuffleBagRemaining = filterActiveTaxa(state.shuffleBagRemaining, entries);
      state.shuffleBagSeen = filterActiveTaxa(state.shuffleBagSeen, entries);
      state.shuffleBagRemaining.erase(
          std::remove(state.shuffleBagRemaining.begin(), state.shuffleBagRemaining.end(),
                      selected.taxonId),
          state.shuffleBagRemaining.end());
      if (!containsTaxonId(state.shuffleBagSeen, selected.taxonId)) {
        state.shuffleBagSeen.push_back(selected.taxonId);
      }
    }
    return selected;
  }

  selectionReason = "rotation";
  if (ROTATION_MODE == RotationMode::Sequential) {
    const auto selected = entries[state.nextIndex % entries.size()];
    state.nextIndex = (state.nextIndex + 1) % entries.size();
    return selected;
  }

  state.shuffleBagRemaining = filterActiveTaxa(state.shuffleBagRemaining, entries);
  state.shuffleBagSeen = filterActiveTaxa(state.shuffleBagSeen, entries);
  bool reshuffled = false;
  const auto active = activeTaxa(entries);
  for (const uint32_t taxonId : active) {
    if (!containsTaxonId(state.shuffleBagRemaining, taxonId) &&
        !containsTaxonId(state.shuffleBagSeen, taxonId)) {
      state.shuffleBagRemaining.push_back(taxonId);
      reshuffled = true;
    }
  }
  if (state.shuffleBagRemaining.empty()) {
    state.shuffleBagRemaining = active;
    state.shuffleBagSeen.clear();
    reshuffled = true;
  }
  if (reshuffled) {
    shuffleWithoutImmediateRepeat(state.shuffleBagRemaining, state.lastTaxonId);
  }
  const uint32_t selectedTaxonId = state.shuffleBagRemaining.front();
  state.shuffleBagRemaining.erase(state.shuffleBagRemaining.begin());
  state.shuffleBagSeen.push_back(selectedTaxonId);
  const CatalogEntry* selected = entryByTaxonId(entries, selectedTaxonId);
  return *selected;
}

bool DisplayCycleRunner::run(const std::vector<CatalogEntry>& entries, const bool force,
                             DisplayCycleResult& result, String& errorMessage) {
  errorMessage = "";
  result = {};
  if (entries.empty()) {
    errorMessage = "Catalog entries must not be empty";
    return false;
  }

  DisplayState state = loadState();
  String selectionReason;
  const CatalogEntry selected = selectEntry(entries, state, selectionReason);

  result.taxonId = selected.taxonId;
  result.commonName = selected.commonName;
  result.selectionReason = selectionReason;

  if (!force && selected.displaySha256 == state.lastSha256) {
    state.lastTaxonId = selected.taxonId;
    saveState(state);
    return true;
  }

  String downloadedSha256;
  if (!downloadAndRenderImage(display_, selected.displayPath, selected.displaySha256,
                              downloadedSha256, errorMessage)) {
    return false;
  }

  state.lastSha256 = downloadedSha256;
  state.lastTaxonId = selected.taxonId;
  saveState(state);

  result.updated = true;
  result.sha256 = downloadedSha256;
  return true;
}

}  // namespace inky_bird_frame
