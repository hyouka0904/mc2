#pragma once

// Minimal replacement for the Windows registry used by gos_LoadDataFromRegistry
// and gos_SaveDataToRegistry.
//
// Values are opaque byte blobs stored one per line as "<key>=<hex bytes>" in a
// plain text file, so the file stays readable and diff-able. Keys may not
// contain '=' or line breaks. The parsing and formatting functions are pure so
// they can be unit-tested without touching the file system.

#include <map>
#include <string>
#include <vector>
#include <stdint.h>

namespace kv_registry {

typedef std::vector<uint8_t> Value;
typedef std::map<std::string, Value> Store;

// Parses file contents. Malformed lines are skipped.
Store parse(const std::string& text);

// Serializes a store; parse(format(s)) == s for every valid key.
std::string format(const Store& store);

// Returns true if `key` can be stored (non-empty, no '=', '\n' or '\r').
bool isValidKey(const std::string& key);

// Copies the value for `key` into `data`. On input *size is the capacity of
// `data`; on output it is the number of bytes copied, or 0 if the key is
// missing or does not fit (the same contract as gos_LoadDataFromRegistry).
void load(const Store& store, const std::string& key, void* data, uint32_t* size);

// File helpers. readFile() returns an empty store if the file does not exist.
Store readFile(const std::string& path);
bool writeFile(const std::string& path, const Store& store);

} // namespace kv_registry
