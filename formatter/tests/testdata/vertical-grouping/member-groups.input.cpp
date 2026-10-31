class TGroups
{
    int First_;


    int Second_;


    void Read();


    void Write();


    int Third_;


    void Reset()
    {
        Before();


        After();
    }



    void Clear()
    {
        ClearState();
    }
    void Touch()
    {
        TouchState();
    }


    struct TNested
    {
        int Value;
    };


    void AfterNested() { Run(); }


public:


    int Public;


    // The method owns this comment.
    template <class T>
    void Set(T value) { Assign(value); }


    void Get() { ReadState(); }

    // An independent group separator.


    void Last() { Finish(); }
};


void First()
{
    Run();
}


void Second()
{
    Run();
}
void InlineOne() {} void InlineTwo() {}
