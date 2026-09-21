void Decode()
{
    while (HasInput()) {
        #define APPLY_STEP(value) \
            Process(value); \
            Advance()

        int value = Read();

        #undef APPLY_STEP
    }
}
