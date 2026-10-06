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
struct Holder
{
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
struct Known
{
    int value;
};
struct Known directEmpty{};
struct Known directValue{1};
struct Known directCall{produce()};
class KnownMessage;
class KnownMessage directMessage{2};
union KnownPayload;
union KnownPayload directPayload{3};
struct External directExternal{9};
struct External negative{-1};
struct External designated{.value = 10};
struct External *directPointer{};
struct Scope::Packet directQualified{4};
struct Batch<int> directBatch{5};
struct Packet directPair[]{{1}, {2}};
struct DirectHolder
{
    struct Known member{6};
};
void useDirect() {
    struct Known localEmpty{};
    struct Known localValue{7};
    struct External localExternal{8};
    int seed = 11;
    struct External fromValue{seed};
}
struct Sealed;
struct Sealed final
{
    int value;
};
struct Exported Record {
    int value;
};
struct Exported Empty { };
struct Exported Destructible {
    ~Destructible() { }
};
template<int N> struct Derived: Base<N == 1>
{
    int value;
};
