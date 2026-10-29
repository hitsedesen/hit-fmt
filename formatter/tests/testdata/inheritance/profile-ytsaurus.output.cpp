struct Pane
    : Widget
{
    Pane(int width)
        : Widget(width)
    { }
};

class ConversionPreviewDialog final
    : public InteractiveWindowWithKeyboardNavigation
    , private SourceSelectionObserver
{
public:
    ConversionPreviewDialog(Document document, PreviewOptions options, EventDispatcher& dispatcher)
        : InteractiveWindowWithKeyboardNavigation(dispatcher)
        , Document_(std::move(document))
        , Options_(options)
    { }
private:
    Document Document_;
    PreviewOptions Options_;
};
