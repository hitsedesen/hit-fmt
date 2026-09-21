template <class... Arguments>
void Callback<void(Arguments...)>::function()
{ }

template <class Value>
void Wrapper<Outer<Value>>::function()
{ }
