void use(int a, int b, int c, int d);
void demo() {
    use(alpha, beta, gamma, delta);
    use(veryLongFirst, veryLongSecond);
    use(
        alpha,
        beta, gamma);
    auto x = nested(alpha, nested(beta, gamma, delta), epsilon);
    MACRO(alpha, beta, gamma, delta);
}
