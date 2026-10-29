#include <vector>
using X=std::vector<int>;
using Number=int;
using Callback [[deprecated]]=void (*)(int);
template <typename T>
using Items=std::vector<T>;
namespace nested {
using Pointer=int*;
using std::vector;
using namespace std;
}
struct Owner {
    using Reference=int&;
};
void Local() {
    using Result=decltype(1 + 2);
}
