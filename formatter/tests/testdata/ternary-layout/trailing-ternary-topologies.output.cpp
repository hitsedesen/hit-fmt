int f(bool condition) {
    return condition ? alphaValue : betaValue;
}

int f(bool condition) {
    return condition ? alphaValue : betaValue;
}

int f() {
    return condition ?
        alphaValue /* keep */
        :
        betaValue;
}

int f() {
    return outer ? inner ? alpha : beta : gamma;
}
