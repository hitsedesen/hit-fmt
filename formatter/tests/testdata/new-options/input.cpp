template<class T> T identity(T value) { return value; }
template<class T> template<class U> U Box<T>::convert(U value) { return value; }
template<class T> requires Ready<T> T create(T value) { return value; }
struct Box {
Box() { initialize(); }
explicit Box(int value) : value_(value) { initialize(); }
~Box() { release(); }
int get() { return value_; }
};
Box::Box() try : value_(0) { initialize(); } catch (...) { recover(); }
Box::~Box() { release(); }
void expressions() {
auto result = first_long_operand + second_long_operand + third_long_operand + fourth_long_operand + fifth_long_operand + sixth_long_operand;
result += another_long_operand + final_long_operand;
auto short_value = 1;
short_value = 2;
return first_long_operand + second_long_operand + third_long_operand;
Open();
work();
Open();
more_work();
Close();
Close();
}
void unmatched() {
Open();
work();
}
