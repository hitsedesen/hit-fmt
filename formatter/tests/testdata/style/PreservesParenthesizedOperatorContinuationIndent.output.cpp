bool RemainsInRange(int value, int delta)
{
    return (value >> 8) ==
        ((value + delta) >> 8);
}
