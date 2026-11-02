void Example() {
    Call(
        [] { Work(); }
    );
    Call([first, second] { Work(); More(); });
    Call([] {});
    Call([]<class T>(T value) mutable noexcept -> T { return value; });
    Outer(Call([] { Work(); }));
    Call(first, [] { Work(); });
    Call([] { Work(); }, second);
    Call(ordinary);
    Call({first, second});
    Call([] { return 1; }());
    Call(([] { Work(); }));
    Call(flag ? [] { Work(); } : [] { More(); });
    Call(/* callback */ [state = Pair{1, 2}] { Work(); } /* done */);
    Call( // callback starts below
        [] { Work(); }
    );
    Call([] { Work(); } // call closer follows
    );
    Call([firstLengthyCaptureName, secondLengthyCaptureName, thirdLengthyCaptureName] { Work(); });
}
