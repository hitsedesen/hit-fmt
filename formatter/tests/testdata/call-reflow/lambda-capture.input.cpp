void f() {
    CallWithLongName([first, second] {
        Process();
    });
}

void f() {
    CallWithLongName([first = MakeVeryLongValue(), second] {
        Process();
    });
}
