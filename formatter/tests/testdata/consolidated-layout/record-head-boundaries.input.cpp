template<class A, class B> struct Pair;
template<class A, class B> struct Pair<Wrap<A,  B>,C> {};
template<> struct Pair<> {};
template<class A, class B> struct Pair<A,/*note*/B> {};
