template<class FirstLongParameterName, typename SecondLongParameterName,
         int ElementCount = (8 >> 1)> struct Header { };
template<
         template<class FirstLongInnerParameterName,
                  class SecondLongInnerParameterName> class Container,
         class Value> struct TemplateTemplate { };
template<class Value = types::Pair<FirstLongArgumentName,
                                   SecondLongArgumentName>> struct DefaultType {
};
template<
         template<class Left = Box<FirstLongArgumentName>,
                  class Right = Box<
                      SecondLongArgumentName>> class Container> struct SharedCloser {
};
template<class... LongParameterPack, auto... Values> struct Packs { };
template<Readable FirstLongParameterName,
         Readable... RemainingLongParameterNames> struct Constrained { };
template<> struct Header<int, char, 4> { };

template<class FirstLongParameterName,
         class SecondLongParameterName> struct Authored { };

template<class FirstLongParameterName, class SecondLongParameterName>
void Invoke(FirstLongParameterName first, SecondLongParameterName second);

auto generic = []<class FirstLongParameterName, class SecondLongParameterName>(
    FirstLongParameterName first,
    SecondLongParameterName second) { return first; };

// hit-fmt off
template< class FirstLongParameterName ,  class SecondLongParameterName > struct Disabled {};
// hit-fmt on

template<class Value = OPAQUE(  alpha,beta  ),
         class Other> struct ProtectedDefault { };
template<class /* keep */ FirstLongParameterName,
         class SecondLongParameterName> struct Comments { };

template<class Value,, class Other> struct MissingParameter { };
template<class Value,> struct TrailingComma { };

template<class Value = Box<int>
