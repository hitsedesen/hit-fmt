#include "z.h"
#include "a.h"

#include <project/runtime/public.h>

#include <vector>

namespace sample::formatting {

class Derived
    : public Base
    , private Other
{
public:
    void Empty()
    { }

    void Run(const std::exception& error)
    {
        auto callback = [this] {
            Handle();
        };
        Await(CALLBACK(&Derived::Run, this)
            .Via(Executor_)
            .Invoke())
            .Check();
    }
};

} // namespace sample::formatting
