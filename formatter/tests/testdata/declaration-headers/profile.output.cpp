struct Inventory
{
    int RetrieveObjectsByCategoryAndStorageLocation(
        TCategoryCode category,
        InventoryData::TLocationPath location) const override;

    int RetrieveObjectsByCategoryAndStorageLocation(TCategoryCode category, InventoryData::TLocationPath location) const
    {
        return 0;
    }
};
