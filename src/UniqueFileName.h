#pragma once

#include <optional>
#include <string>
#include <vector>

// Finds "<prefix>_NNNN" (NNNN = 0000-9999, zero-padded) for the smallest
// NNNN such that no file "<prefix>_NNNN<ext>" exists on disk, for any ext
// in `extensions`. Passing multiple extensions together keeps a set of
// sibling output files (e.g. this module's tag stream, and eventually a
// BH-format .spc/.sdt/.json triplet) sharing one index instead of drifting
// independently. Returns std::nullopt if no free index was found in
// [0, 10000).
//
// Not race-free against a second concurrent caller.
std::optional<std::string>
UniqueFileName(std::string const &prefix,
               std::vector<std::string> const &extensions);
