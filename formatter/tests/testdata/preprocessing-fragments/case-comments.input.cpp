void Decode(int width, const void* input, int* output) {
    switch (width) {
        case 0:
            Clear(output);
            break; // Keep the trailing case comment here.
        // The generated cases read from typed input.
        /* Each expansion supplies its own break. */
        #define DECODE_CASE(bits, type) \
            case bits: \
                DecodeBits<bits>(static_cast<const type*>(input), output); \
                break;
        DECODE_CASE(1, unsigned char)
        DECODE_CASE(2, unsigned short)
        // Stop generating cases here.
        #undef DECODE_CASE
        default:
            // This comment belongs to the case body.
            Fail(width);
            break;
    }
}

void LocalDirective(int value) {
    // Keep this block-level comment indented, not at column zero.
    // The directive itself is intentionally at column zero.
#define LOCAL_VALUE 3
    Use(value, LOCAL_VALUE);
    // A block comment may also document a directive.
    /* Remove the temporary definition. */
#undef LOCAL_VALUE
}

void TrailingComment(int value) {
    switch (value) {
        case 0:
            Use(value); // This comment is trailing, not directive documentation.
#define FALLBACK_VALUE 4
            // Ordinary case comments still use case indentation.
            Use(FALLBACK_VALUE);
            break;
    }
}
