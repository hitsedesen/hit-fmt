void Update()
{
    TransformRecord::SelectedValueStorage destination;
    const auto fillSelectedMember = [&] <class T> (
        int selector,
        T TransformRecord::SelectedValueStorage::* member) {
        auto pairs = std::vector<std::pair<int, T>>{{0, T{1}}};
        if (selector) {
            for (const auto& [position, value] : pairs) {
                destination.*member = value;
                auto pointer = &destination;
                pointer->*member = value + 1;
            }
        }
    };
    Apply(
        [] (auto* object, auto member) {
            if (object) {
                (object->*member)(1);
            }
        });
}
