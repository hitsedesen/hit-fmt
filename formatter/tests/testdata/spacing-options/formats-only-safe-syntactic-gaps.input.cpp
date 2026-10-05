int * p; int & r; int * * q; int * const c = nullptr;
int values[ 2 ]={1,2,};
auto [ one , two ] = Pair();
int f(int * a,int & b){auto x=a[ 0 ]+3; auto y=(int)x; auto neg= - x; auto post=x ++; auto choice=x?y:z; auto z=a+ +x; if(x<=3)f(a,b); if(int init=1 ;init)f(a,b); for(int i=0 ;i<3 ;++i)f(a,b); return x ;}
#define MAC(x) x+x
template<class... Items> struct Forwarder {
    template<class Owner> auto operator()(Owner* object, Items&&... values) const;
};
template<class... Items> void Pointers(Items*... values);
template<class... Items> void References(Items&... values);
template<class... Items> void Unnamed(Items&&...);
template<class... Items> void Callbacks(Items (*... callbacks)(int));
template<class Owner, class... Items> void Members(Items Owner::*... members);
