template<auto,auto> struct ValuePair;
template<> struct ValuePair<(1<2),0> {
};
template<> struct ValuePair<(8>>1),0> {
};
constexpr int indices[] = {1,2};
template<> struct ValuePair<indices[1<2],0> {
};
struct Key {
    int first,second;
};
template<Key,int> struct KeyPair;
template<> struct KeyPair<Key{1<2,2},0> {};
template<auto,class> struct Wrap;
template<class,auto> struct TypePair;
template<> struct TypePair<Wrap<(1>0),int>,2> {
};
template<> struct TypePair<Wrap<(8>>1),   int>,2> {
};
template<> struct TypePair<Wrap<(1<<2),Wrap<(3>2),int>>,0> {
};
void call(int left= (1< 2),int right= (2> 1));
int rows[][2] = {{1< 2,0},{3> 2,0}};
