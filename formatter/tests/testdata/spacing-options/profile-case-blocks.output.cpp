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

        case 3:
            {
                Handle(value);
            }

            {
                // Consecutive blocks share the case statement level.
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

        case 6: {
            Handle(value);
            break;
        } // Still a sole case block.

        case 7:
            {
                Handle(value);
            }

            {
                Cleanup();
            }

        default: {
            HandleOther();
            break;
        }
    }
}
