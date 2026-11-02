void f() try {
  work();
} catch (...) {
  recover();
}

namespace Outer {
namespace Inner {
void bridge(int value) try {
  visit(value, [value](int item) {
    switch (item) {
      case 0: {
        invoke(value, item);
        break;
      }
      default:
        invoke(item);
        break;
    }
  });
} catch (...) {
  if (value) {
    recover();
  }
}
}
}

void static_calls() {
  try {
    Channel<>::Send();
    ::scope::Channel< /* defaults */ >::Send(1);
    Channel<int>::Send();
    Dispatch<>(1);
    object.Send<>(1);
  } catch (...) {
    Channel<>::Report();
  }
}

void default_call() try {
  Channel<>::Send();
} catch (...) {
  Report();
}
