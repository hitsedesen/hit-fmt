void Contexts() {
    Check(
        first + second
    );
    Check(
        firstLongOperand + /* note */ secondLongOperand +
            thirdLongOperand,
        following
    );
    auto value = firstLongOperand + secondLongOperand +
        thirdLongOperand;
    Check(
        firstLongCondition ? firstLongResult
        : secondLongResult,
        following
    );
    Check(
        "first long literal " "second long literal "
            "third long literal "
    );
}
