void demo(int n) {
    if(n) {}
    if(n) {} else if(n) {}
    if constexpr(n > 0) {}
    for(int i = 0; i < n; ++i) {}
    for co_await(auto x : range) {}
    while(n) {}
    do {} while(n);
    switch(n) { default: break; }
    try {} catch(...) {}
    if /* keep */(n) {}
    if
    (n) {}
    if CHECK(n) {}
    auto lambda = [](int x) { return x; };
    int value = demo_call  (n);
    int declaration  (int);
}
