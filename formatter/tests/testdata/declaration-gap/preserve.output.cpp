template<class T> struct Box {};
Box<int>make(){return {};}
Box<Box<int>>nested(){return {};}
Box<int>  flat();
int scalar(){return 0;}
