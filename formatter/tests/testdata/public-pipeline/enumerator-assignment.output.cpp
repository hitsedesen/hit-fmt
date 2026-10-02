enum class Mode { First = 0, Second = (1 + 2), Third = (1 << (2 + 1)), Fourth = /* value */3, Fifth = 4, };
int count = 0;
void adjust(int step = 1) {
    count += step;
}
