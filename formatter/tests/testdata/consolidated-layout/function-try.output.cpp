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
