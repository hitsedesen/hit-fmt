#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace formatter {

struct Style;

enum class FirstArgumentAnnotation : std::uint8_t
{
    // Soft preference for the complete literal expression, not its prefix.
    // A required legal owned Always break and safety/protection outrank it.
    PinCompleteStringLiteral
};

enum class ChainParticipation : std::uint8_t
{
    AllSafe,
    AnnotatedRoots
};

enum class VisualScopeLayout : std::uint8_t
{
    PreserveExistingExtraLevel
};

struct MessagePinAnnotation
{
    std::string Name;
    FirstArgumentAnnotation FirstArgument =
        FirstArgumentAnnotation::PinCompleteStringLiteral;
};

struct VisualScopeAnnotation
{
    std::string Begin;
    std::string End;
    VisualScopeLayout Layout = VisualScopeLayout::PreserveExistingExtraLevel;
};

struct ChainCallPairAnnotation
{
    std::string First;
    std::string Second;
};

// Configuration-lifetime metadata for syntax lowering, never for the renderer.
// Owns sorted contiguous records; no views into raw Style. Publish as const and
// reuse across files. Spans/pointers survive raw-Style mutation but not
// moving, replacing or destroying this snapshot. No mutable record access.
class LibraryAnnotations
{
public:
    [[nodiscard]] std::span<const MessagePinAnnotation> MessagePins() const;
    [[nodiscard]] const MessagePinAnnotation* FindMessagePin(
        std::string_view name) const noexcept;
    [[nodiscard]] ChainParticipation Chains() const noexcept;
    [[nodiscard]] bool IsAnnotatedChainRoot(std::string_view name) const
    noexcept;
    [[nodiscard]] std::span<const std::string> ChainRoots() const;
    [[nodiscard]] std::span<const std::string> AlwaysBreakChainRoots() const;
    [[nodiscard]] bool AlwaysBreakChainRoot(std::string_view name) const
    noexcept;
    [[nodiscard]] std::span<const VisualScopeAnnotation> VisualScopes() const;
    [[nodiscard]] std::span<const ChainCallPairAnnotation> ChainScopes() const;
    [[nodiscard]] std::span<const ChainCallPairAnnotation> StickyChainPairs()
    const;
    [[nodiscard]] bool IsStickyChainPair(
        std::string_view first,
        std::string_view second) const noexcept;
    [[nodiscard]] const VisualScopeAnnotation* FindScopeBegin(
        std::string_view name) const noexcept;
    [[nodiscard]] const VisualScopeAnnotation* FindScopeEnd(
        std::string_view name) const noexcept;

private:
    std::vector<MessagePinAnnotation> MessagePins_;
    std::vector<std::string> ChainRoots_;
    std::vector<std::string> AlwaysBreakChainRoots_;
    std::vector<VisualScopeAnnotation> VisualScopes_;
    std::vector<ChainCallPairAnnotation> ChainScopes_;
    std::vector<ChainCallPairAnnotation> StickyChainPairs_;
    // Integer indices remain valid across snapshot copies/moves; no self-views.
    std::vector<std::size_t> ScopeEndOrder_;
    friend LibraryAnnotations CompileLibraryAnnotations(const Style&);
};

// Validates identifiers, qualified chain roots, fixed Begin:End pairs and
// duplicate/ambiguous endpoints. Throws invalid_argument, including for raw
// programmatic input. Uses bounded metadata scratch, not per-token matchers.
void ValidateLibraryAnnotationStyle(const Style&);

// Explicit compilation after final overrides, like CompilePolicies. Own the
// result separately: mutable Configuration never caches a derived snapshot.
[[nodiscard]] LibraryAnnotations CompileLibraryAnnotations(const Style&);

} // namespace formatter
