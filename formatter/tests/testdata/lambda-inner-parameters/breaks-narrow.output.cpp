auto f = [] (Ref<int()> g) { return g(); };

auto plain = [] (int value) { return value; };

auto reference =
    [] (const Ref<int()>& callback) { return callback(); };

auto pointer = [] (int* value) { return *value; };

auto generic = [] <class T> (
        T value,
        Ref<T()> callback) { return callback(); };

auto defaulted =
    [] (int value = calculate(1, 2)) { return value; };

auto commented = [] (
        Ref<int()> // keep
    callback) { return callback(); };

auto grouped =
    [] (int (*callback)(int)) { return callback(1); };

void regular(Ref<int()> callback, int value);
