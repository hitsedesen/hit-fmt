#pragma once

#include <array>
#include <cstdint>

namespace tokenizer::detail {

enum class AsciiState : uint8_t
{
    Identifier,
    Whitespace,
    String,
    Character,
    LineComment,
    BlockComment,
    RawString,
    Directive,
    DirectiveLineComment,
};

// The low bit marks bytes handled by the state's uninterrupted ASCII loop.
// Space is also continuable, but preserves the logical-line-start flag.
enum class ByteAction : uint8_t
{
    Finish = 0,
    Continue = 1,
    Space = 3,
    Unicode = 4,
    Newline = 6,
    Escape = 8,
    Delimiter = 10,
    Quote = 12,
    RawPrefix = 14,
};

[[nodiscard]] constexpr bool IsAsciiSpace(unsigned char byte) noexcept
{
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\f' ||
        byte == '\v';
}

[[nodiscard]] constexpr auto MakeByteActions(AsciiState state) noexcept
{
    std::array<ByteAction, 256> actions{};
    for (unsigned byte = 0; byte < actions.size(); ++byte) {
        auto action = ByteAction::Continue;
        if (byte >= 0x80) {
            action = ByteAction::Unicode;
        } else if (byte == 0) {
            action = ByteAction::Finish;
        } else if (byte == '\n') {
            action = ByteAction::Newline;
        } else if (IsAsciiSpace(static_cast<unsigned char>(byte))) {
            action = ByteAction::Space;
        }
        switch (state) {
            case AsciiState::Identifier:
                if (byte < 0x80) {
                    action = ((byte >= 'a' && byte <= 'z') ||
                        (byte >= 'A' && byte <= 'Z') ||
                        (byte >= '0' && byte <= '9') || byte == '_')
                        ? ByteAction::Continue
                        : ByteAction::Finish;
                }
                break;
            case AsciiState::Whitespace:
                if (action == ByteAction::Continue) {
                    action = ByteAction::Finish;
                }
                break;
            case AsciiState::String:
            case AsciiState::Character:
                if (byte == '\\') {
                    action = ByteAction::Escape;
                } else if (byte == (state == AsciiState::String ? '"' : '\'')) {
                    action = ByteAction::Delimiter;
                }
                break;
            case AsciiState::BlockComment:
                if (byte == '*') {
                    action = ByteAction::Delimiter;
                }
                break;
            case AsciiState::RawString:
                if (byte == ')') {
                    action = ByteAction::Delimiter;
                }
                break;
            case AsciiState::Directive:
                if (byte == '\\') {
                    action = ByteAction::Escape;
                } else if (byte == '/') {
                    action = ByteAction::Delimiter;
                } else if (byte == '"' || byte == '\'') {
                    action = ByteAction::Quote;
                } else if (byte == 'R') {
                    action = ByteAction::RawPrefix;
                }
                break;
            case AsciiState::DirectiveLineComment:
                if (byte == '\\') {
                    action = ByteAction::Escape;
                }
                break;
            case AsciiState::LineComment:
                break;
        }
        actions[byte] = action;
    }
    return actions;
}

template <AsciiState State>
inline constexpr auto ByteActions = MakeByteActions(State);

} // namespace tokenizer::detail
