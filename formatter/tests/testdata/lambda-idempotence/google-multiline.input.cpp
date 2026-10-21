auto build(Handle value) {
    return Storage::CreateWithCleanup(data, count(size),
        [](
            const void*,
            void* state) {
            destroy_resource((Resource*)state);
        },
        value.release());
}

// hit-fmt off
void untouched() { accept( [ = ] {   keep( ); } ); }
// hit-fmt on
