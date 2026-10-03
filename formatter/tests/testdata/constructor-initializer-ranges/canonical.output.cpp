struct PairCallbacks {
    PairCallbacks(): callback([this] {
                stop();
            }, [this] {
                start();
            }) {
    }
};
struct SeparateMembers {
    SeparateMembers(): first([] {
                return 1;
            }), second([] {
                return 2;
            }) {
    }
};
struct NoexceptCallbacks {
    NoexceptCallbacks() noexcept: callback([this]() noexcept {
                stop();
            }, [this]() noexcept {
                start();
            }) {
    }
};
struct NestedCallback {
    NestedCallback(): first(0), callback(wrap(bind([id = 1](int* value) {
                        consume(value, id);
                    }))) {
    }
};
struct TryCallbacks {
    TryCallbacks() try: first([] {
                return 1;
            }), second([] {
                return 2;
            }) {
    } catch (...) {
    }
};
struct Controls {
    int values[3];
    int (*routes[2])(int);
    unsigned flags : 2;
};
