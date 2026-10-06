void check_conditions() {
    if (ready && enabled && valid) {
        run();
    }
    if (first_condition_name
        && second_condition_name
        && third_condition_name
        && fourth_condition_name) {
        run();
    }
    if (first_condition_name
        && second_condition_name
        || third_condition_name
        && fourth_condition_name) {
        run();
    }
    if (first_condition_name
        && (left || right)
        && second_condition_name
        && third_condition_name) {
        run();
    }
    if ((first_condition_name && second_condition_name
        && third_condition_name)) {
        run();
    }
    if (evaluate(first_argument_name, second_argument_name,
        third_argument_name)
        && ready
        && enabled) {
        run();
    }
    if (ready
        && evaluate(first_argument_name, second_argument_name,
            third_argument_name)
        && enabled) {
        run();
    }
    if (ready
        && evaluate(left && right, first_argument_name,
            second_argument_name)
        && enabled) {
        run();
    }
    if (auto value = create(left && right); value
        && ready
        && enabled) {
        run();
    }
    if (auto value = left && right; value
        && first_condition_name
        && second_condition_name) {
        run();
    }
    if (left_value > threshold
        && first_condition_name
        && second_condition_name) {
        run();
    }
    if constexpr (first_condition_name
        && second_condition_name
        && third_condition_name) {
        run();
    }
    if (ready) {
        run();
    } else if (first_condition_name
        && second_condition_name
        && third_condition_name) {
        run();
    }
    if (first_condition_name
        && /* next condition */ second_condition_name
        && third_condition_name) {
        run();
    }
    if (first_condition_name
        && // keep attachment
        second_condition_name
        && third_condition_name) {
        run();
    }
    if (first_condition_name
        and second_condition_name
        or third_condition_name) {
        run();
    }
    if (choose ? left && right
        : third_condition_name && fourth_condition_name) {
        run();
    }
    if (ready
        && [] {
            return first_condition_name && second_condition_name
                && third_condition_name;
        }()
        && enabled) {
        run();
    }
    while (first_condition_name && second_condition_name
        && third_condition_name) {
        run();
    }
    for (;
        first_condition_name && second_condition_name
        && third_condition_name;) {
        run();
    }
    bool result = first_condition_name && second_condition_name
        && third_condition_name;
}
