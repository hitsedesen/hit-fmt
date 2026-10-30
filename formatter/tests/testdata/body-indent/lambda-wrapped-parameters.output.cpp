void Work();

auto MakeCallback()
{
    return [] (
        int type,
        int other) {
        switch (type) {
            case 0:
            case 1: {
                Work();
                break;
            }
            case 2:
            case 3: {
            }
            default: {
                break;
            }
        }
        Work();
    };
}

void RegisterCallback()
{
    Register(0, [] (
        int type,
        int other) {
        switch (type) {
            case 0:
            case 1: {
                Work();
                break;
            }
            default: {
                break;
            }
        }
    });
}
