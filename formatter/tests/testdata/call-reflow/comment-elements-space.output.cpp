void demo() {
    consume( // first argument
        alpha,
        beta, // next argument
        gamma,
        /* block prefix */
        delta,
        /* adjacent */ /* comments */
        epsilon
    );
    auto values = { // first element
        alpha,
        beta, // next element
        gamma,
        /* block prefix */
        delta,
        /* adjacent */ /* comments */
        epsilon
    };
    consume(
        alpha, // first comment
        // second comment
        beta
    );
    auto trailing = {
        alpha, // trailing comma comment
    };
    consume(
        alpha,
        /* inline */ beta,
        gamma
    );
    auto prefixes = consume(
        alpha,
        /* first */  /* second */	beta,
        gamma
    );
    consume(/* range */ {
        first,
        last
    },
        /* empty */ {},
        /* scalar */ value
    );
    consume(
        alpha,
        /* range */ {
            positions.begin(),
            positions.end()
        },
        gamma
    );
    consume(/* tight */{
        first,
        last
    }
    );
    consume(/* type */ Range {
        first,
        last
    }
    );
    consume(
        Range /* suffix */ {
            first,
            last
        }
    );
    (opaque)(/* uncertain */ {
        first,
        last
    });
    consume(/* outer */ {/* inner */ {
            first,
            last
        }
    }
    );
    auto labeled = {/* range */ {
            first,
            last
        },
        /* tight */{
            first,
            last
        }
    };
    auto mixed = consume(
        alpha, /* prefix */ // stays with alpha
        beta,
        gamma
    );
    auto suffix = consume(
        alpha /* belongs to alpha */,
        beta,
        gamma
    );
    auto nestedComma = consume(
        (alpha, /* expression */ beta),
        gamma
    );
    auto trailingBlock = {
        alpha, /* no next item */};
    consume(
        alpha, // trailing comment
        beta /* closer comment */
    );
    auto nested = {
        consume( // nested argument
            x,
            y
        ), // nested element
        z
    };
    // clang-format off
    consume(/* protected */ {first, last});
    consume(alpha, // protected argument
  beta);
    // clang-format on
}
