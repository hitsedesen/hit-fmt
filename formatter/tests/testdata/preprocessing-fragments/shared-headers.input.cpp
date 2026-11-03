#if ENABLE_FIRST
void First() {
#else
void Second() {
#endif
    int value=1;
}

namespace outer {
#if ENABLE_FIRST
void First(int value) { ++value; if (value) {Ready();}
#elif ENABLE_SECOND
void Second(int value) { --value;
#else
void Third(int value) {
#endif
if(value){Work(value+1);}
}
void After(){Done();}
}

#if RECORD
struct Record {
#else
void Function() {
#endif
    Ambiguous();
}

#if COMPLETE
void Complete(){Work();}
#else
void Alternative(){Other();}
#endif
