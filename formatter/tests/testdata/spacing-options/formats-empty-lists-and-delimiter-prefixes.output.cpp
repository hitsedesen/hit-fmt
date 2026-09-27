struct T { int value; };
auto f( ) -> int{return 1;}
void g( ){f( ); auto lambda=[ ]( ){}; int a [2]; auto x=a [0]; T t {1}; T empty {};}
