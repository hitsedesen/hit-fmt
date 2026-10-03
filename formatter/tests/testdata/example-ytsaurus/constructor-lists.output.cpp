class TCell
    : public TBase
{
public:
    TCell()
        : Value_(1)
    { }
    TCell(int value, int tag);
private:
    int Value_;
    int Tag_;
};
TCell::TCell(int value, int tag)
    : Value_(value)
    , Tag_(tag)
{ }
