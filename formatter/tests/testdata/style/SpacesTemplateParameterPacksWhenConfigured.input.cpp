template<typename T,typename...Args>
void function(T value,Args&&... args)
{
    use<T, Args...>(value,args...);
}

template<class...Values>
struct Collection;
