#pragma once

#include <util/types.hpp>

#include <parser/syntax_tree.hpp>

namespace formatter::detail {

// Exact, already-canonical scalar text. Original IDs remain valid; only the
// strictly internal syntax/gap events may be omitted by private lowering.
struct FlatSpan
{
    parser::TokenRange Tokens;
    u32 Root;
    u32 RegionEnd;
};

struct FlatSpanAccess;
class FlatSpanCollector;

} // namespace formatter::detail
