std::uniform_int_distribution<int> foobariumDistribution(static_cast<int>(foobarSize * 0.5), static_cast<int>(foobarSize * 1.5));
void Sample()
{
    std::uniform_int_distribution<int> foobariumDistribution(static_cast<int>(foobarSize * 0.5), static_cast<int>(foobarSize * 1.5));
    Distribution<long> sampleDistribution(static_cast<long>(sampleCount * 0.5), static_cast<long>(sampleCount * 1.5));
    external::Range sampleRange(static_cast<int>(sampleCount * 0.5), static_cast<int>(sampleCount * 1.5));
    ::external::Distribution<int> globalDistribution(static_cast<int>(sampleCount * 0.5), static_cast<int>(sampleCount * 1.5));
    external::Distribution<int> distribution = make(firstLongArgument, secondLongArgument, thirdLongArgument);
    external::Distribution<int> parameters(int firstLongParameterName, int secondLongParameterName);
    if (external::Range<int> range = make(firstLongArgument, secondLongArgument); range.ready()) {
        consume(range);
    }
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
