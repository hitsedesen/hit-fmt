bool IsInside(int Position, int limit)
{
    return Position < 0 || Position > limit || (Position == limit && mode == BoundaryMode::Inclusive);
}

bool IsOrdered(int left, int middle, int right)
{
    return left < middle > right;
}
