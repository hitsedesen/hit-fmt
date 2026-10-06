void Sample()
{
    for (int mode = 0; mode < 3; ++mode) {
        if (ready) {
            if (active) {
                const auto anchor = mode == 2
                    ? document.MarkAnchor(
                        {
                            .Kind = AnchorKind::DelimiterColumn,
                            .Fallback = 4,
                            .Minimum = 4,
                            .StructuralBase = true,
                            .Continuation = true,
                        })
                    : InvalidId;
                const auto text = std::string_view(output.Text).substr(
                    map.Output.Begin,
                    map.Output.End - map.Output.Begin);
            }
            const auto callText = std::string_view(output.Text).substr(
                output.Map[2].OutputBegin,
                output.Map[5].OutputEnd - output.Map[2].OutputBegin);
        }
    }
    size_t callbacks = 0, factories = 0, pointers = 0, constructors = 0,
        selfTypes = 0;
}

namespace Tags {

BEGIN_TAGS
enum Token{First, Second};

END_TAGS

} // namespace Tags
