void Example()
{
        {
            int outer = 1;
                {
                    int inner = outer + 1;
                }
            Use(outer);
        }
}
