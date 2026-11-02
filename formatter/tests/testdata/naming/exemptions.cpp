struct Range {
    int begin() { return 0; }
    int end() { return 0; }
    int cbegin() { return 0; }
    int cend() { return 0; }
    int rbegin() { return 0; }
    int rend() { return 0; }
    int crbegin() { return 0; }
    int crend() { return 0; }
    int size() { return 0; }
    int ssize() { return 0; }
    bool empty() { return false; }
    int* data() { return nullptr; }
};
int begin(Range& value) { return value.begin(); }
int end(Range& value) { return value.end(); }
void swap(Range& left, Range& right) { }
int get(Range& value) { return 0; }
int iter_move(Range& value) { return 0; }
void iter_swap(Range& left, Range& right) { }
void external_name(int external_name) { { int external_name; } }
namespace types { struct external_name { }; }
void custom_begin() { }
struct Names {
    int custom_begin;
    int begin;
    int external_name;
};
void begin_extra() { }
void BEGIN() { }
void external_names() { }
void DoWork_() { int data; }
void size(int BadParameter) { }
int main() { return 0; }
int main(int argc, char** argv) { return 0; }
void main_extra() { }
void DoMain_() { int main; }
class Error : public std::exception {
public:
    const char* what() const noexcept override { return "failure"; }
};
struct Mutex {
    void lock() { }
    void unlock() { }
    void try_lock() { }
    void try_lock_for() { }
    void try_lock_until() { }
    void lock_shared() { }
    void unlock_shared() { }
    void try_lock_shared() { }
    void try_lock_shared_for() { }
    void try_lock_shared_until() { }
};
void hash_value() { }
void what_extra() { }
void DoCheck_() { int lock; }
