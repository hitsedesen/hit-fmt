void f() {
    CallWithLongName(
        [first = MakeVeryLongValue(), second] {
            Process();
        });
}
