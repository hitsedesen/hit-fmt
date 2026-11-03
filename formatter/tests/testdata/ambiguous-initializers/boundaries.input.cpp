void run() {
    A  < B >  C(firstLongArgument,secondLongArgument);
    A<B> C(First  *  pointer, Second  &  reference);
    A<B> C(int firstParameter, int secondParameter);
    A<B> C(unknown  < left, right >  value, tail);
    A<B> C(choose ? left, right : other, finalArgument);
    A<B> C(nested( first,  second ), anotherLongArgument);
    A<B> C(/* prefix */ firstLongArgument, secondLongArgument);
    A<B> C(firstLongArgument, // trailing
        secondLongArgument);
    A<B> C(KEEP( first,  second ), anotherLongArgument);
    A<B> KEEP(firstLongArgument, secondLongArgument);
    A<B> C(firstLongArgument, secondLongArgument) + tail;
    A<B> C(firstLongArgument, secondLongArgument), other;
    A<B> C(firstLongArgument, secondLongArgument) SUFFIX;
    A<B> C(firstLongArgument,, secondLongArgument);
    A<B> C(firstLongArgument, secondLongArgument,);
    A<B> C(firstLongArgument,
#if CHOICE
        secondLongArgument
#else
        thirdLongArgument
#endif
    );
    External * pointer(firstLongArgument, secondLongArgument);
}
