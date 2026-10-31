int global_count;
int BadGlobal;
struct Record
{
    int Count;
    int bad_member;
    static int arbitrary_static;
private:
    int Count_;
    int Missing;
    static int arbitrary_private_static;
protected:
    int ProtectedCount;
    int bad_protected;
public:
    void Work(int bad_parameter)
    {
        int localCount;
        int bad_local;
        static int StaticCount;
        static int bad_static;
        int Logger;
        int OtherLogger;
        struct Nested { int Count; int bad_nested; };
        int afterNested;
    }
};
int Logger;
void External(int bad_parameter);
