void Report()
{
    message(code, "Values %v %v",
            first,
            second
    );
    message(code, "Only a message");
    message(code, text, first, second);
    message(code, "A message that exceeds a narrow column limit %v",
            value
    );
}
