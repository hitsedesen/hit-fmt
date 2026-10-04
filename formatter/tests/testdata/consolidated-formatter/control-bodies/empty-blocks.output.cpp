void spin(bool a) {
    while (a) { }
    while (a) { }
    for (; a; ) { }
    do { } while (a);
    if (a) { } else {
        Stop();
    }
    if (a) /* keep */ ;
}
