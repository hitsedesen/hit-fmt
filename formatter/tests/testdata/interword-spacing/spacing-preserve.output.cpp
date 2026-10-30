#define DECLARE(name) const   int   name

template<class T> struct Box {
};
template<class... Items> void Pack(Items... items);
Box<int>   boxed;
decltype(new   Widget)   pointer;
int* const   volatile   qualified = nullptr;

void F() {
    int   value = 1;
    const   int   other = value;
}

static   unsigned   long   count;
Widget   object;
const	volatile	int	flags = 0;

const   Widget   Convert(const   Widget   input) {
    return   input;
}

void Literals() {
    const   char* text = "keep   these   spaces";
    const   char* joined = "left" "right";
    const /* keep   this */ int   value = 1;
}
