#include <memory>
#include <sstream>

namespace NExample {

void F()
{
    std::ostringstream output;
    output << "first=" << 1
        << ", second=" << 2;
    auto callback = [](int count) {
        auto bytes = std::unique_ptr<char[]>(new char[count]);
        (void)bytes;
    };
    callback(1);
}

}
