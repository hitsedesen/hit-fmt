#pragma once

#include <cstddef>
#include <cstdint>

namespace formatter {

// Member declaration/definition counts can inherit their enclosing policy.
// Keep counts wide until validating the 0..10000 range before narrowing.
enum class BlankLineMode : std::uint8_t
{
    Preserve,
    Inherit,
    Exact
};

struct BlankLineSetting
{
    BlankLineMode Mode = BlankLineMode::Preserve;
    std::size_t EmptyLines = 0;

    [[nodiscard]] static constexpr BlankLineSetting Inherited() noexcept
    {
        return {BlankLineMode::Inherit, 0};
    }

    bool operator==(const BlankLineSetting& other) const = default;
};

// Concrete vertical counts measure empty lines, not newline characters. An
// Exact positive count requires an applicable boundary break; Exact zero only
// removes surplus empty lines, retaining its selected inline/broken status.
enum class VerticalGapMode : std::uint8_t
{
    Preserve,
    Exact
};

struct VerticalGapPolicy
{
    VerticalGapMode Mode = VerticalGapMode::Preserve;
    std::uint16_t EmptyLines = 0;
};

struct EmptyLineCap
{
    bool Enabled = false;
    std::uint16_t EmptyLines = 0;
};

struct VerticalGapPolicies
{
    VerticalGapPolicy NamespaceBlankLinesAfterOpen;
    VerticalGapPolicy NamespaceBlankLinesBeforeClose;
    VerticalGapPolicy AccessSectionBlankLinesBefore;
    VerticalGapPolicy AccessSectionBlankLinesAfterLabel;
    VerticalGapPolicy DeclarationBlankLines;
    VerticalGapPolicy MemberDeclarationBlankLines;
    VerticalGapPolicy DefinitionBlankLines;
    VerticalGapPolicy MemberDefinitionBlankLines;
    EmptyLineCap MaxEmptyLines;
    VerticalGapPolicy StandaloneCommentBlankLinesBefore;
    VerticalGapPolicy StandaloneCommentBlankLinesAfter;
};

} // namespace formatter
