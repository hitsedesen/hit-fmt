void deeper(bool ready) {
    Visit(
        MakeCallback(
            [&] {
                [[likely]] {
                    [[maybe_unused]] int result = 0;
                    [[likely]] if (ready) {
                        Touch();
                    }
                    [[unlikely]]
                    [[vendor::tag(1)]]
                    {
                        Touch();
                    }
                }
                if (ready) [[likely]] {
                    Touch();
                }
                [[likely]] Touch();
                [[unlikely]] return;
            }), other);
}

void shallower() {
    Visit(
        [] {
            [[likely]]
            {
                Touch();
            }
            switch (1) {
                case 0:
                [[fallthrough]];
                default:
                    break;
            }
        }, other);
}

void ordinary() {
    Visit(
        [] {
            {
                [[maybe_unused]] int value = 0;
                Touch();
            }
        }, other);
}

void inline_body() {
    Visit(
        [] { [[likely]] {
                Touch();
            } }, other);
}
