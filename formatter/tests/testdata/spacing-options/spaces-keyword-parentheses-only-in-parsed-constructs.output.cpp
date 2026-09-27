struct alignas (16) S { void f() noexcept (true); void g() noexcept; };
static_assert (sizeof (int)>0);
int x=alignof (S); auto y=typeid (x); auto z=noexcept (x);
using T=decltype (x); typedef decltype (x) U;
template<class V> requires (true) struct Box {};
template<class V> concept C=requires (V v) { v; };
