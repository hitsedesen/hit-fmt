void Decode(int mode)
{
    switch (mode) {
    // First mode.
    case 0: {
            Start();
            Stop();
        }

    // A block is the entire preceding case.
    // Both comments describe the next label.
    case 1: {
            Step();
            break;
        }

    /* An unbraced case. */
    case 2:
        // This comment belongs to the case body.
        Step(); // Keep a trailing comment on this line.
        break; // This trailing comment is not label trivia.

    // The preceding case had unbraced statements.
    case 3:
        {
            Step();
        }

        Stop();
        break;

    /* A mixed case preceded this label. */
    case 4: {
            switch (mode) {
            case 0:
                Step();
                break;

            // A nested default label.
            default:
                break;
            }

            break;
        }

    // Fallback mode.
    default:
        // Keep body comments one level below the label.
        break;
        // With no following label, this is a body comment.
    }
}
