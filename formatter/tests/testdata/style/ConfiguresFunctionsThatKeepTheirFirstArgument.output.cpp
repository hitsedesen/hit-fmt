void function()
{
    render("A deliberately long format string with a first argument that must remain beside the opening parenthesis: %v %v",
        firstValue,
        secondValue);
    inspect(firstArgumentWithAnIntentionallyLongDescriptiveName,
        secondArgumentWithAnIntentionallyLongDescriptiveName,
        thirdArgumentWithAnIntentionallyLongDescriptiveName);
    inspect(firstArgument,
        secondArgument);
}
