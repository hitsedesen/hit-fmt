template<class T> struct Box {};
Box<int>make(){return {};}
Box<Box<int>>nested(){return {};}
Box<int>  flat();
int scalar(){return 0;}
NO_INLINE std::tuple<int, int> Serve(int value){return {};}
EXPORT HOT Box<Box<int>> chained(){return {};}
static EXPORT Box<int> qualified_prefix(){return {};}
EXPORT int builtin(){return 0;}
EXPORT Box<int> declared();
EXPORT Box<int> Owner<int>::method(){return {};}
Value Owner<int>::unprefixed(){return {};}
Value Owner::*member;
EXPORT Box<int>*pointer(){return nullptr;}
