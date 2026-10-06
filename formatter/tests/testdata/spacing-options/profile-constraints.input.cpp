template <class Base, class... Alternatives>
    requires (
        Compatible<Base, Alternatives...> &&
        (
            !OptionalChoice<Base, Alternatives...>::DefaultChoiceValue.has_value() ||
            HasDefaultChoice<Base, Alternatives...>)
        )
struct Choice;

template <class Base, class... Alternatives>
void Select()
    requires (
        Compatible<Base, Alternatives...> &&
        (
            !OptionalChoice<Base, Alternatives...>::DefaultChoiceValue.has_value() ||
            HasDefaultChoice<Base, Alternatives...>)
        );

template <class Base, class Tag, Tag... Defaults>
struct ChoiceFactory
{
    template <class... Branches, Tag... Entries>
        requires (
            CompatibleHierarchy<Base, Branches...> &&
            (
                !OptionalChoice<Tag, Defaults...>::DefaultChoiceValue.has_value() ||
                IsDefaultChoiceInMapping<Tag, Defaults..., Entries...>)
            )
    static void Configure();
};

template <class T> requires (Ready<T> && Enabled<T>)
struct Compact;

template <class T>
void Probe() requires (Ready<T> && Enabled<T>);

template <class Tag, Tag... Defaults, CompatibleBase Base, Tag... Entries, class... Branches>
    requires (
        CompatibleHierarchy<Base, Branches...> &&
        // The fallback branch must have a compatible default.
        (
            !OptionalChoice<Tag, Defaults...>::DefaultChoiceValue.has_value() ||
            IsDefaultChoiceInMapping<Tag, Defaults..., Entries...>)
        )
struct ConstrainedChoice;

struct ConstrainedFactory
{
    template <class Tag, Tag... Defaults, CompatibleBase Base, Tag... Entries, class... Branches>
        requires (
            CompatibleHierarchy<Base, Branches...> &&
            // Keep the nested fallback at the retained constraint's level.
            (
                !OptionalChoice<Tag, Defaults...>::DefaultChoiceValue.has_value() ||
                IsDefaultChoiceInMapping<Tag, Defaults..., Entries...>)
            )
    static void Configure();
};
