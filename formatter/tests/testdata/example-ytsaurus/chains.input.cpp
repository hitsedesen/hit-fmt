void Run(){
    Source().Begin().End();
    auto result=Source().Begin().End();
    Source()->Open().Close();
    Source()(1)(2);
    Consume(Source());
    auto nested=Wrap( // Outer argument.
        Wrap( // Inner argument.
            Source().Begin().End()));
}
