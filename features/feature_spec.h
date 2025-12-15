// feature_spec.h (fix)
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace feat {

inline std::string json_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:   out += c;      break;
    }
  }
  return out;
}

inline uint64_t fnv1a64(std::string_view data) {
  constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL; // correct
  constexpr uint64_t FNV_PRIME  = 1099511628211ULL;
  uint64_t h = FNV_OFFSET;
  for (unsigned char c : data) {
    h ^= static_cast<uint64_t>(c);
    h *= FNV_PRIME;
  }
  return h;
}

inline std::string to_hex_u64(uint64_t value) {
  std::ostringstream ss;
  ss << std::hex << std::setfill('0') << std::setw(16) << value;
  return ss.str();
}

struct FeatureSpec {
  std::string name;
  std::string family;
  int version = 1;
  std::string inputs;
  std::string update_mode;
  std::string dtype;
  std::string unit;
  int warmup_events = 1;
  int64_t warmup_ns = 0;
  std::string description;

  // canonical name: params
  std::vector<std::pair<std::string, std::string>> params;

  void canonicalize() {
    std::sort(params.begin(), params.end(),
              [](auto& a, auto& b) { return a.first < b.first; });
  }

  std::string canonical_json() const {
    std::ostringstream ss;
    ss << "{";
    ss << "\"name\":\"" << json_escape(name) << "\",";
    ss << "\"family\":\"" << json_escape(family) << "\",";
    ss << "\"version\":" << version << ",";
    ss << "\"inputs\":\"" << json_escape(inputs) << "\",";
    ss << "\"update_mode\":\"" << json_escape(update_mode) << "\",";
    ss << "\"dtype\":\"" << json_escape(dtype) << "\",";
    ss << "\"unit\":\"" << json_escape(unit) << "\",";
    ss << "\"warmup_events\":" << warmup_events << ",";
    ss << "\"warmup_ns\":" << warmup_ns << ",";
    ss << "\"description\":\"" << json_escape(description) << "\",";
    ss << "\"params\":[";
    for (size_t i = 0; i < params.size(); ++i) {
      ss << "{\"name\":\"" << json_escape(params[i].first) << "\","
         << "\"value\":\"" << json_escape(params[i].second) << "\"}";
      if (i + 1 < params.size()) ss << ",";
    }
    ss << "]}";
    return ss.str();
  }

  std::string spec_id() const {
    const std::string j = canonical_json();
    return to_hex_u64(fnv1a64(j));
  }
};

} // namespace feat
