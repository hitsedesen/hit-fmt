template<class... Ts> int n=sizeof...();
template<class... Us> int m=sizeof...( Us );
template<class... Vs> int bad=sizeof...(first, second);
