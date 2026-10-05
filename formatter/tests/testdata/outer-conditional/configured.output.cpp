void f() {
    if (DefaultCtor_ &&
        !(std::is_base_of_v<TYsonStructLite, TValue> && dontSetLiteMembers && DefaultIsDefaultConstruction_)) {
        go();
    }
}
