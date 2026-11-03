void Example() {
    Handler handler = [](const SyntheticTranslation& translation, const std::string& /*locale*/) -> std::optional<SyntheticAttributedText> {
        SyntheticAttributedText value;
        Work(translation, value);
        return value;
    };
}

void NamedComment() {
    Handler handler = [](const SyntheticTranslationWithLongDescriptiveName& translation, const std::string& locale /*comment*/) -> std::optional<SyntheticAttributedText> {
        SyntheticAttributedText value;
        Work(translation, value);
        return value;
    };
}

void UnnamedControl() {
    Handler handler = [](const SyntheticTranslationWithLongDescriptiveName& translation, const std::string&) -> std::optional<SyntheticAttributedText> {
        SyntheticAttributedText value;
        Work(translation, value);
        return value;
    };
}

void NamedControl() {
    Handler handler = [](const SyntheticTranslationWithLongDescriptiveName& translation, const std::string& locale) -> std::optional<SyntheticAttributedText> {
        SyntheticAttributedText value;
        Work(translation, value);
        return value;
    };
}

void LineComment() {
    Handler handler = [](const SyntheticTranslationWithLongDescriptiveName& translation, const std::string& locale // comment
) -> std::optional<SyntheticAttributedText> {
        SyntheticAttributedText value;
        Work(translation, value);
        return value;
    };
}

void FunctionControl(const SyntheticTranslationWithLongDescriptiveName& translation, const std::string& locale /*comment*/) {
    Work(translation, locale);
}
