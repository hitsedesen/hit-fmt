struct Type {
};
HEADER_BLOCK(Thing, inspect, const Item *, input) { if(input){return input;} return 0; }
OPAQUE_BODY(Label, char*(Box<char*>, Box<bool>)) { int value=1+2; return value; };
HEADER_BLOCK(Type) { Type * item; }
HEADER_BLOCK(Tag) { using Hidden=int; int member; }
// independent
int following(int value) {
    return value+ 1;
}
struct Record {
    Record() {
    }
    operator bool() const {
        return true;
    }
};
Record::Record() {
}
void defaults(int value = [] {
        return 1;
    }()) {
}
