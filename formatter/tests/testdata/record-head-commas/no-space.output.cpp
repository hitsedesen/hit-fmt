template<class,class> struct Wrap;
template<class... Ts> struct Slots;
template<class A> struct Slots<A,int> {
};
template<> struct Slots<long,Wrap<char,short>> {
};
template<> struct Slots<> {
};
