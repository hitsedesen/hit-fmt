void F() {
    Object object{.data = {.first_property = source.first_property, .second_property = source.second_property,
                           .third_property = source.third_property}};
    Object short_value{.data = {.value = 1}};
    Object scalar{.value = 1};
    Object commented{.data = // retain the explanation
            {.value = 1}, .other = /* label */
            {.value = 2}};
    Object values[] = {[2] = {.value = 1}};
}
