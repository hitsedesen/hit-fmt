void Check()
{
    if (shortCondition) {
        Accept();
    }

    if (firstLongCondition &&
        secondLongCondition)
    {
        Accept();
    }

    for (int index = 0;
        index < count;
        ++index)
    {
        Accept();
    }

    if (thirdLongCondition &&
        fourthLongCondition)
    {
        Accept();
    }
}
