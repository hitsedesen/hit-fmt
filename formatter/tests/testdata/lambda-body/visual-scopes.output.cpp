void Example() {
    auto begin = [] {
        stream.Begin();
    };
    auto end = [] {
        stream.End();
    };
    Invoke([] {
        stream.Begin();
        Work();
    });
    Invoke([] {
        stream.End();
        Work();
    });
    Invoke([] {
        stream.Begin();
        Work();
        stream.End();
    });
    Invoke([] {
        left.Begin();
        Work();
        right.End();
    });
    Invoke([] {
        Wrap(left.Begin(), right.Begin());
        Work();
    });
    Invoke([] {
        stream.Begin();
        Work();
    });
    Invoke([] {
        stream.Begin();
            Work(  value );
    });
    stream.Begin();
        Invoke([] {
            Work();
        });
    stream.End();
}

void InlineBody() {
    stream.Begin();
    Invoke([] {
        Work();
    });
}
