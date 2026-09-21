Reader::Reader(Input* input)
    : Worker_(WRAP_CALLBACK([=] (Input* current) {
        Decoder decoder(current, output);
        decoder.Run();
    }))
{ }
