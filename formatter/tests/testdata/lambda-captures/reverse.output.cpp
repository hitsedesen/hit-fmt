void sample(int value, int& source) {
    auto equal = [ = ] { return value; };
    auto ref = [ & ] { return value; };
    auto wideEqual = [ = ] { return value; };
    auto tabRef = [ & ] { return value; };
    auto equalAndRef = [ =, &source ] { return source; };
    auto refAndValue = [ &, value ] { return value; };
    auto valueAndRef = [ value, &source ] { return source; };
    auto init = [ value=&source ] { return value; };
    auto nested = [ value=(source = value) ] { return value; };
    auto commentedAssignment = [ value /* keep */ = /* keep */ &source ] {
        return value;
    };
    auto area = [ height=value, width=source ] (int n) {
        return height * width + n;
    };
    auto byRef = [ &value ] { return value; };
    auto empty = [] { return 0; };
    auto spacedEmpty = [  ] { return 0; };
    auto commented = [/* keep */ value ] { return value; };
    auto commentAfter = [ value /* keep */] { return value; };
    auto multiline = [
        value
    ] { return value; };
    auto defaultMulti = [
        =
    ] { return value; };
    auto parameters = [ value ]  (int n) { return value + n; };
    int values[4];
    int element = values [1];
}
struct Owner {
    int member;
    void use() {
        auto self = [ this ] { return member; };
        auto copy = [ *this ] { return member; };
    }
};
