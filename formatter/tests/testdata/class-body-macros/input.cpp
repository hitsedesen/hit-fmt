BEGIN_SETTER(String, std::string)
    void OnStringScalar(const char* value) override
    {
        Node_->SetValue(std::string(value));
    }
END_SETTER()

BEGIN_SETTER(Integer, int)
    void OnIntegerScalar(int value) override
    {
        Node_->SetValue(value);
    }
    void Reset() override
    {
        Node_->SetValue(0);
    }
END_SETTER()

namespace Detail {
OPEN_RECORD(Outer, Helper(alpha   +  beta))
public:
int Read() const {return value_;}
BEGIN_SETTER(Inner, int)
void Update(int value) {value_=value;}
END_SETTER();
private:
int value_;
CLOSE_RECORD()

void Neighbor(int value)
{
    Use(value);
}
}

OPEN_RECORD(Empty, int)
CLOSE_RECORD()

OPEN_RECORD(Conditional, int)
#if FEATURE
    void Enabled() {Use(1);}
#else
    void Disabled() {Use(0);}
#endif
    DEFINE_METHOD(Reset, void) {Use(2);}
CLOSE_RECORD()

OPEN_RECORD(Protected, int)
// hit-fmt off
 void Untouched()  { Use(3); }
// hit-fmt on
    void Editable() {Use(4);}
CLOSE_RECORD()

struct Ordinary {
void Neighbor() {Use(5);}
};
void Calls() {BEGIN_SETTER(1, 2); END_SETTER();}
void Qualified() {detail::OPEN_RECORD(1, 2);}
