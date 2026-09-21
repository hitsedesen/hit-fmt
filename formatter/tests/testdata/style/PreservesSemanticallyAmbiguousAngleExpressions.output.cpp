bool Compare(int left, int middle, int right)
{
    return left < middle > (right);
}

bool CompareGlobal(int left, int middle)
{
    return left < middle > ::globalValue;
}
