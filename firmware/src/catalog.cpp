#include "catalog.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>

#include "config.h"

namespace inky_bird_frame {
namespace {

constexpr size_t MAX_CATALOG_JSON_SIZE = 65536;

bool isNonEmptyString(const JsonVariantConst& value) {
  const char* text = value.as<const char*>();
  return text != nullptr && text[0] != '\0';
}

bool containsTaxonId(const std::vector<CatalogEntry>& entries, uint32_t taxonId) {
  for (const auto& entry : entries) {
    if (entry.taxonId == taxonId) {
      return true;
    }
  }
  return false;
}

}  // namespace

CatalogClient::CatalogClient(String controllerUrl) : controllerUrl_(std::move(controllerUrl)) {
  controllerUrl_.trim();
  while (controllerUrl_.endsWith("/")) {
    controllerUrl_.remove(controllerUrl_.length() - 1);
  }
}

bool CatalogClient::fetch(std::vector<CatalogEntry>& entries, String& errorMessage) const {
  errorMessage = "";
  entries.clear();

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(CATALOG_TIMEOUT_MS);
  if (!http.begin(client, controllerUrl_ + "/v1/catalog")) {
    errorMessage = "Could not prepare catalog request";
    return false;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    errorMessage = "Catalog request failed with HTTP status " + String(status);
    http.end();
    return false;
  }

  const String payload = http.getString();
  http.end();

  DynamicJsonDocument document(MAX_CATALOG_JSON_SIZE);
  const DeserializationError jsonError = deserializeJson(document, payload);
  if (jsonError) {
    errorMessage = "Controller returned invalid catalog JSON";
    return false;
  }

  if (!document.is<JsonObjectConst>() || document["schema_version"].as<int>() != 1) {
    errorMessage = "Controller returned an unsupported catalog";
    return false;
  }

  const JsonArrayConst species = document["species"].as<JsonArrayConst>();
  if (species.isNull()) {
    errorMessage = "Controller catalog has no species list";
    return false;
  }

  entries.reserve(species.size());
  for (const JsonObjectConst raw : species) {
    if (raw.isNull()) {
      errorMessage = "Controller catalog entry must be an object";
      entries.clear();
      return false;
    }

    const JsonVariantConst taxonVariant = raw["taxon_id"];
    if (!taxonVariant.is<uint32_t>()) {
      errorMessage = "Controller catalog entry has invalid fields";
      entries.clear();
      return false;
    }

    const uint32_t taxonId = taxonVariant.as<uint32_t>();
    if (taxonId == 0 || containsTaxonId(entries, taxonId)) {
      errorMessage = taxonId == 0 ? "Controller catalog entry has invalid fields"
                                  : "Controller catalog has duplicate taxon ID";
      entries.clear();
      return false;
    }

    constexpr const char* stringFields[] = {
        "common_name", "scientific_name", "slug", "portrait_path", "portrait_sha256",
        "display_path", "display_sha256", "approved_at",
    };
    for (const char* field : stringFields) {
      if (!isNonEmptyString(raw[field])) {
        errorMessage = "Controller catalog entry has invalid fields";
        entries.clear();
        return false;
      }
    }

    const JsonVariantConst observationVariant = raw["observation_count"];
    const uint32_t observationCount = observationVariant.isNull() ? 1 : observationVariant.as<uint32_t>();
    if (!observationVariant.isNull() && !observationVariant.is<uint32_t>()) {
      errorMessage = "Controller catalog entry has invalid observation count";
      entries.clear();
      return false;
    }

    const JsonVariantConst latestDetectionVariant = raw["latest_detection_at"];
    if (!latestDetectionVariant.isNull() && !isNonEmptyString(latestDetectionVariant)) {
      errorMessage = "Invalid latest detection timestamp in controller catalog";
      entries.clear();
      return false;
    }

    CatalogEntry entry;
    entry.taxonId = taxonId;
    entry.commonName = raw["common_name"].as<const char*>();
    entry.scientificName = raw["scientific_name"].as<const char*>();
    entry.slug = raw["slug"].as<const char*>();
    entry.portraitPath = raw["portrait_path"].as<const char*>();
    entry.portraitSha256 = raw["portrait_sha256"].as<const char*>();
    entry.displayPath = raw["display_path"].as<const char*>();
    entry.displaySha256 = raw["display_sha256"].as<const char*>();
    entry.approvedAt = raw["approved_at"].as<const char*>();
    entry.observationCount = observationCount;
    if (!latestDetectionVariant.isNull()) {
      entry.latestDetectionAt = latestDetectionVariant.as<const char*>();
    }
    entries.push_back(entry);
  }

  if (entries.empty()) {
    errorMessage = "Controller catalog has no approved species";
    return false;
  }
  return true;
}

}  // namespace inky_bird_frame
