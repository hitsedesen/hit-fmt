void f() {
        EXPECT_EQ(GetDefault(UnwrapMember(GetMember(schema->FindChild("members")->AsList(), "sub")),
        "my_uint")->GetValue<ui64>(), 0u) << description;
        EXPECT_EQ(GetDefault(schema, "sub")->AsMap()->template GetChildValueOrThrow<ui64>("my_uint"), 8u) <<
        description;
}
