void demo() {
    consume( // first argument
 alpha,
        beta, // next argument
              gamma,
        /* block prefix */
 delta,
        /* adjacent */ /* comments */
 epsilon);
    auto values = { // first element
  alpha,
        beta, // next element
                   gamma,
        /* block prefix */
 delta,
        /* adjacent */ /* comments */
 epsilon};
    consume(alpha, // first comment
          // second comment
 beta);
    auto trailing = {alpha, // trailing comma comment
  };
    consume(alpha, /* inline */ beta, gamma);
    auto prefixes = consume(alpha, /* first */ /* second */ beta, gamma);
    auto suffix = consume(alpha /* belongs to alpha */, beta, gamma);
    auto nestedComma = consume((alpha, /* expression */ beta), gamma);
    auto trailingBlock = {alpha, /* no next item */};
    consume(alpha, // trailing comment
        beta /* closer comment */
    );
    auto nested = {consume( // nested argument
 x,
        y), // nested element
 z};
    // clang-format off
    consume(alpha, // protected argument
  beta);
    // clang-format on
}
