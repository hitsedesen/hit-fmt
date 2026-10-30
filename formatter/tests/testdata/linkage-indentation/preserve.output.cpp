extern "C" {
void First();
int status;
}
extern "C++"
{
    void Indented();
}
namespace Bridge {
    extern "C" {
    // Entry points.
    void Entry() { Work(); }
    extern "C++" {
        void Nested();
    }
    }
}
extern "C" {
#if ENABLED
void Conditional();
#endif
}
extern "C" {} // Empty linkage.
extern "C" { void Inline(); }
extern "C" void Single();
void Neighbor();
extern "C" {
       int F() {
           return 1;
       }
       void Other();
}
extern "C++" { extern "C" {
       void InlineNested();
}
}
