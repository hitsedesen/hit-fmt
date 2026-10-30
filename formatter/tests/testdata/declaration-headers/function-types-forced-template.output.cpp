struct TaskHooks
{
    std::function<
        bool(
            const ExecutionPreferences& preferences,
            const scheduling::WorkUnit* work,
            JobQueue* queue,
            unsigned long long attemptNumber)> Submit;
    Callback<
        bool(
            const ExecutionPreferences& preferences,
            const scheduling::WorkUnit* work,
            JobQueue* queue,
            unsigned long long attemptNumber)> Resume;
    Wrapper<
        Callback<
            bool(
                const ExecutionPreferences& preferences,
                const scheduling::WorkUnit* work,
                JobQueue* queue,
                unsigned long long attemptNumber)>> Pending;
    Callback<
        bool(unsigned ticket)> Ready;
};

using DispatchSignature = int(
    const scheduling::WorkUnit* work,
    const ExecutionPreferences& preferences,
    JobQueue* queue,
    unsigned long long attemptNumber) noexcept;
Callback<
    int(1 + 2)> Constant;
Callback<
    int(unresolved)> Unresolved;
Callback<
    int(left * right)> Product;
Callback<
    int(left & right)> Bitwise;
Callback<
    int(left && right)> Logical;
Callback<
    decltype(compute(1, 2))> Result;
Callback<
    bool(
        const Batch<
            unsigned>& incoming,
        const Batch<
            long>& deferred,
        const Batch<
            unsigned long long>& completed,
        JobQueue* destination)> Route;
int (*MakeCallback())(
    int firstParameterWithSomeExtraContext,
    int secondParameterWithSomeExtraContext,
    int thirdParameterWithSomeExtraContext);

int (*ChooseCallback(
    int firstSelectionParameter,
    int secondSelectionParameter,
    int thirdSelectionParameter) noexcept)(
    int firstParameterWithSomeExtraContext,
    int secondParameterWithSomeExtraContext,
    int thirdParameterWithSomeExtraContext)
{
    return nullptr;
}

int (&GetCallback())(
    int firstParameterWithSomeExtraContext,
    int secondParameterWithSomeExtraContext,
    int thirdParameterWithSomeExtraContext);
int (TaskHooks::*MakeMemberCallback(
    int selectionParameter))(
    int firstParameterWithSomeExtraContext,
    int secondParameterWithSomeExtraContext,
    int thirdParameterWithSomeExtraContext);
int (*CallbackPointer)(
    int firstParameterWithSomeExtraContext,
    int secondParameterWithSomeExtraContext,
    int thirdParameterWithSomeExtraContext);
int (*ShortCallback())(int first, int second);
void AcceptCallback(
    int (*callback)(
        int firstParameterWithSomeExtraContext,
        int secondParameterWithSomeExtraContext,
        int thirdParameterWithSomeExtraContext));
