int f(bool condition) {
    return condition ?
        alphaValue : betaValue;
}

int f(bool condition) {
    return condition
        ?
        alphaValue : betaValue;
}

int f(bool condition) {
    return condition
        ? alphaValue
        : betaValue;
}

int f() {
    return one ? a :
        two ? b :
        three ? c : d;
}
