void consume( [[maybe_unused]]int first,[[tag]]int second );
void trailing( int value[[tag]] );
void extensions( __attribute__((unused))int first, int second __attribute__((unused)) );
__attribute__((destructor(101)))static void Cleanup() {}
struct __attribute__((packed))Record { int value; };
class __declspec(dllexport)Forward;
union __attribute__ /* note */ ((aligned(16)))Aligned { int value; };
struct __attribute__((packed))__attribute__((aligned(8)))Repeated {};
struct __attribute__((packed))[[nodiscard]]Mixed {};
struct __attribute__((packed))RecordNoGap {};
void run() {
    for ( [[maybe_unused]]const auto& token : tokens ) {}
    for ( [[tag]][[other]]int i = 0; i < 2; ++i ) {}
    auto callback = []( [[tag]]int value ) {};
    auto extension = []( __attribute__((unused))int value ) {};
    int[[tag]]local;
    [[tag]][[other]]int consecutive;
}
