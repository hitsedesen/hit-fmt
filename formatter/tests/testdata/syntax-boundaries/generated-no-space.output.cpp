namespace Outer {
namespace Inner {
}
}
export module demo:part;
import demo:part;
export import<other>;
extern "C"{ int f(); }
using namespace Outer;
using typename Box<int>::type;
struct Derived final: public virtual Base<int>, virtual protected Other {
};
enum class E : unsigned { A };
template<typename T, class...Rest>struct Pack {
};
template<class T>concept Constraint = true;
template<Constraint T>void Constrained(T value) {
}
template< ::Names::Constraint<Box<int>>T/* default */= int>struct Qualified {
};
template<Constraint...Ts>struct ConstrainedPack {
};
template<Constraint T = Box<const int>>struct Defaulted {
};
template<Constraint T>requires Constraint<const T&> void Required(T value) {
}
template<template<class>class C, typename...Ts>void F(Ts...args) {
    auto fn = [args...]() {
        return 1;
    };
    auto other = [&...refs = args ] {
        return 2;
    };
    auto size = sizeof...(Ts);
    return fn();
}
struct C {
    C()try {
    }catch (...) {
    }
    C(int): item(0) {
    }
    int item;
};
void G() {
    int* const volatile p = nullptr;
    int(*callback)(int);
    int C::* member;
    auto[a, b] = pair;
    return value;
    co_return value;
    co_yield value;
    throw value;
    goto done;
    if (x)
        first();
    else if (y)
        second();
    else
        third();
    do
        first();
    while (x);
    try {
        first();
    }catch (...) {
        second();
    }
    switch (x) {
        case 1:
            first();
            break;
        default:
            break;
    }
    auto p = new const int;
    delete p;
    delete [] p;
    auto f = []<class T>(T t)requires Check<T> {
        return t;
    };
    done:
    first();
}
template<class T>requires Check<T> void H(T t)requires Other<T> {
}
template<class T>concept Test = requires(T t){
    typename T::type;
    {t()} noexcept->Concept;
    requires Check<T>;
};
struct Ops {
    int operator()();
    int operator[](int);
    operator bool();
    void operator delete[](void*);
};

alignas(16)int aligned;
int noexcept_fn() noexcept(true);
const::Outer::Value* global;
export{int exposed;int other;}
// clang-format off
void Protected() {return   untouched;}
// clang-format on
void Commented() {
    first();/* keep   content */second();
}

void Modifiers() {
    if constexpr (true)
        Use();
    for co_await (auto item : items)
        Use(item);
    object.template Method<int>();
    const char* joined = "left" "right";
    asm volatile("nop");
}

int Comments() {
    return/* first */   /* second */   value;
}
namespace Alias = Outer;
void Consteval() {
    if!consteval {
        Use();
    }
    if consteval {
        Use();
    }
}
