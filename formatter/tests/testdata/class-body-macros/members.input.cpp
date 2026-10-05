namespace Detail {
OPEN_RECORD(Outer, int)
public:
int Read() const {return value_;}
void Reset() {value_=0;}
BEGIN_SETTER(Inner, int)
void Update(int value) {value_=value;}
END_SETTER();
private:
int value_;
void Store(int value) {value_=value;}
CLOSE_RECORD()

void Neighbor(int value) {Use(value);}
}
