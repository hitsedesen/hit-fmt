namespace Path {
struct Meter {
    Meter();
    Meter(int value);
    Meter(const Meter&);
    ~Meter();
    int Read();
    operator bool() const;
};
Meter::Meter() {}
Meter::Meter(int value) try {} catch (...) {}
Meter::~Meter() {}
int Meter::Read() { return 1; }
Meter::operator bool() const { return true; }
}
Path::Meter::Meter(const Path::Meter&) = default;

// Specialized owners and nested owners must use the immediate qualifier.
template<class T> struct Vessel {
    Vessel();
    ~Vessel();
    struct Item { Item(); ~Item(); };
};
template<class T> Vessel<T>::Vessel() {}
template<class T> Vessel<T>::~Vessel() {}
template<class T> Vessel<T>::Item::Item() {}
template<class T> Vessel<T>::Item::~Item() {}
template<> Vessel<int>::Vessel() {}

struct Local {
    Local() {}
    template<class T> explicit Local(T) {}
    ~Local() {}
    int Read() { return 2; }
    operator int() const { return 2; }
    Local(const Local&) = default;
    Local& operator=(const Local&) = delete;
};
int ordinary() try { return 1; } catch (...) { return 0; }

// Return type and pointer declarators are not constructors.
struct Result { static int Read(); };
int Result::Read() { return 3; }
int (*factory())(int) { return nullptr; }

// Record-head angle isolation and nonexport remain independent.
template<class T> struct Holder { struct Entry; };
template<class T> struct Holder<T>::Entry {
    Entry() {}
    Entry* Self() { return this; }
};
int Entry = 0;
int marker = 0;
void scope_control(int Entry) { Entry* marker; }
