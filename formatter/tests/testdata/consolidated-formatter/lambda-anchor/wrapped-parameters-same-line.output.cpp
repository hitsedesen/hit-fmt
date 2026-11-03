void Example() {
    Run([retainedObject = RetainObject(), referenceValue = GetReferenceValue()](
        const TSomeVeryLongSyntheticParameterType& argument) mutable {
        Work(argument);
    });
}

void Nested() {
    if (ready) {
        auto callback = [retainedObject = RetainObject(), referenceValue = GetReferenceValue()](
            const TSomeVeryLongSyntheticParameterType& argument) mutable {
            Work(argument);
        };
        Run([](int argument) {
            Work(argument);
        });
    }
}
