void consume([[ maybe_unused ]] int first, [[ tag ]] int second);
void trailing(int value [[ tag ]]);
void extensions(__attribute__((unused)) int first, int second __attribute__((unused)));
__attribute__((destructor(101))) static void Cleanup() {}
void run() {
    for ([[ maybe_unused ]] const auto& token : tokens) {}
    for ([[ tag ]] [[ other ]] int i = 0; i < 2; ++i) {}
    auto callback = []([[ tag ]] int value) {};
    auto extension = [](__attribute__((unused)) int value) {};
    int [[ tag ]] local;
    [[ tag ]] [[ other ]] int consecutive;
}
