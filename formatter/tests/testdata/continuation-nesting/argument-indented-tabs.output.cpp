void inspect() {
    call(
	  foo &&
		bar);
    call(
	  foo ||
		(bar &&
		      baz));
    call(
	  foo ||
		bar &&
		      baz);
    call(
	  foo ||
		bar ||
		baz);
    call(
	  foo ||
		nested(
		      bar &&
			    baz));
    call(
	  foo ||
		bar ==
		      baz);
}
