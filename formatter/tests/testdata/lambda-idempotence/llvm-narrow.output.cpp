void dump() {
  {
    {
      {
        emitChild([=] {
          visit(convert<SpecialClause>(
              node)->getExpression());
        });
      }
    }
  }
}

// hit-fmt off
void untouched() { accept( [ = ] {   keep( ); } ); }
// hit-fmt on
