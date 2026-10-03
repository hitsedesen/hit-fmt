void Register()
{
    REGISTER_DERIVED_EXTERNALIZED_YSON_STRUCT(  TLeaf,
	TBranch  );
    DEFINE_ENUM(  EState, ((Idle)(0))  );
    PP_STRINGIZE(  left   +   right  );
    RegisterOrdinary(left, right);
}
