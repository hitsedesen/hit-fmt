#include <type_traits>
       namespace NExample {
int F() {
    return []<class T>(T x) {
        if constexpr (std::is_integral_v<T>) { return x; }
        else { return 0; }
    }(1);
}
}
