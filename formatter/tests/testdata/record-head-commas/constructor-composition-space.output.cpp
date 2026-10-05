template<auto, class> struct Wrap {
};
int Pair= 0;
int marker= 1;
struct Host {
    template<class, auto> struct Pair;
};
template<> struct Host::Pair<Wrap<(1>0), int>, 2> {
    int stored;
    Pair(int first= (1< 2), int second= (3> 2)): stored(first+ second) {
    }
    Pair* self();
    enum Mode { First= (1< 2), Second= (8>> 1) };
};
void outside() { Pair*marker; }
void known_values(int Pair, int marker) {
    Pair* marker;
}
