#define WRAPPER_BEGIN
#define WRAPPER_END
#define INVOKE_BEGIN()
#define INVOKE_END()

WRAPPER_BEGIN
extern "C"{
#if defined(ENABLE_ATTRIBUTE)
__attribute__((visibility("default")))
#endif
int entry(int value);
}
WRAPPER_END;

WRAPPER_BEGIN
extern "C++"{
int second(int value);
}
WRAPPER_END;

INVOKE_BEGIN()
extern "C"{
int invoked(int value);
}
INVOKE_END();

#define OPTIONAL_ANNOTATION
OPTIONAL_ANNOTATION
extern int counter;
