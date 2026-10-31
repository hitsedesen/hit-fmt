void Dispatch()
{
    if (Ready()) [[likely]] {
        Act();
    }

    if ([] {
        return true;
    } ())
    {
        Act();
    }

    while (DrainQueue()) { }

    if (Ready()) {
        Act();
    }

    class Guard final
    {
    public:
        virtual bool IsReady() const
        {
            return false;
        }

        void Empty()
        { }

        void Run()
        {
            Check();
            Notify();
        }
    };
}
