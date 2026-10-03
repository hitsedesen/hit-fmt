int compute(int first, int second) {
    return first + second
        + first + second;
}
bool ready(bool first, bool second, bool third) {
    return first && second
        && third;
}
int choose(bool flag, int alpha, int beta) {
    return flag ? alpha + beta
        : alpha * beta;
}
int mixed(int alpha, int beta) {
    return alpha * beta
        + alpha * beta;
}
void update(int& result, int alpha, int beta) {
    result =
        alpha + beta + alpha
        + beta;
    result =
        alpha + beta + alpha
        + beta; // keep comment
    result =
        alpha + beta + alpha
        + beta; /* keep comment */
}
