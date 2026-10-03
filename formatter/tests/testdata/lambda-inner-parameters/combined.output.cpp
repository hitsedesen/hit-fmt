int named(int n) { return n; }
void sample() {
    auto spaced = []( int value ) { return value; };
    auto snug = []( int value ) { return value; };
    auto many = []( int x, int y ) { return x + y; };
    auto generic = []<class T>( T value ) { return value; };
    auto empty = [](  ) { return 0; };
    auto commentedFirst = []( /* keep */ int x ) { return x; };
    auto commentedLast = []( int x /* keep */ ) { return x; };
    auto commentedName = []( int /*token*/ ) { return 1; };
    auto multiline = [](
        int x
    ) { return x; };
    auto defaultValue = []( int x = named(3) ) { return x; };
    auto capture = [ x = 1 ]( int n ) { return x + n; };
    int result = named(3);
    if ( result ) { result++; }
}
