void Dispatch(int value)
{
    switch (value) {
        case 1: {
            Handle(value);
            break;
        }
        case 2:
            Verify(value);
            {
                // A standalone block follows a non-block statement.
                Prepare(value);
                {
                    Handle(value);
                }
            }
            Finish();
            {
                Cleanup();
            }
            break;
        case 3: {
            Handle(value);
        }
            {
                // Only the first block belongs directly to the label.
                Cleanup();
            }
            break;
        case 4:
        case 5:
            Verify(value);
            {
                Handle(value);
            }
            break;
        default: {
            HandleOther();
            break;
        }
    }
}
