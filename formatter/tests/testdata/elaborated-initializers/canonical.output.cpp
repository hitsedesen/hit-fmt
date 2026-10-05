struct Packet first = {};
Packet second = {};
struct Packet third = {1};
Packet fourth = {1};
class Message message = {2};
union Payload payload = {3};
struct Scope::Packet qualified = {4};
struct Batch<int> batch = {5};
struct Packet pair[] = {{1}, {2}};
struct Packet *pointer = nullptr;
struct Packet make() {
    return {};
}
struct Holder {
    struct Packet member = {6};
};
void use() {
    struct Packet localEmpty = {};
    Packet plainEmpty = {};
    struct Packet localValue = {7};
    Packet plainValue = {7};
    class Message localMessage = {8};
    union Payload localPayload = {9};
}
struct Exported Record {
    int value;
};
template<int N> struct Derived: Base<N == 1> {
    int value;
};
