void f() {
    auto simple = new int[count + 1];
    auto grouped = new int[(count + 1) * stride];
    auto matrix = new int[rows + 1][columns * 2];
    auto pointers = new int*[count + 1];
    auto placed = new (arena) Cell[count + 1]{};
    auto nested = new int[values[index + 1] + extent(2 * count)];
    auto conditional = new int[count < limit ? count + 1 : limit - 1];
    auto indirect = new int[*size + 1];
    auto templated = new int[extent<int>(count + 1)];
    auto offset = new int[count + 1] + distance;
    auto empty = new int[]{1, 2};
}
