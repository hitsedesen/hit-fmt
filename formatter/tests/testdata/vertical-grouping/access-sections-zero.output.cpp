class TState {
    int Value;
private:
    int State_;
};

struct TBuffer {
    void Reset();
protected:
    int Size;
private:
    int Capacity;
};

class TExplicit {
public:
    int Value;
private:
    int State_;
};

class TCommented {
    int Value; // Current value.
    // Implementation details.
private:
    int State_;
};
