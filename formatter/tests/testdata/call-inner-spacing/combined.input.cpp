void f(int value);
void g(int n) {
    f( n );
    (*callback)( n );
    (T) ( n );
}
