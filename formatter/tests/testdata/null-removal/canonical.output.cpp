#define CLASS_NAME(name) name

struct CLASS_NAME(Example) {
};

class CLASS_NAME(Widget) {
};

union CLASS_NAME(Storage) {
};

struct CLASS_NAME(WithMember) {
    ;
    ;
    int member;
    ;
};

void beforeUnknown() {
};
;
struct CLASS_NAME(Adjacent) {
};
;
;
int value;


void known() {
}

struct Plain {
};
;

namespace Nested {
void before() {
};
;
struct CLASS_NAME(Inner) {
};
;
void after() {
}
int tail;
}
;

extern "C" {
struct CLASS_NAME(Linked) {
};
;
int linkedValue;

}

void statements() {
    int local;
    ;
    ;
ANNOTATED(local) {};
;
    ;
    int next;
    ++next;
}

void safeTail() {
}

