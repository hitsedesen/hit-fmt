void SearchBooks()
{
    store->FindBooks(
        Shop::BookSearchQuery()
            .Select<Shop::Book>(
                Shop::ShelfPath(settings.GetShelf()).Columns(
                    Vector<String>{settings.GetColumns().begin(), settings.GetColumns().end()}))
            .Write<Shop::BookListing>(results.Name()),
        MakeVisitor<BookListingVisitor<BookCategory>>());
}
