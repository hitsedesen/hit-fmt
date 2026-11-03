std::uniform_int_distribution<int> foobariumDistribution(
    static_cast<int>(foobarSize * 0.5),
    static_cast<int>(foobarSize * 1.5));
void Sample()
{
    std::uniform_int_distribution<int> foobariumDistribution(
        static_cast<int>(foobarSize * 0.5),
        static_cast<int>(foobarSize * 1.5));
    Distribution<long> sampleDistribution(
        static_cast<long>(sampleCount * 0.5),
        static_cast<long>(sampleCount * 1.5));
    external::Range sampleRange(
        static_cast<int>(sampleCount * 0.5),
        static_cast<int>(sampleCount * 1.5));
    ::external::Distribution<int> globalDistribution(
        static_cast<int>(sampleCount * 0.5),
        static_cast<int>(sampleCount * 1.5));
    external::Distribution<int> distribution =
        make(firstLongArgument, secondLongArgument, thirdLongArgument);
    external::Distribution<int> parameters(
        int firstLongParameterName,
        int secondLongParameterName);

    if (external::Range<int> range =
        make(firstLongArgument, secondLongArgument);
        range.ready())
    {
        consume(range);
    }

    BufferSize maximumEncodedByteCountForCurrentBuffer = Estimate(
        unencodedByteCountForCurrentBuffer,
        estimatedByteCountForCurrentBuffer);
    size_t maximumReservedByteCountForCurrentBuffer = Estimate(
        unreservedByteCountForCurrentBuffer,
        estimatedByteCountForCurrentBuffer);
    ByteCount shortName =
        Estimate(firstLongArgument, secondLongArgument, thirdLongArgument);
    ByteCount fitting = Estimate(first, second);
    [[maybe_unused]] ByteCount annotated =
        Estimate(firstLongArgument, secondLongArgument, thirdLongArgument);
    ByteCount /* external type */ commented =
        Estimate(firstLongArgument, secondLongArgument, thirdLongArgument);
    FILE* buffer = OpenTemporaryBuffer(
        capacity,
        alignment,
        flags,
        retryLimit,
        readMode,
        writeMode,
        timeoutSeconds,
        fallbackMode);
    ExternalBuffer  *  pointer =
        Acquire(firstLongArgument, secondLongArgument, thirdLongArgument);
    ExternalBuffer& reference =
        Acquire(firstLongArgument, secondLongArgument, thirdLongArgument);
    ExternalBuffer&& movable =
        Acquire(firstLongArgument, secondLongArgument, thirdLongArgument);
    external::Buffer<int, long>* qualified =
        Acquire(firstLongArgument, secondLongArgument, thirdLongArgument);
    ::external::Buffer* const global =
        Acquire(firstLongArgument, secondLongArgument, thirdLongArgument);
    [[maybe_unused]] ExternalBuffer /* type */ * annotatedPointer =
        Acquire(firstLongArgument, secondLongArgument, thirdLongArgument);
    ExternalBuffer* nested = Adapt(
        Acquire(firstLongArgument, secondLongArgument, thirdLongArgument),
        fourthLongArgument);
    ExternalBuffer* conditional = ready
        ? Acquire(firstLongArgument, secondLongArgument, thirdLongArgument)
        : fallback;
    ExternalBuffer* grouped =
        (Acquire(firstLongArgument, secondLongArgument, thirdLongArgument),
            fallback);
    ExternalBuffer* ambiguousOperand = Adapt((ExternalValue) & operand, firstLongArgument, secondLongArgument, thirdLongArgument);
    ExternalBuffer* opaqueArgument = Acquire(
        MACRO(value) SUFFIX(other),
        firstLongArgument,
        secondLongArgument,
        thirdLongArgument);
    ExternalBuffer* multiple = Acquire(firstLongArgument, secondLongArgument, thirdLongArgument), *other = fallback;
    ExternalBuffer * condition ? target = Acquire(firstLongArgument, secondLongArgument, thirdLongArgument) : fallback;
    ExternalBuffer* opaque = PREFIX Acquire(firstLongArgument, secondLongArgument, thirdLongArgument);
    ExternalBuffer* incomplete = Acquire(firstLongArgument, secondLongArgument, thirdLongArgument) +;
    ExternalBuffer* comparison == Acquire(firstLongArgument, secondLongArgument, thirdLongArgument);
    ExternalBuffer* compound += Acquire(firstLongArgument, secondLongArgument, thirdLongArgument);
    ExternalBuffer* nestedOnly(
        Acquire(target = firstLongArgument, secondLongArgument, thirdLongArgument));
    multiplier * product =
        Acquire(firstLongArgument, secondLongArgument, thirdLongArgument);
    int knownValue;
    knownValue * product =
        Acquire(firstLongArgument, secondLongArgument, thirdLongArgument);
    ByteCount plain;
    ByteCount assigned;
    assigned =
        Estimate(firstLongArgument, secondLongArgument, thirdLongArgument);
    external::Range unresolved(Unknown);
    left < middle > right(1, 2);
    external::left < middle > right(1, 2);
    external::value * factor;
    external::value & mask;
    external::value && enabled;
    external::value<int> * factor;
    external::value<int> & mask;
    external::value<int> && enabled;
}
