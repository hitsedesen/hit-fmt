auto tight = [](int value) { return value; };
auto wide = []   (int value) { return value; };
auto tabbed = [=]	(int value) { return value; };
auto empty = []() { return 1; };
auto noArguments = []  () { return 2; };
auto implicit = []  { return 3; };
auto captured = [base = 1]  (int value) { return base + value; };
auto generic = []<typename T>(T value) { return value; };
auto commented = [] /* keep */ (int value) { return value; };
auto line = []
    (int value) { return value; };
int array[2];
auto element = array  [0];
int function (int value);
auto invocation = tight  (1);
