#ifndef GEMINI_USAGE_H_
#define GEMINI_USAGE_H_

#include "cJSON.h"

// Private to gemini_service: records the usageMetadata block of a generateContent response
// into the persistent token-usage counters exposed via gemini_service::GetUsageStats().
namespace gemini_service::usage {

void RecordFromResponse(const cJSON* root);

}  // namespace gemini_service::usage

#endif  // GEMINI_USAGE_H_
