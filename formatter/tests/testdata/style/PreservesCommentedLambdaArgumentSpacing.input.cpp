register_case(
    Sample,
    Inputs(),
    /*makeName*/ [] (const framework::Info<Suite::Parameter>& item) {
        return item.Name;
    });
