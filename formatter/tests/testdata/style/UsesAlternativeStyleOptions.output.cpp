namespace sample
{

    class Widget {
    public:
        void function () {
            if ( ready )
            {
                work ();
            }
            auto callback = []
            {
                return values[ index ];
            };
        }
        void empty () {
        }
    };

    void inspect ( const Widget *widget, int &count ) {
        switch ( count )
        {
            case 1: use ( widget );
                break;
            default: break;
        }
    }

} // namespace sample
