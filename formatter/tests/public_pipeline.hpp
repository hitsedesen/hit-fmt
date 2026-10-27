#pragma once

#include <formatter/config.hpp>

#include <string_view>

namespace public_pipeline {

// Owning-result invariants shared by the canonical unscoped formatter goldens.
void CheckFormattedResult(std::string_view source, const formatter::Style& style, std::string_view expectedText);

} // namespace public_pipeline
