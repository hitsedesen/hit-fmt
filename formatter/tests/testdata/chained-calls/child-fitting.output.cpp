void inspect() {
    if (ready) {
        auto measured = measureText(
            source.slice(
                span.InitialSourceOffset,
                span.FinalSourceOffset - span.InitialSourceOffset),
            tabWidth);
        auto trimmed = measureText(
            source.slice(
                span.InitialSourceOffset,
                span.FinalSourceOffset - span.InitialSourceOffset).trim(),
            tabWidth);
        auto concise = measureText(source.slice(begin, end), tabWidth);
        if (valid) {
            auto fragment = buffer
                .slice(startingOffset, currentPositionOffset - startingOffset);
            auto value =
                source.collect(firstArgument, secondArgument, thirdArgument);
            auto shortValue = source.collect(one, two);
            auto result = buildResult(
                wrapValue(alphaArgument, betaArgument),
                gammaArgument);
            auto deliberatelyLongResultVariable = exceptionallyLongReceiverName
                .collect(firstArgument, secondArgument);
            auto sameBounds =
                boundaries_[item.Range.Begin] == boundaries_[item.Range.End];
            return cache_.BlockAt(index / EntriesPerBlock)->Entries
                [index % EntriesPerBlock];
        }
    }
}
