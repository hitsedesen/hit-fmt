bind_handler("root", [](int value) { return build_handler(value, first_dependency, second_dependency, third_dependency, fourth_dependency); })
int following;
namespace Routes {
bind_handler("namespaced", [](int value) { return build_handler(value, first_dependency, second_dependency, third_dependency, fourth_dependency); });
}
struct Registry {
bind_handler("member", [] { return build_handler(first_dependency, second_dependency, third_dependency, fourth_dependency, fifth_dependency); })
};
void install() {
bind_handler("local", [] { return build_handler(first_dependency, second_dependency, third_dependency, fourth_dependency, fifth_dependency); })
bind_handler("semicolon", [] { return build_handler(first_dependency, second_dependency, third_dependency, fourth_dependency, fifth_dependency); });
bind_handler("commented", [] { return 1; }) /* keep */;
auto value = bind_handler("initializer", [] { return 1; });
bind_handler("result", [] { return 1; }).run();
bind_handler("result", [] { return 1; }) + other();
}
bind_handler_EXTRA("opaque", [](int value) { return build_handler(value, first_dependency, second_dependency, third_dependency, fourth_dependency); })
// hit-fmt off
bind_handler("off", [] { return build_handler( first_dependency , second_dependency , third_dependency , fourth_dependency , fifth_dependency ); })
// hit-fmt on
bind_handler({ expand(); });
bind_handler(first,,last);
