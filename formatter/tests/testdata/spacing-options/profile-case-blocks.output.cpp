void Dispatch(int value)
{
    switch (value) {
        case 1: {
            Handle(value);
            break;
        }
        default: {
            HandleOther();
            break;
        }
    }
}
