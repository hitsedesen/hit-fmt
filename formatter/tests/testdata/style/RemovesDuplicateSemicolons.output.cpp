class Handler
{
    DECLARE_HANDLER();
};

void function()
{
    prepare();
    for (;;) {
        wait();
    }
    finish();
    ;
}
